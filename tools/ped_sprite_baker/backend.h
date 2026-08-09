#pragma once

#include <string>
#include <vector>

struct PedBakeTarget
{
	int id;
	std::string name;
	std::string source;
};

struct PedBakeBackendOptions
{
	std::string assetRoot;
	std::string outputRoot;
	std::vector<PedBakeTarget> targets;
	bool validateOnly;
};

int RunPedSpriteBakeBackend(const PedBakeBackendOptions &options);
