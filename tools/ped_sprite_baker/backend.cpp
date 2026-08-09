#include "backend.h"
#include "atlas.h"

#ifdef REVC_PED_BAKER_WITH_LIBRW

#include <rw.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

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
lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
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
resolvePath(const std::string &path)
{
	if(path.empty() || exists(path))
		return path;
	std::string out;
	size_t pos = 0;
	if(path[0] == '/'){
		out = "/";
		pos = 1;
	}
	while(pos <= path.size()){
		size_t next = path.find_first_of("/\\", pos);
		std::string part = path.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
		if(!part.empty() && part != "."){
			std::string dir = out.empty() ? std::string(".") : out;
			std::string candidate = joinPath(out, part);
			if(!exists(candidate)){
				DIR *d = opendir(dir.c_str());
				bool found = false;
				if(d){
					std::string wanted = lower(part);
					for(dirent *e = readdir(d); e; e = readdir(d)){
						if(lower(e->d_name) == wanted){
							candidate = joinPath(out, e->d_name);
							found = true;
							break;
						}
					}
					closedir(d);
				}
				if(!found)
					return path;
			}
			out = candidate;
		}
		if(next == std::string::npos)
			break;
		pos = next + 1;
	}
	return out;
}

static bool
readFile(const std::string &path, std::vector<rw::uint8> &data)
{
	std::ifstream f(resolvePath(path).c_str(), std::ios::binary);
	if(!f)
		return false;
	f.seekg(0, std::ios::end);
	std::streamoff size = f.tellg();
	if(size <= 0)
		return false;
	f.seekg(0, std::ios::beg);
	data.resize((size_t)size);
	f.read((char*)data.data(), size);
	return f.good();
}

static rw::uint32
readLE32(const rw::uint8 *p)
{
	return (rw::uint32)p[0] | ((rw::uint32)p[1] << 8) | ((rw::uint32)p[2] << 16) | ((rw::uint32)p[3] << 24);
}

class ImgArchive
{
public:
	bool open(const std::string &path, const std::string &dirPath)
	{
		mPath = path;
		mEntries.clear();

		mPath = resolvePath(mPath);
		std::string resolvedDirPath = resolvePath(dirPath);
		if(!exists(mPath))
			return false;

		if(exists(resolvedDirPath)){
			std::ifstream dir(resolvedDirPath.c_str(), std::ios::binary);
			if(!dir)
				return false;
			dir.seekg(0, std::ios::end);
			std::streamoff dirSize = dir.tellg();
			dir.seekg(0, std::ios::beg);
			if(dirSize <= 0 || dirSize % 32 != 0)
				return false;
			return readDirectory(dir, (rw::uint32)(dirSize / 32), false);
		}

		std::ifstream f(path.c_str(), std::ios::binary);
		if(!f)
			return false;

		char magic[4];
		f.read(magic, 4);
		if(!f)
			return false;

		if(std::memcmp(magic, "VER2", 4) == 0){
			rw::uint8 countBytes[4];
			f.read((char*)countBytes, 4);
			if(!f)
				return false;
			rw::uint32 count = readLE32(countBytes);
			return readDirectory(f, count, true);
		}

		return false;
	}

	bool readEntry(const std::string &name, std::vector<rw::uint8> &data) const
	{
		std::map<std::string, Entry>::const_iterator it = mEntries.find(lower(name));
		if(it == mEntries.end())
			return false;
		std::ifstream f(mPath.c_str(), std::ios::binary);
		if(!f)
			return false;
		const Entry &entry = it->second;
		data.resize(entry.size);
		f.seekg(entry.offset, std::ios::beg);
		f.read((char*)data.data(), entry.size);
		return f.good();
	}

private:
	struct Entry
	{
		rw::uint32 offset;
		rw::uint32 size;
	};

	bool readDirectory(std::ifstream &f, rw::uint32 count, bool hasHeader)
	{
		const rw::uint32 sectorSize = 2048;
		if(hasHeader)
			f.seekg(8, std::ios::beg);
		for(rw::uint32 i = 0; i < count; i++){
			rw::uint8 buf[32];
			f.read((char*)buf, sizeof(buf));
			if(!f)
				return false;
			rw::uint32 offsetSectors = readLE32(buf + 0);
			rw::uint32 sizeSectors = readLE32(buf + 4);
			char nameBuf[25];
			std::memcpy(nameBuf, buf + 8, 24);
			nameBuf[24] = '\0';
			std::string name = lower(nameBuf);
			name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
			if(name.empty())
				continue;
			Entry entry;
			entry.offset = offsetSectors * sectorSize;
			entry.size = sizeSectors * sectorSize;
			mEntries[name] = entry;
		}
		return !mEntries.empty();
	}

