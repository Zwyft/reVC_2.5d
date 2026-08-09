#include "atlas.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include "lodepng.h"

static const char *RequiredStates[] = {
	"idle",
	"walk",
	"run",
	"sprint",
	"crouch",
	"attack",
	"firearm",
	"hit",
	"death",
	"car_sit",
	"bike_ride",
	"enter_exit",
};

static std::string
joinPath(const std::string &a, const std::string &b)
{
	if(a.empty())
		return b;
	char last = a[a.size() - 1];
	if(last == '/' || last == '\\')
		return a + b;
	return a + "/" + b;
}

static bool
isDir(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool
makeDir(const std::string &path)
{
	if(path.empty() || isDir(path))
		return true;
	return mkdir(path.c_str(), 0775) == 0 || errno == EEXIST;
}

static bool
makeDirs(const std::string &path)
{
	if(path.empty() || isDir(path))
		return true;
	std::string partial;
	for(size_t i = 0; i < path.size(); i++){
		partial.push_back(path[i]);
		if(path[i] == '/' || path[i] == '\\'){
			if(partial.size() > 1 && !makeDir(partial))
				return false;
		}
	}
	return makeDir(path);
}

static std::string
safeName(std::string name)
{
	for(size_t i = 0; i < name.size(); i++){
		char &c = name[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			c = '_';
	}
	return name.empty() ? std::string("ped") : name;
}

static std::string
frameKey(int model, const std::string &state, int direction)
{
	return std::to_string(model) + ":" + state + ":" + std::to_string(direction);
}

struct PackedFrame
{
	PedCapturedFrame frame;
	std::string atlasName;
	int x;
	int y;
};

static bool
validateFramePixels(const PedCapturedFrame &frame, std::vector<std::string> &errors)
{
	if(frame.modelId < 0){
		errors.push_back("captured frame has invalid model id");
		return false;
	}
	if(frame.state.empty() || frame.direction < 0 || frame.direction >= 8 || frame.frameIndex < 0){
		errors.push_back("captured frame has invalid animation key for model " + std::to_string(frame.modelId));
		return false;
	}
	if(frame.durationMs <= 0 || frame.width <= 0 || frame.height <= 0 || frame.worldHeight <= 0.0f){
		errors.push_back("captured frame has invalid dimensions/timing for model " + std::to_string(frame.modelId));
		return false;
	}
	if(frame.rgba.size() != (size_t)frame.width * (size_t)frame.height * 4u){
		errors.push_back("captured frame pixel payload size mismatch for model " + std::to_string(frame.modelId) + " state " + frame.state);
		return false;
	}
	return true;
}

static bool
writeAtlasPng(const std::string &path, int width, int height, const std::vector<unsigned char> &rgba, std::vector<std::string> &errors)
{
	unsigned error = lodepng_encode32_file(path.c_str(), rgba.data(), (unsigned)width, (unsigned)height);
	if(error){
		errors.push_back("could not write atlas PNG " + path + ": " + lodepng_error_text(error));
		return false;
	}
	return true;
}

static bool
packModelAtlas(const std::string &outputRoot, const PedBakeTarget &target, const std::vector<PedCapturedFrame> &modelFrames,
    std::vector<PackedFrame> &packed, std::vector<std::string> &manifestAtlasLines, std::vector<std::string> &errors)
{
	const int padding = 2;
	const int maxAtlasWidth = 2048;
	int atlasWidth = 256;
	int atlasHeight = padding;
	int cursorX = padding;
	int cursorY = padding;
	int rowH = 0;

	for(size_t i = 0; i < modelFrames.size(); i++){
		const PedCapturedFrame &frame = modelFrames[i];
		if(frame.width + padding * 2 > maxAtlasWidth){
			errors.push_back("captured frame wider than atlas limit for model " + std::to_string(frame.modelId));
			return false;
		}
		while(atlasWidth < frame.width + padding * 2)
			atlasWidth <<= 1;
		if(cursorX + frame.width + padding > maxAtlasWidth){
			cursorX = padding;
			cursorY += rowH + padding;
			rowH = 0;
		}
		PackedFrame pf;
		pf.frame = frame;
		pf.x = cursorX;
		pf.y = cursorY;
		packed.push_back(pf);
		cursorX += frame.width + padding;
		rowH = std::max(rowH, frame.height);
		atlasWidth = std::max(atlasWidth, cursorX + padding);
		atlasHeight = std::max(atlasHeight, cursorY + rowH + padding);
	}

	int pow2Width = 1;
	while(pow2Width < atlasWidth)
		pow2Width <<= 1;
	int pow2Height = 1;
	while(pow2Height < atlasHeight)
		pow2Height <<= 1;

	std::vector<unsigned char> atlas((size_t)pow2Width * (size_t)pow2Height * 4u, 0);
	for(size_t i = 0; i < packed.size(); i++){
		PackedFrame &pf = packed[i];
		if(pf.frame.modelId != target.id)
			continue;
		for(int y = 0; y < pf.frame.height; y++){
			unsigned char *dst = &atlas[((size_t)(pf.y + y) * (size_t)pow2Width + (size_t)pf.x) * 4u];
			const unsigned char *src = &pf.frame.rgba[(size_t)y * (size_t)pf.frame.width * 4u];
			std::memcpy(dst, src, (size_t)pf.frame.width * 4u);
		}
	}

	std::string atlasName = "ped_" + std::to_string(target.id);
	std::string relPath = "sprites/peds/" + atlasName + "_" + safeName(target.name) + ".png";
	std::string atlasPath = joinPath(outputRoot, relPath);
	if(!writeAtlasPng(atlasPath, pow2Width, pow2Height, atlas, errors))
		return false;

	std::ostringstream atlasLine;
	atlasLine << "atlas " << atlasName << " " << relPath << " " << pow2Width << " " << pow2Height;
	manifestAtlasLines.push_back(atlasLine.str());
	for(size_t i = 0; i < packed.size(); i++){
		if(packed[i].frame.modelId == target.id)
			packed[i].atlasName = atlasName;
	}
	return true;
}

bool
WritePedSpriteAtlases(const std::string &outputRoot, const std::vector<PedBakeTarget> &targets,
    const std::vector<PedCapturedFrame> &frames, std::vector<std::string> &errors)
{
	if(targets.empty()){
		errors.push_back("no ped targets available for atlas generation");
		return false;
	}
	if(frames.empty()){
		errors.push_back("no captured RenderWare frames available; build/run the GL3 capture backend before atlas generation");
		return false;
	}

	std::string pedDir = joinPath(joinPath(outputRoot, "sprites"), "peds");
	if(!makeDirs(pedDir)){
		errors.push_back("could not create ped atlas output directory " + pedDir);
		return false;
	}

	std::map<int, PedBakeTarget> targetById;
	for(size_t i = 0; i < targets.size(); i++)
		targetById[targets[i].id] = targets[i];

	std::map<int, std::vector<PedCapturedFrame> > framesByModel;
	std::set<std::string> coverage;
	for(size_t i = 0; i < frames.size(); i++){
		const PedCapturedFrame &frame = frames[i];
		if(!validateFramePixels(frame, errors))
			continue;
		if(targetById.find(frame.modelId) == targetById.end()){
			errors.push_back("captured frame references unknown model " + std::to_string(frame.modelId));
			continue;
		}
		framesByModel[frame.modelId].push_back(frame);
		coverage.insert(frameKey(frame.modelId, frame.state, frame.direction));
	}

	for(size_t i = 0; i < targets.size(); i++){
		const PedBakeTarget &target = targets[i];
		for(size_t s = 0; s < sizeof(RequiredStates) / sizeof(RequiredStates[0]); s++){
			for(int direction = 0; direction < 8; direction++){
				std::string key = frameKey(target.id, RequiredStates[s], direction);
				if(coverage.find(key) == coverage.end()){
					errors.push_back("missing captured frame for model=" + std::to_string(target.id) + " name=" + target.name +
					    " state=" + RequiredStates[s] + " direction=" + std::to_string(direction));
					if(errors.size() > 64)
						return false;
				}
			}
		}
	}
	if(!errors.empty())
		return false;

	std::vector<PackedFrame> packed;
	std::vector<std::string> manifestAtlasLines;
	for(size_t i = 0; i < targets.size(); i++){
		std::vector<PedCapturedFrame> &modelFrames = framesByModel[targets[i].id];
		std::sort(modelFrames.begin(), modelFrames.end(), [](const PedCapturedFrame &a, const PedCapturedFrame &b) {
			if(a.state != b.state)
				return a.state < b.state;
			if(a.direction != b.direction)
				return a.direction < b.direction;
			return a.frameIndex < b.frameIndex;
		});
		if(!packModelAtlas(outputRoot, targets[i], modelFrames, packed, manifestAtlasLines, errors))
			return false;
	}

	std::string manifestPath = joinPath(pedDir, "manifest.txt");
	std::ofstream manifest(manifestPath.c_str());
	if(!manifest){
		errors.push_back("could not write ped sprite manifest " + manifestPath);
		return false;
	}

	manifest << "# reVC ped sprite manifest v1\n";
	for(size_t i = 0; i < targets.size(); i++)
		manifest << "model " << targets[i].id << " " << targets[i].name << "\n";
	for(size_t i = 0; i < manifestAtlasLines.size(); i++)
		manifest << manifestAtlasLines[i] << "\n";
	for(size_t i = 0; i < packed.size(); i++){
		const PackedFrame &pf = packed[i];
		manifest << "frame " << pf.frame.modelId << " " << pf.frame.state << " " << pf.frame.direction << " " << pf.frame.frameIndex
		         << " " << pf.frame.durationMs << " " << pf.atlasName << " " << pf.x << " " << pf.y << " " << pf.frame.width << " "
		         << pf.frame.height << " " << pf.frame.pivotX << " " << pf.frame.pivotY << " " << pf.frame.worldHeight << "\n";
	}

	std::printf("Ped sprite manifest written: %s\n", manifestPath.c_str());
	std::printf("Packed %zu captured frames for %zu ped targets.\n", packed.size(), targets.size());
	return true;
}
