#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "backend.h"

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

static const PedBakeTarget BuiltInPedTargets[] = {
	{0, "player", "builtin"},
	{1, "cop", "builtin"},
	{2, "swat", "builtin"},
	{3, "fbi", "builtin"},
	{4, "army", "builtin"},
	{5, "medic", "builtin"},
	{6, "fireman", "builtin"},
	{7, "male01", "builtin"},
	{9, "hfyst", "builtin"},
	{10, "hfost", "builtin"},
	{11, "hmyst", "builtin"},
	{12, "hmost", "builtin"},
	{13, "hfyri", "builtin"},
	{14, "hfori", "builtin"},
	{15, "hmyri", "builtin"},
	{16, "hmori", "builtin"},
	{17, "hfybe", "builtin"},
	{18, "hfobe", "builtin"},
	{19, "hmybe", "builtin"},
	{20, "hmobe", "builtin"},
	{21, "hfybu", "builtin"},
	{22, "hfymd", "builtin"},
	{23, "hfycg", "builtin"},
	{24, "hfypr", "builtin"},
	{25, "hfotr", "builtin"},
	{26, "hmotr", "builtin"},
	{27, "hmyap", "builtin"},
	{28, "hmoca", "builtin"},
	{29, "bmodk", "builtin"},
	{30, "bmykr", "builtin"},
	{31, "bfyst", "builtin"},
	{32, "bfost", "builtin"},
	{33, "bmyst", "builtin"},
	{34, "bmost", "builtin"},
	{35, "bfyri", "builtin"},
	{36, "bfori", "builtin"},
	{37, "bmyri", "builtin"},
	{38, "bfybe", "builtin"},
	{39, "bmybe", "builtin"},
	{40, "bfobe", "builtin"},
	{41, "bmobe", "builtin"},
	{42, "bmybu", "builtin"},
	{43, "bfypr", "builtin"},
	{44, "bfotr", "builtin"},
	{45, "bmotr", "builtin"},
	{46, "bmypi", "builtin"},
	{47, "bmybb", "builtin"},
	{48, "wmycr", "builtin"},
	{49, "wfyst", "builtin"},
	{50, "wfost", "builtin"},
	{51, "wmyst", "builtin"},
	{52, "wmost", "builtin"},
	{53, "wfyri", "builtin"},
	{54, "wfori", "builtin"},
	{55, "wmyri", "builtin"},
	{56, "wmori", "builtin"},
	{57, "wfybe", "builtin"},
	{58, "wmybe", "builtin"},
	{59, "wfobe", "builtin"},
	{60, "wmobe", "builtin"},
	{61, "wmycw", "builtin"},
	{62, "wmygo", "builtin"},
	{63, "wfogo", "builtin"},
	{64, "wmogo", "builtin"},
	{65, "wfylg", "builtin"},
	{66, "wmylg", "builtin"},
	{67, "wfybu", "builtin"},
	{68, "wmybu", "builtin"},
	{69, "wmobu", "builtin"},
	{70, "wfypr", "builtin"},
	{71, "wfotr", "builtin"},
	{72, "wmotr", "builtin"},
	{73, "wmypi", "builtin"},
	{74, "wmoca", "builtin"},
	{75, "wfyjg", "builtin"},
	{76, "wmyjg", "builtin"},
	{77, "wfysk", "builtin"},
	{78, "wmysk", "builtin"},
	{79, "wfysh", "builtin"},
	{80, "wfosh", "builtin"},
	{81, "jfoto", "builtin"},
	{82, "jmoto", "builtin"},
	{83, "cba", "builtin"},
	{84, "cbb", "builtin"},
	{85, "hna", "builtin"},
	{86, "hnb", "builtin"},
	{87, "sga", "builtin"},
	{88, "sgb", "builtin"},
	{89, "cla", "builtin"},
	{90, "clb", "builtin"},
	{91, "gda", "builtin"},
	{92, "gdb", "builtin"},
	{93, "bka", "builtin"},
	{94, "bkb", "builtin"},
	{95, "pga", "builtin"},
	{96, "pgb", "builtin"},
	{97, "vice1", "builtin"},
	{98, "vice2", "builtin"},
	{99, "vice3", "builtin"},
	{100, "vice4", "builtin"},
	{101, "vice5", "builtin"},
	{102, "vice6", "builtin"},
	{103, "vice7", "builtin"},
	{104, "vice8", "builtin"},
	{105, "wfyg1", "builtin"},
	{106, "wfyg2", "builtin"},
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

static std::string
lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
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

static bool
makeDir(const std::string &path)
{
	if(path.empty() || isDir(path))
		return true;
	if(mkdir(path.c_str(), 0775) == 0 || errno == EEXIST)
		return true;
	return false;
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

static std::string
stripComment(const std::string &line)
{
	size_t hash = line.find('#');
	size_t slash = line.find("//");
	size_t cut = std::string::npos;
	if(hash != std::string::npos)
		cut = hash;
	if(slash != std::string::npos)
		cut = cut == std::string::npos ? slash : std::min(cut, slash);
	return trim(cut == std::string::npos ? line : line.substr(0, cut));
}

static std::vector<std::string>
splitFields(std::string line)
{
	for(size_t i = 0; i < line.size(); i++)
		if(line[i] == ',' || std::isspace((unsigned char)line[i]))
			line[i] = ' ';
	std::istringstream in(line);
	std::vector<std::string> fields;
	std::string field;
	while(in >> field)
		fields.push_back(field);
	return fields;
}

static bool
parseInt(const std::string &s, int &out)
{
	char *end = NULL;
	long value = std::strtol(s.c_str(), &end, 0);
	if(end == s.c_str() || *end != '\0')
		return false;
	out = (int)value;
	return true;
}

static std::string
normalizeDataPath(std::string path)
{
	path = trim(path);
	if(path.size() >= 2 && ((path[0] == '"' && path[path.size() - 1] == '"') || (path[0] == '\'' && path[path.size() - 1] == '\'')))
		path = path.substr(1, path.size() - 2);
	size_t version = path.find(';');
	if(version != std::string::npos)
		path = path.substr(0, version);
	while(!path.empty() && (path[0] == '/' || path[0] == '\\'))
		path.erase(path.begin());
	for(size_t i = 0; i < path.size(); i++)
		if(path[i] == '\\')
			path[i] = '/';
	return path;
}

static void
addTarget(std::map<int, PedBakeTarget> &targets, int id, const std::string &name, const std::string &source)
{
	if(id < 0)
		return;
	PedBakeTarget target;
	target.id = id;
	target.name = lower(name);
	target.source = source;
	std::map<int, PedBakeTarget>::iterator it = targets.find(id);
	if(it == targets.end() || it->second.source == "builtin")
		targets[id] = target;
}

static void
addBuiltInTargets(std::map<int, PedBakeTarget> &targets)
{
	for(size_t i = 0; i < sizeof(BuiltInPedTargets) / sizeof(BuiltInPedTargets[0]); i++)
		addTarget(targets, BuiltInPedTargets[i].id, BuiltInPedTargets[i].name, BuiltInPedTargets[i].source);
}

static void
parseIdePeds(const std::string &assetRoot, const std::string &idePath, std::map<int, PedBakeTarget> &targets)
{
	std::ifstream f(joinPath(assetRoot, idePath).c_str());
	if(!f)
		return;

	bool inPeds = false;
	std::string line;
	while(std::getline(f, line)){
		line = stripComment(line);
		if(line.empty())
			continue;
		std::string section = lower(line);
		if(!inPeds){
			if(section == "peds")
				inPeds = true;
			continue;
		}
		if(section == "end"){
			inPeds = false;
			continue;
		}

		std::vector<std::string> fields = splitFields(line);
		int id;
		if(fields.size() >= 2 && parseInt(fields[0], id))
			addTarget(targets, id, fields[1], "ide:" + idePath);
	}
}

static void
parseLevelForIdeFiles(const std::string &assetRoot, const std::string &levelPath, std::map<int, PedBakeTarget> &targets)
{
	std::ifstream f(joinPath(assetRoot, levelPath).c_str());
	if(!f)
		return;

	std::string line;
	while(std::getline(f, line)){
		line = stripComment(line);
		if(line.empty())
			continue;
		std::vector<std::string> fields = splitFields(line);
		if(fields.size() < 2)
			continue;
		if(lower(fields[0]) != "ide")
			continue;
		std::string idePath = normalizeDataPath(fields[1]);
		if(!idePath.empty())
			parseIdePeds(assetRoot, idePath, targets);
	}
}

static void
parseSpecialTargets(const std::string &assetRoot, std::map<int, PedBakeTarget> &targets)
{
	std::ifstream f(joinPath(assetRoot, "DATA/SPECIAL.TXT").c_str());
	if(!f)
		return;

	std::string line;
	int lineId = 0;
	while(lineId < 21 && std::getline(f, line)){
		line = stripComment(line);
		if(line.empty()){
			lineId++;
			continue;
		}
		std::vector<std::string> fields = splitFields(line);
		if(!fields.empty())
			addTarget(targets, 109 + lineId, fields[0], "special:DATA/SPECIAL.TXT");
		lineId++;
	}
}

static std::vector<PedBakeTarget>
discoverPedBakeTargets(const std::string &assetRoot)
{
	std::map<int, PedBakeTarget> targets;
	addBuiltInTargets(targets);
	parseLevelForIdeFiles(assetRoot, "DATA/DEFAULT.DAT", targets);
	parseLevelForIdeFiles(assetRoot, "DATA/ANIMVIEWER.DAT", targets);
	parseSpecialTargets(assetRoot, targets);

	std::vector<PedBakeTarget> result;
	for(std::map<int, PedBakeTarget>::const_iterator it = targets.begin(); it != targets.end(); ++it)
		result.push_back(it->second);
	return result;
}

static bool
writeBakePlan(const std::string &assetRoot, const std::string &outputRoot, const std::vector<PedBakeTarget> &targets)
{
	std::string pedDir = joinPath(joinPath(outputRoot, "sprites"), "peds");
	if(!makeDirs(pedDir)){
		std::fprintf(stderr, "Could not create %s\n", pedDir.c_str());
		return false;
	}

	std::string planPath = joinPath(pedDir, "bake-plan.txt");
	std::ofstream out(planPath.c_str());
	if(!out){
		std::fprintf(stderr, "Could not write %s\n", planPath.c_str());
		return false;
	}

	out << "# reVC ped sprite bake plan v1\n";
	out << "# Generated from owned local game data. This is not a sprite atlas.\n";
	out << "asset_root " << assetRoot << "\n";
	out << "target_count " << targets.size() << "\n";
	for(size_t i = 0; i < targets.size(); i++)
		out << "target " << targets[i].id << " " << targets[i].name << " " << targets[i].source << "\n";
	for(size_t state = 0; state < sizeof(RequiredStates) / sizeof(RequiredStates[0]); state++)
		out << "state " << RequiredStates[state] << " directions 8 height_px 128\n";

	std::printf("Ped bake plan written: %s\n", planPath.c_str());
	std::printf("Discovered %zu gameplay-visible ped targets.\n", targets.size());
	return true;
}

struct ManifestCoverage
{
	std::set<int> models;
	std::map<int, std::string> modelNames;
	std::map<std::string, std::string> atlasPaths;
	std::set<std::string> frameKeys;
};

static std::string
frameKey(int model, const std::string &state, int direction)
{
	return std::to_string(model) + ":" + state + ":" + std::to_string(direction);
}

static bool
parseManifest(const std::string &outputRoot, ManifestCoverage &coverage, std::vector<std::string> &errors)
{
	std::string manifestPath = joinPath(outputRoot, "sprites/peds/manifest.txt");
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
			coverage.atlasPaths[atlasName] = path;
			if(!exists(joinPath(outputRoot, path)))
				errors.push_back("Missing atlas PNG referenced on line " + std::to_string(lineNo) + ": " + joinPath(outputRoot, path));
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
			if(coverage.atlasPaths.find(atlasName) == coverage.atlasPaths.end())
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
	parseManifest(outputRoot, coverage, errors);

	if(coverage.models.empty())
		errors.push_back("Manifest has no model/frame entries");
	if(coverage.atlasPaths.empty())
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
	    coverage.models.size(), coverage.atlasPaths.size(), coverage.frameKeys.size());
	return true;
}

static void
usage(const char *argv0)
{
	std::fprintf(stderr,
		"usage: %s [--validate-output] [--emit-bake-plan] [--validate-rw-assets] --asset-root <owned GTA VC root> --output <generated sprite output>\n"
		"\n"
		"The output contract is sprites/peds/manifest.txt plus PNG atlases referenced by that manifest.\n"
		"This tool validates the user-owned asset root before any bake work starts.\n"
		"--emit-bake-plan writes sprites/peds/bake-plan.txt with discovered ped/special targets and exits before rendering.\n"
		"--validate-rw-assets loads discovered DFF/TXD inputs through librw and scans required IFP packages.\n"
		"Config fallback: vc-assets.local.properties with asset.root and sprite.output.\n",
		argv0);
}

int
main(int argc, char **argv)
{
	std::string assetRoot;
	std::string outputRoot;
	bool validateOutputOnly = false;
	bool emitBakePlanOnly = false;
	bool validateRwAssetsOnly = false;
	for(int i = 1; i < argc; i++){
		if(std::strcmp(argv[i], "--asset-root") == 0 && i + 1 < argc)
			assetRoot = argv[++i];
		else if(std::strcmp(argv[i], "--output") == 0 && i + 1 < argc)
			outputRoot = argv[++i];
		else if(std::strcmp(argv[i], "--validate-output") == 0)
			validateOutputOnly = true;
		else if(std::strcmp(argv[i], "--emit-bake-plan") == 0)
			emitBakePlanOnly = true;
		else if(std::strcmp(argv[i], "--validate-rw-assets") == 0)
			validateRwAssetsOnly = true;
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

	std::vector<PedBakeTarget> targets = discoverPedBakeTargets(assetRoot);
	std::printf("Asset root validated: %s\n", assetRoot.c_str());
	std::printf("Sprite output root: %s\n", outputRoot.c_str());
	if(!writeBakePlan(assetRoot, outputRoot, targets))
		return 1;

	if(emitBakePlanOnly)
		return 0;

	PedBakeBackendOptions backendOptions;
	backendOptions.assetRoot = assetRoot;
	backendOptions.outputRoot = outputRoot;
	backendOptions.targets = targets;
	backendOptions.validateOnly = validateRwAssetsOnly;
	return RunPedSpriteBakeBackend(backendOptions);
}
