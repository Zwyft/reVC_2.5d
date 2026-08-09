#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

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
trim(const std::string &s)
{
	size_t first = 0;
	while(first < s.size() && std::isspace((unsigned char)s[first]))
		first++;
	size_t last = s.size();
	while(last > first && std::isspace((unsigned char)s[last - 1]))
		last--;
	return s.substr(first, last - first);
}

static void
readLocalConfig(std::string &assetRoot, std::string &outputRoot)
{
	std::ifstream f("vc-assets.local.properties");
	if(!f)
		return;

	std::string line;
	while(std::getline(f, line)){
		line = trim(line);
		if(line.empty() || line[0] == '#')
			continue;
		size_t eq = line.find('=');
		if(eq == std::string::npos)
			continue;
		std::string key = trim(line.substr(0, eq));
		std::string value = trim(line.substr(eq + 1));
		if(assetRoot.empty() && (key == "asset.root" || key == "revc.assetRoot"))
			assetRoot = value;
		else if(outputRoot.empty() && (key == "sprite.output" || key == "revc.spriteOutput"))
			outputRoot = value;
	}
}

static bool
exists(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

static bool
isDir(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

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

static std::string
upper(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return s;
}

static bool
dirHasExt(const std::string &dir, const char *ext)
{
	DIR *d = opendir(dir.c_str());
	if(d == NULL)
		return false;
	const std::string wanted = upper(ext);
	bool found = false;
	for(dirent *e = readdir(d); e; e = readdir(d)){
		std::string name = upper(e->d_name);
		if(name.size() >= wanted.size() && name.compare(name.size() - wanted.size(), wanted.size(), wanted) == 0){
			found = true;
			break;
		}
	}
	closedir(d);
	return found;
}

static std::vector<std::string>
validateAssetRoot(const std::string &root)
{
	std::vector<std::string> missing;
	const char *required[] = {
		"MODELS/GTA3.IMG",
		"MODELS/TXD.IMG",
		"DATA/DEFAULT.DAT",
		"DATA/ANIMVIEWER.DAT",
		"DATA/SPECIAL.TXT",
	};
	for(size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++)
		if(!exists(joinPath(root, required[i])))
			missing.push_back(required[i]);

	if(!isDir(joinPath(root, "MODELS")) || !dirHasExt(joinPath(root, "MODELS"), ".TXD"))
		missing.push_back("MODELS/*.TXD");
	if(!isDir(joinPath(root, "ANIM")) || !dirHasExt(joinPath(root, "ANIM"), ".IFP"))
		missing.push_back("ANIM/*.IFP");

	std::sort(missing.begin(), missing.end());
	return missing;
}

struct ManifestCoverage
{
	std::set<int> models;
	std::map<int, std::string> modelNames;
	std::set<std::string> atlases;
	std::set<std::string> frameKeys;
};

static std::string
frameKey(int model, const std::string &state, int direction)
{
	return std::to_string(model) + ":" + state + ":" + std::to_string(direction);
}

static bool
parseManifest(const std::string &manifestPath, ManifestCoverage &coverage, std::vector<std::string> &errors)
{
	std::ifstream f(manifestPath.c_str());
	if(!f){
		errors.push_back("Missing ped sprite manifest: " + manifestPath);
		return false;
	}

	std::string line;
	int lineNo = 0;
	while(std::getline(f, line)){
		lineNo++;
		line = trim(line);
		if(line.empty() || line[0] == '#')
			continue;

		char tag[16];
		if(sscanf(line.c_str(), "%15s", tag) != 1)
			continue;

		if(strcmp(tag, "model") == 0){
			int model;
			char name[64];
			if(sscanf(line.c_str(), "%*s %d %63s", &model, name) != 2){
				errors.push_back("Bad model line " + std::to_string(lineNo));
				continue;
			}
			coverage.models.insert(model);
			coverage.modelNames[model] = name;
		}else if(strcmp(tag, "atlas") == 0){
			char atlasName[32], path[128];
			int w, h;
			if(sscanf(line.c_str(), "%*s %31s %127s %d %d", atlasName, path, &w, &h) != 4 || w <= 0 || h <= 0){
				errors.push_back("Bad atlas line " + std::to_string(lineNo));
				continue;
			}
			coverage.atlases.insert(atlasName);
		}else if(strcmp(tag, "frame") == 0){
			int model, direction, frame, duration, x, y, w, h;
			char state[24], atlasName[32];
			float pivotX, pivotY, worldHeight;
			if(sscanf(line.c_str(), "%*s %d %23s %d %d %d %31s %d %d %d %d %f %f %f",
			    &model, state, &direction, &frame, &duration, atlasName, &x, &y, &w, &h, &pivotX, &pivotY, &worldHeight) != 13){
				errors.push_back("Bad frame line " + std::to_string(lineNo));
				continue;
			}
			if(direction < 0 || direction >= 8 || duration <= 0 || w <= 0 || h <= 0 || worldHeight <= 0.0f){
				errors.push_back("Invalid frame values on line " + std::to_string(lineNo));
				continue;
			}
			coverage.models.insert(model);
			coverage.frameKeys.insert(frameKey(model, state, direction));
			if(coverage.atlases.find(atlasName) == coverage.atlases.end())
				errors.push_back("Frame line " + std::to_string(lineNo) + " references unknown atlas " + atlasName);
		}else{
			errors.push_back("Unknown manifest tag on line " + std::to_string(lineNo) + ": " + tag);
		}
	}
	return errors.empty();
}

static bool
validateManifestCoverage(const std::string &outputRoot)
{
	std::vector<std::string> errors;
	ManifestCoverage coverage;
	parseManifest(joinPath(outputRoot, "sprites/peds/manifest.txt"), coverage, errors);

	if(coverage.models.empty())
		errors.push_back("Manifest has no model/frame entries");
	if(coverage.atlases.empty())
		errors.push_back("Manifest has no atlas entries");

	for(std::set<int>::const_iterator model = coverage.models.begin(); model != coverage.models.end(); ++model){
		for(size_t state = 0; state < sizeof(RequiredStates) / sizeof(RequiredStates[0]); state++){
			for(int direction = 0; direction < 8; direction++){
				std::string key = frameKey(*model, RequiredStates[state], direction);
				if(coverage.frameKeys.find(key) == coverage.frameKeys.end()){
					std::string name = coverage.modelNames.count(*model) ? coverage.modelNames[*model] : std::string("<unnamed>");
					errors.push_back("Missing model=" + std::to_string(*model) + " name=" + name + " state=" + RequiredStates[state] + " direction=" + std::to_string(direction));
				}
			}
		}
	}

	if(!errors.empty()){
		std::fprintf(stderr, "Invalid ped sprite output: %s\n", outputRoot.c_str());
		for(size_t i = 0; i < errors.size(); i++)
			std::fprintf(stderr, "  %s\n", errors[i].c_str());
		return false;
	}

	std::printf("Ped sprite output validated: %s\n", outputRoot.c_str());
	std::printf("Models: %zu, atlases: %zu, model-state-direction entries: %zu\n",
	    coverage.models.size(), coverage.atlases.size(), coverage.frameKeys.size());
	return true;
}

static void
usage(const char *argv0)
{
	std::fprintf(stderr,
		"usage: %s [--validate-output] --asset-root <owned GTA VC root> --output <generated sprite output>\n"
		"\n"
		"The output contract is sprites/peds/manifest.txt plus PNG atlases referenced by that manifest.\n"
		"This tool validates the user-owned asset root before any bake work starts.\n"
		"Config fallback: vc-assets.local.properties with asset.root and sprite.output.\n",
		argv0);
}

int
main(int argc, char **argv)
{
	std::string assetRoot;
	std::string outputRoot;
	bool validateOutputOnly = false;
	for(int i = 1; i < argc; i++){
		if(std::strcmp(argv[i], "--asset-root") == 0 && i + 1 < argc)
			assetRoot = argv[++i];
		else if(std::strcmp(argv[i], "--output") == 0 && i + 1 < argc)
			outputRoot = argv[++i];
		else if(std::strcmp(argv[i], "--validate-output") == 0)
			validateOutputOnly = true;
		else if(std::strcmp(argv[i], "--help") == 0){
			usage(argv[0]);
			return 0;
		}else{
			std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
			usage(argv[0]);
			return 2;
		}
	}

	if(assetRoot.empty()){
		const char *env = std::getenv("REVC_VC_ASSET_ROOT");
		if(env)
			assetRoot = env;
	}
	if(outputRoot.empty()){
		const char *env = std::getenv("REVC_SPRITE_OUTPUT");
		if(env)
			outputRoot = env;
	}
	readLocalConfig(assetRoot, outputRoot);

	if(outputRoot.empty() || (!validateOutputOnly && assetRoot.empty())){
		usage(argv[0]);
		return 2;
	}

	if(validateOutputOnly)
		return validateManifestCoverage(outputRoot) ? 0 : 1;

	std::vector<std::string> missing = validateAssetRoot(assetRoot);
	if(!missing.empty()){
		std::fprintf(stderr, "Invalid GTA VC asset root: %s\nMissing required asset inputs:\n", assetRoot.c_str());
		for(size_t i = 0; i < missing.size(); i++)
			std::fprintf(stderr, "  %s\n", missing[i].c_str());
		return 1;
	}

	std::printf("Asset root validated: %s\n", assetRoot.c_str());
	std::printf("Sprite output root: %s\n", outputRoot.c_str());
	std::fprintf(stderr,
		"Real DFF/TXD/IFP sprite baking is not connected yet. No procedural fallback atlas was generated.\n");
	return 3;
}