	std::string mPath;
	std::map<std::string, Entry> mEntries;
};

class RwEngineScope
{
public:
	RwEngineScope() : mInitialized(false), mOpened(false), mStarted(false) {}

	bool start()
	{
#ifdef RW_GL3
		if(std::getenv("DISPLAY") == nil && std::getenv("WAYLAND_DISPLAY") == nil){
			std::fprintf(stderr, "GL3 ped sprite baking requires a graphics session. Run under X/Wayland or xvfb-run.\n");
			return false;
		}
#endif
		if(!rw::Engine::init())
			return false;
		mInitialized = true;
		rw::registerMeshPlugin();
		rw::registerNativeDataPlugin();
		rw::registerAtomicRightsPlugin();
		rw::registerMaterialRightsPlugin();
		rw::xbox::registerVertexFormatPlugin();
		rw::registerSkinPlugin();
		rw::registerUserDataPlugin();
		rw::registerHAnimPlugin();
		rw::registerMatFXPlugin();
		rw::registerUVAnimPlugin();
#ifdef RW_GL3
		mOpenParams.width = 256;
		mOpenParams.height = 256;
		mOpenParams.windowtitle = "revc_ped_sprite_baker";
		mOpenParams.window = &mWindow;
		if(!rw::Engine::open(&mOpenParams))
			return false;
#else
		if(!rw::Engine::open(nil))
			return false;
#endif
		mOpened = true;
		if(!rw::Engine::start())
			return false;
		mStarted = true;
		return true;
	}

	~RwEngineScope()
	{
		if(mStarted)
			rw::Engine::stop();
		if(mOpened)
			rw::Engine::close();
		if(mInitialized && mOpened)
			rw::Engine::term();
	}

private:
	bool mInitialized;
	bool mOpened;
	bool mStarted;
#ifdef RW_GL3
	rw::EngineOpenParams mOpenParams;
	GLFWwindow *mWindow = nil;
#endif
};

static rw::TexDictionary*
loadTxdFromBytes(std::vector<rw::uint8> &bytes)
{
	rw::StreamMemory stream;
	stream.open(bytes.data(), (rw::uint32)bytes.size());
	if(!rw::findChunk(&stream, rw::ID_TEXDICTIONARY, nil, nil))
		return nil;
	return rw::TexDictionary::streamRead(&stream);
}

static rw::Clump*
loadClumpFromBytes(std::vector<rw::uint8> &bytes)
{
	rw::StreamMemory stream;
	stream.open(bytes.data(), (rw::uint32)bytes.size());
	if(!rw::findChunk(&stream, rw::ID_CLUMP, nil, nil))
		return nil;
	return rw::Clump::streamRead(&stream);
}

static rw::Frame*
findHierarchyFrameCB(rw::Frame *frame, void *data)
{
	rw::HAnimData *hanim = rw::HAnimData::get(frame);
	if(hanim && hanim->hierarchy){
		*(rw::HAnimHierarchy**)data = hanim->hierarchy;
		return nil;
	}
	frame->forAllChildren(findHierarchyFrameCB, data);
	return frame;
}

static rw::HAnimHierarchy*
findHAnimHierarchy(rw::Clump *clump)
{
	rw::HAnimHierarchy *hierarchy = nil;
	if(clump && clump->getFrame())
		findHierarchyFrameCB(clump->getFrame(), &hierarchy);
	return hierarchy;
}

static void
initHierarchyFromFrames(rw::HAnimHierarchy *hierarchy)
{
	if(hierarchy == nil)
		return;
	for(rw::int32 i = 0; i < hierarchy->numNodes; i++){
		if(hierarchy->nodeInfo[i].frame)
			hierarchy->matrices[hierarchy->nodeInfo[i].index] = *hierarchy->nodeInfo[i].frame->getLTM();
	}
}

struct ClumpStats
{
	int atomics;
	int skinnedAtomics;
	int vertices;
	int triangles;
	int materials;
	int hierarchyNodes;
	bool hasHierarchy;
	bool hasNativeOnlyGeometry;

	ClumpStats()
	    : atomics(0), skinnedAtomics(0), vertices(0), triangles(0), materials(0), hierarchyNodes(0), hasHierarchy(false),
	      hasNativeOnlyGeometry(false)
	{
	}
};

static void
prepareClumpForBake(rw::Clump *clump, ClumpStats &stats)
{
	rw::HAnimHierarchy *hierarchy = findHAnimHierarchy(clump);
	if(hierarchy){
		stats.hasHierarchy = true;
		stats.hierarchyNodes = hierarchy->numNodes;
		hierarchy->attach();
		initHierarchyFromFrames(hierarchy);
	}

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		stats.atomics++;
		atomic->pipeline = nil;

		rw::Geometry *geometry = atomic->geometry;
		if(geometry == nil)
			continue;

		stats.vertices += geometry->numVertices;
		stats.triangles += geometry->numTriangles;
		stats.materials += geometry->matList.numMaterials;

		// The host baker renders clumps without inserting them into a world.
		// GL3's default lighting path expects a current world when LIGHT is set,
		// so bake flat textured pixels and apply sprite-style lighting afterward.
		geometry->flags &= ~rw::Geometry::LIGHT;
		geometry->flags |= rw::Geometry::MODULATE;

		if((geometry->flags & rw::Geometry::NATIVE) && geometry->numVertices == 0)
			stats.hasNativeOnlyGeometry = true;

		if(rw::Skin::get(geometry)){
			stats.skinnedAtomics++;
			if(hierarchy)
				rw::Skin::setHierarchy(atomic, hierarchy);
		}
	}
}

static bool
validateClumpStats(const PedBakeTarget &target, const ClumpStats &stats, std::vector<std::string> &errors)
{
	bool ok = true;
	const std::string label = "model " + std::to_string(target.id) + " " + target.name;
	if(stats.atomics <= 0){
		errors.push_back("no atomics in DFF for " + label);
		ok = false;
	}
	if(stats.vertices <= 0 || stats.triangles <= 0){
		errors.push_back("no renderable geometry in DFF for " + label);
		ok = false;
	}
	if(stats.skinnedAtomics > 0 && !stats.hasHierarchy){
		errors.push_back("skinned geometry has no HAnim hierarchy for " + label);
		ok = false;
	}
	if(stats.hasNativeOnlyGeometry){
		errors.push_back("native-only geometry cannot be baked on host backend for " + label);
		ok = false;
	}
	return ok;
}

struct IfpKeyFrame
{
	rw::Quat rot;
	rw::V3d trans;
	float time;
	bool hasTranslation;
};

struct IfpSequence
{
	std::string name;
	int boneTag;
	std::vector<IfpKeyFrame> keys;
};

struct IfpAnimation
{
	std::string name;
	float duration;
	std::vector<IfpSequence> sequences;
};

class IfpLibrary
{
public:
	bool loadPedIfp(const std::string &assetRoot, std::vector<std::string> &errors)
	{
		std::vector<rw::uint8> bytes;
		std::string path = joinPath(joinPath(assetRoot, "ANIM"), "PED.IFP");
		if(!readFile(path, bytes)){
			errors.push_back("could not read ANIM/PED.IFP for sprite animation sampling");
			return false;
		}
		const rw::uint8 *data = bytes.data();
		size_t size = bytes.size();
		size_t off = 0;
		Chunk anpk, info;
		if(!readChunk(data, size, off, anpk) || anpk.ident != "ANPK" || !readChunk(data, size, off, info) || info.ident != "INFO"){
			errors.push_back("ANIM/PED.IFP has unexpected header");
			return false;
		}
		if(off + info.paddedSize() > size || info.size < 8){
			errors.push_back("ANIM/PED.IFP has invalid INFO block");
			return false;
		}
		int numAnimations = readS32(data + off);
		off += info.paddedSize();
		for(int i = 0; i < numAnimations; i++){
			IfpAnimation anim;
			Chunk name;
			if(!readChunk(data, size, off, name) || name.ident != "NAME" || off + name.paddedSize() > size)
				return fail(errors, "ANIM/PED.IFP has invalid animation NAME block");
			anim.name = readCString(data + off, name.size);
			off += name.paddedSize();

			Chunk dgan, dginfo;
			if(!readChunk(data, size, off, dgan) || dgan.ident != "DGAN" || !readChunk(data, size, off, dginfo) || dginfo.ident != "INFO" || off + dginfo.paddedSize() > size || dginfo.size < 4)
				return fail(errors, "ANIM/PED.IFP has invalid DGAN block for " + anim.name);
			int numSequences = readS32(data + off);
			off += dginfo.paddedSize();
			anim.duration = 0.0f;

			for(int s = 0; s < numSequences; s++){
				Chunk cpan, seqInfo;
				if(!readChunk(data, size, off, cpan) || cpan.ident != "CPAN" || !readChunk(data, size, off, seqInfo) || seqInfo.ident != "ANIM" || off + seqInfo.paddedSize() > size || seqInfo.size < 32)
					return fail(errors, "ANIM/PED.IFP has invalid CPAN block for " + anim.name);
				IfpSequence seq;
				seq.name = readCString(data + off, std::min<size_t>(seqInfo.size, 24));
				int numFrames = readS32(data + off + 28);
				seq.boneTag = seqInfo.size >= 44 ? readS32(data + off + 40) : 0;
				off += seqInfo.paddedSize();
				if(numFrames > 0){
					Chunk keyInfo;
					if(!readChunk(data, size, off, keyInfo))
						return fail(errors, "ANIM/PED.IFP has missing key data for " + anim.name + ":" + seq.name);
					int stride = 0;
					bool hasTranslation = false;
					bool hasScale = false;
					if(keyInfo.ident == "KRTS"){
						stride = 0x2C;
						hasTranslation = true;
						hasScale = true;
					}else if(keyInfo.ident == "KRT0"){
						stride = 0x20;
						hasTranslation = true;
					}else if(keyInfo.ident == "KR00"){
						stride = 0x14;
					}else
						return fail(errors, "ANIM/PED.IFP has unsupported key block " + keyInfo.ident + " for " + anim.name + ":" + seq.name);
					if(off + (size_t)stride * (size_t)numFrames > size)
						return fail(errors, "ANIM/PED.IFP key data overruns file for " + anim.name + ":" + seq.name);
					for(int k = 0; k < numFrames; k++){
						const rw::uint8 *p = data + off + (size_t)stride * (size_t)k;
						IfpKeyFrame key;
						key.rot = rw::conj(rw::makeQuat(readF32(p + 12), readF32(p + 0), readF32(p + 4), readF32(p + 8)));
						key.hasTranslation = hasTranslation;
						key.trans = { 0.0f, 0.0f, 0.0f };
						if(hasTranslation)
							key.trans = { readF32(p + 16), readF32(p + 20), readF32(p + 24) };
						key.time = readF32(p + (hasScale ? 40 : hasTranslation ? 28 : 16));
						seq.keys.push_back(key);
						anim.duration = std::max(anim.duration, key.time);
					}
					off += (size_t)stride * (size_t)numFrames;
				}
				anim.sequences.push_back(seq);
			}
			mAnimations[lower(anim.name)] = anim;
		}
		return true;
	}

	const IfpAnimation *find(const std::string &name) const
	{
		std::map<std::string, IfpAnimation>::const_iterator it = mAnimations.find(lower(name));
		return it == mAnimations.end() ? nil : &it->second;
	}

private:
	struct Chunk
	{
		std::string ident;
		rw::uint32 size;
		size_t paddedSize(void) const { return (size + 3u) & ~3u; }
	};

	static bool fail(std::vector<std::string> &errors, const std::string &message)
	{
		errors.push_back(message);
		return false;
	}

	static bool readChunk(const rw::uint8 *data, size_t size, size_t &off, Chunk &chunk)
	{
		if(off + 8 > size)
			return false;
		chunk.ident.assign((const char*)data + off, 4);
		chunk.size = readU32(data + off + 4);
		off += 8;
		return off <= size;
	}

	static rw::uint32 readU32(const rw::uint8 *p)
	{
		return (rw::uint32)p[0] | ((rw::uint32)p[1] << 8) | ((rw::uint32)p[2] << 16) | ((rw::uint32)p[3] << 24);
	}

	static int readS32(const rw::uint8 *p)
	{
		return (int)readU32(p);
	}

	static float readF32(const rw::uint8 *p)
	{
		float f;
		std::memcpy(&f, p, sizeof(f));
		return f;
	}

	static std::string readCString(const rw::uint8 *p, size_t maxLen)
	{
		size_t len = 0;
		while(len < maxLen && p[len] != 0)
			len++;
		return std::string((const char*)p, len);
	}

	std::map<std::string, IfpAnimation> mAnimations;
};

struct SpriteStateAnim
{
	const char *state;
	const char *anim;
	float sample;
	int durationMs;
};

static const SpriteStateAnim RequiredSpriteStateAnims[] = {
	{ "idle", "IDLE_stance", 0.0f, 160 },
	{ "walk", "WALK_civi", 0.35f, 110 },
	{ "run", "run_civi", 0.35f, 90 },
	{ "sprint", "sprint_civi", 0.35f, 80 },
	{ "crouch", "DUCK_low", 0.55f, 140 },
	{ "attack", "FIGHTpunch", 0.45f, 90 },
	{ "firearm", "SHOT_partial", 0.55f, 90 },
	{ "hit", "HIT_front", 0.45f, 120 },
	{ "death", "KO_shot_front", 0.70f, 180 },
	{ "car_sit", "CAR_sit", 0.0f, 160 },
	{ "bike_ride", "BIKE_pullupL", 0.55f, 160 },
	{ "enter_exit", "CAR_getin_LHS", 0.45f, 120 },
};

static bool
sampleSequence(const IfpSequence &seq, float time, rw::Quat &rot, rw::V3d &trans, bool &hasTranslation)
{
	if(seq.keys.empty())
		return false;
	if(seq.keys.size() == 1 || time <= seq.keys.front().time){
		rot = seq.keys.front().rot;
		trans = seq.keys.front().trans;
		hasTranslation = seq.keys.front().hasTranslation;
		return true;
	}
	for(size_t i = 1; i < seq.keys.size(); i++){
		const IfpKeyFrame &a = seq.keys[i - 1];
		const IfpKeyFrame &b = seq.keys[i];
		if(time <= b.time){
			float span = b.time - a.time;
			float t = span > 0.0f ? (time - a.time) / span : 0.0f;
			rot = rw::slerp(a.rot, b.rot, t);
			trans = rw::lerp(a.trans, b.trans, t);
			hasTranslation = a.hasTranslation || b.hasTranslation;
			return true;
		}
	}
	rot = seq.keys.back().rot;
	trans = seq.keys.back().trans;
	hasTranslation = seq.keys.back().hasTranslation;
	return true;
}

static const IfpSequence*
findSequenceForNode(const IfpAnimation &anim, int boneTag)
{
	for(size_t i = 0; i < anim.sequences.size(); i++)
		if(anim.sequences[i].boneTag == boneTag)
			return &anim.sequences[i];
	return nil;
}

static void
applyAnimationPose(rw::Clump *clump, const IfpAnimation *anim, float sample)
{
	rw::HAnimHierarchy *hierarchy = findHAnimHierarchy(clump);
	if(hierarchy == nil || hierarchy->matrices == nil)
		return;

	float time = anim && anim->duration > 0.0f ? anim->duration * sample : 0.0f;
	rw::Matrix rootMat;
	rootMat.setIdentity();
	rw::Matrix *parentMat = &rootMat;
	rw::Matrix *stack[64];
	rw::Matrix **sp = stack;
	*sp++ = parentMat;

	for(int i = 0; i < hierarchy->numNodes; i++){
		rw::Matrix local;
		local.setIdentity();
		if(hierarchy->nodeInfo[i].frame)
			local = hierarchy->nodeInfo[i].frame->matrix;

		const IfpSequence *seq = anim ? findSequenceForNode(*anim, hierarchy->nodeInfo[i].id) : nil;
		if(seq){
			rw::Quat rot;
			rw::V3d trans;
			bool hasTranslation = false;
			if(sampleSequence(*seq, time, rot, trans, hasTranslation)){
				rw::V3d basePos = local.pos;
				local.setIdentity();
				local.rotate(rot, rw::COMBINEREPLACE);
				local.pos = hasTranslation ? trans : basePos;
			}
		}

		rw::Matrix::mult(&hierarchy->matrices[hierarchy->nodeInfo[i].index], &local, parentMat);
		if(hierarchy->nodeInfo[i].flags & rw::HAnimHierarchy::PUSH)
			*sp++ = parentMat;
		parentMat = &hierarchy->matrices[hierarchy->nodeInfo[i].index];
		if(hierarchy->nodeInfo[i].flags & rw::HAnimHierarchy::POP)
			parentMat = *--sp;
	}
}

static bool
writeBackendReport(const std::string &outputRoot, const std::vector<std::string> &lines, std::vector<std::string> &errors)
{
	std::string spriteDir = joinPath(outputRoot, "sprites");
	std::string reportDir = joinPath(spriteDir, "peds");
	if(mkdir(spriteDir.c_str(), 0775) != 0 && errno != EEXIST){
		errors.push_back("could not create backend report directory " + spriteDir);
		return false;
	}
	if(mkdir(reportDir.c_str(), 0775) != 0 && errno != EEXIST){
		errors.push_back("could not create backend report directory " + reportDir);
		return false;
	}

	std::string reportPath = joinPath(reportDir, "backend-report.txt");
	std::ofstream out(reportPath.c_str());
	if(!out){
		errors.push_back("could not write backend report " + reportPath);
		return false;
	}

	out << "# reVC ped sprite RenderWare backend report v1\n";
	for(size_t i = 0; i < lines.size(); i++)
		out << lines[i] << "\n";
	std::printf("Ped RenderWare backend report written: %s\n", reportPath.c_str());
	return true;
}

#ifdef RW_GL3
class RwCaptureContext
{
public:
	RwCaptureContext() : mCamera(nil) {}

	bool create(std::vector<std::string> &errors)
	{
		if(mCamera)
			return true;
		mCamera = rw::Camera::create();
		if(mCamera == nil){
			errors.push_back("could not create RenderWare bake camera");
			return false;
		}
		rw::Frame *frame = rw::Frame::create();
		if(frame == nil){
			errors.push_back("could not create RenderWare bake camera frame");
			return false;
		}
		mCamera->setFrame(frame);
		mCamera->frameBuffer = rw::Raster::create(256, 256, 32, rw::Raster::C8888 | rw::Raster::CAMERATEXTURE);
		mCamera->zBuffer = rw::Raster::create(256, 256, 0, rw::Raster::ZBUFFER);
		if(mCamera->frameBuffer == nil || mCamera->zBuffer == nil){
			errors.push_back("could not create RenderWare bake camera texture target");
			return false;
		}
		mCamera->setProjection(rw::Camera::PARALLEL);
		rw::V2d viewWindow = { 1.2f, 1.8f };
		mCamera->setViewWindow(&viewWindow);
		mCamera->setNearPlane(0.1f);
		mCamera->setFarPlane(40.0f);
		return true;
	}

	~RwCaptureContext()
	{
		if(mCamera){
			rw::Raster *fb = mCamera->frameBuffer;
			rw::Raster *zb = mCamera->zBuffer;
			mCamera->frameBuffer = nil;
			mCamera->zBuffer = nil;
			rw::Frame *frame = mCamera->getFrame();
			mCamera->setFrame(nil);
			if(fb)
				fb->destroy();
			if(zb)
				zb->destroy();
			if(frame)
				frame->destroy();
			mCamera->destroy();
		}
	}

	bool captureDirection(const PedBakeTarget &target, rw::Clump *clump, const SpriteStateAnim &state, int direction, PedCapturedFrame &out,
	    std::vector<std::string> &errors)
	{
		if(!create(errors))
			return false;
		if(clump == nil || clump->getFrame() == nil){
			errors.push_back("cannot capture model without clump frame " + std::to_string(target.id) + " " + target.name);
			return false;
		}

		const float angleDeg = (float)direction * 45.0f;
		rw::V3d zaxis = { 0.0f, 0.0f, 1.0f };
		clump->getFrame()->rotate(&zaxis, angleDeg, rw::COMBINEREPLACE);
		clump->getFrame()->updateObjects();

		rw::Matrix *cam = &mCamera->getFrame()->matrix;
		cam->right = { 1.0f, 0.0f, 0.0f };
		cam->up = { 0.0f, 0.0f, 1.0f };
		cam->at = { 0.0f, 1.0f, 0.0f };
		cam->pos = { 0.0f, -8.0f, 0.95f };
		mCamera->getFrame()->updateObjects();

		rw::RGBA clear = { 0, 0, 0, 0 };
		mCamera->clear(&clear, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
		mCamera->beginUpdate();
		clump->render();
		mCamera->endUpdate();

		rw::Image *image = mCamera->frameBuffer->toImage();
		if(image == nil || image->depth != 32){
			if(image)
				image->destroy();
			errors.push_back("could not read 32-bit RGBA capture for model " + std::to_string(target.id) + " " + target.name);
			return false;
		}

		out.modelId = target.id;
		out.modelName = target.name;
		out.state = state.state;
		out.direction = direction;
		out.frameIndex = 0;
		out.durationMs = state.durationMs;
		out.width = image->width;
		out.height = image->height;
		out.pivotX = 0.5f;
		out.pivotY = 0.92f;
		out.worldHeight = 1.8f;
		out.rgba.resize((size_t)image->width * (size_t)image->height * 4u);
		for(int y = 0; y < image->height; y++){
			const rw::uint8 *src = image->pixels + (size_t)y * (size_t)image->stride;
			rw::uint8 *dst = out.rgba.data() + (size_t)y * (size_t)image->width * 4u;
			std::memcpy(dst, src, (size_t)image->width * 4u);
		}
		image->destroy();
		return true;
	}

private:
	rw::Camera *mCamera;
};

static bool
captureAvailableFrames(const PedBakeTarget &target, rw::Clump *clump, RwCaptureContext &capture, std::vector<PedCapturedFrame> &frames,
    const IfpLibrary &ifpLibrary, std::vector<std::string> &errors)
{
	for(size_t stateIndex = 0; stateIndex < sizeof(RequiredSpriteStateAnims) / sizeof(RequiredSpriteStateAnims[0]); stateIndex++){
		const SpriteStateAnim &state = RequiredSpriteStateAnims[stateIndex];
		const IfpAnimation *anim = ifpLibrary.find(state.anim);
		if(anim == nil){
			errors.push_back("missing required PED.IFP animation " + std::string(state.anim) + " for sprite state " + state.state);
			return false;
		}
		applyAnimationPose(clump, anim, state.sample);
		for(int direction = 0; direction < 8; direction++){
			PedCapturedFrame frame;
			if(!capture.captureDirection(target, clump, state, direction, frame, errors))
				return false;
			frames.push_back(frame);
		}
	}
	return true;
}
#else
static bool
captureAvailableFrames(const PedBakeTarget &target, rw::Clump *clump, std::vector<PedCapturedFrame> &frames, const IfpLibrary &ifpLibrary,
    std::vector<std::string> &errors)
{
	(void)target;
	(void)clump;
	(void)frames;
	(void)ifpLibrary;
	errors.push_back("real frame capture requires a baker built with LIBRW_PLATFORM=GL3");
	return false;
}
#endif

static bool
loadTxd(const std::string &assetRoot, const ImgArchive &gtaImg, const ImgArchive &txdImg, const std::string &modelName, rw::TexDictionary **out)
{
	std::vector<rw::uint8> bytes;
	const std::string fileName = lower(modelName) + ".txd";
	std::string loose = joinPath(joinPath(assetRoot, "MODELS"), modelName + ".TXD");
	if(!readFile(loose, bytes)){
		loose = joinPath(joinPath(assetRoot, "MODELS"), fileName);
		if(!readFile(loose, bytes) && !txdImg.readEntry(fileName, bytes) && !gtaImg.readEntry(fileName, bytes))
			return false;
	}

	*out = loadTxdFromBytes(bytes);
	return *out != nil;
}

static bool
loadDff(const ImgArchive &gtaImg, const std::string &modelName, rw::Clump **out)
{
	std::vector<rw::uint8> bytes;
	if(!gtaImg.readEntry(lower(modelName) + ".dff", bytes))
		return false;
	*out = loadClumpFromBytes(bytes);
	return *out != nil;
}

static bool
scanIfpFile(const std::string &path, int &animationChunks)
{
	std::vector<rw::uint8> bytes;
	if(!readFile(path, bytes))
		return false;
	animationChunks = 0;
	for(size_t i = 0; i + 4 <= bytes.size(); i++){
		if(std::memcmp(bytes.data() + i, "ANPK", 4) == 0 || std::memcmp(bytes.data() + i, "ANP3", 4) == 0 ||
		   std::memcmp(bytes.data() + i, "ANP2", 4) == 0 || std::memcmp(bytes.data() + i, "ANIM", 4) == 0)
			animationChunks++;
	}
	return animationChunks > 0;
}

static bool
scanRequiredIfps(const std::string &assetRoot, std::vector<std::string> &errors)
{
	std::string animDir = resolvePath(joinPath(assetRoot, "ANIM"));
	DIR *dir = opendir(animDir.c_str());
	if(dir == NULL){
		errors.push_back("missing ANIM directory");
		return false;
	}

	int readable = 0;
	bool foundPed = false;
	for(dirent *e = readdir(dir); e; e = readdir(dir)){
		std::string name = e->d_name;
		std::string lname = lower(name);
		if(lname.size() < 4 || lname.compare(lname.size() - 4, 4, ".ifp") != 0)
			continue;
		int chunks = 0;
		std::string path = joinPath(animDir, name);
		if(!scanIfpFile(path, chunks)){
			errors.push_back("unreadable IFP animation package " + path);
			continue;
		}
		readable++;
		if(lname == "ped.ifp")
			foundPed = true;
	}
	closedir(dir);

	if(readable == 0)
		errors.push_back("no readable ANIM/*.IFP packages found");
	if(!foundPed)
		errors.push_back("missing readable ANIM/PED.IFP");
	return errors.empty();
}

static bool
createOffscreenDescriptor(int width, int height, std::vector<std::string> &errors)
{
	if(width <= 0 || height <= 0){
		errors.push_back("invalid offscreen target size");
		return false;
	}

	// The NULL librw device can parse streams without a GL context. Actual framebuffer
	// readback is intentionally deferred until the GL3/SDL2 baker runtime is wired.
	return true;
}

int
RunPedSpriteBakeBackend(const PedBakeBackendOptions &options)
{
	std::vector<std::string> errors;
	RwEngineScope rw;
	if(!rw.start()){
		std::fprintf(stderr, "Could not initialize librw for ped sprite baking.\n");
		return 1;
	}

	std::vector<PedCapturedFrame> capturedFrames;
#ifdef RW_GL3
	RwCaptureContext captureContext;
#endif
	IfpLibrary ifpLibrary;

	ImgArchive gtaImg;
	ImgArchive txdImg;
	std::string modelDir = joinPath(options.assetRoot, "MODELS");
	if(!gtaImg.open(joinPath(modelDir, "GTA3.IMG"), joinPath(modelDir, "GTA3.DIR")))
		errors.push_back("could not read MODELS/GTA3.IMG directory data; expected MODELS/GTA3.DIR sidecar or embedded IMG directory");
	bool haveTxdImg = txdImg.open(joinPath(modelDir, "TXD.IMG"), joinPath(modelDir, "TXD.DIR"));
	if(!haveTxdImg)
		std::printf("No MODELS/TXD.IMG found; ped TXDs will be loaded from loose MODELS/*.TXD or GTA3.IMG.\n");
	if(!options.validateOnly)
		ifpLibrary.loadPedIfp(options.assetRoot, errors);

	if(errors.empty()){
		int loaded = 0;
		std::vector<std::string> reportLines;
		for(size_t i = 0; i < options.targets.size(); i++){
			const PedBakeTarget &target = options.targets[i];
			rw::TexDictionary *txd = nil;
			rw::Clump *clump = nil;
			if(!loadTxd(options.assetRoot, gtaImg, txdImg, target.name, &txd))
				errors.push_back("missing/unreadable TXD for model " + std::to_string(target.id) + " " + target.name);
			else
				rw::TexDictionary::setCurrent(txd);

			if(!loadDff(gtaImg, target.name, &clump))
				errors.push_back("missing/unreadable DFF for model " + std::to_string(target.id) + " " + target.name);
			else{
				ClumpStats stats;
				prepareClumpForBake(clump, stats);
				if(validateClumpStats(target, stats, errors)){
					loaded++;
					if(!options.validateOnly){
#ifdef RW_GL3
						captureAvailableFrames(target, clump, captureContext, capturedFrames, ifpLibrary, errors);
#else
						captureAvailableFrames(target, clump, capturedFrames, ifpLibrary, errors);
#endif
					}
					std::ostringstream line;
					line << "target " << target.id << " " << target.name << " atomics " << stats.atomics << " skinned "
					     << stats.skinnedAtomics << " vertices " << stats.vertices << " triangles " << stats.triangles
					     << " materials " << stats.materials << " hierarchy_nodes " << stats.hierarchyNodes;
					reportLines.push_back(line.str());
				}
			}

			if(clump)
				clump->destroy();
			if(txd)
				txd->destroy();
			if(errors.size() > 64)
				break;
		}
		std::printf("Loaded %d ped DFF/TXD pairs through librw.\n", loaded);
		if(!reportLines.empty())
			writeBackendReport(options.outputRoot, reportLines, errors);
	}

	scanRequiredIfps(options.assetRoot, errors);
	createOffscreenDescriptor(256, 256, errors);

	if(!errors.empty()){
		std::fprintf(stderr, "Ped sprite RenderWare backend validation failed:\n");
		for(size_t i = 0; i < errors.size(); i++)
			std::fprintf(stderr, "  %s\n", errors[i].c_str());
		return 1;
	}

	std::printf("RenderWare DFF/TXD/IFP backend inputs validated for %zu ped targets.\n", options.targets.size());
	if(options.validateOnly)
		return 0;

	if(!capturedFrames.empty())
		std::printf("Captured %zu RenderWare animation frame(s).\n", capturedFrames.size());
	if(!WritePedSpriteAtlases(options.outputRoot, options.targets, capturedFrames, errors)){
		std::fprintf(stderr, "Ped sprite atlas generation failed:\n");
		for(size_t i = 0; i < errors.size(); i++)
			std::fprintf(stderr, "  %s\n", errors[i].c_str());
		return 3;
	}
	return 0;
}

#else

int
RunPedSpriteBakeBackend(const PedBakeBackendOptions &options)
{
	(void)options;
	std::fprintf(stderr, "This revc_ped_sprite_baker binary was built without librw backend support. Build from the repo root to enable real DFF/TXD/IFP loading.\n");
	return 3;
}

#endif
