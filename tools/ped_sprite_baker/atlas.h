#pragma once

#include "backend.h"

#include <string>
#include <vector>

struct PedCapturedFrame
{
	int modelId;
	std::string modelName;
	std::string state;
	int direction;
	int frameIndex;
	int durationMs;
	int width;
	int height;
	float pivotX;
	float pivotY;
	float worldHeight;
	std::vector<unsigned char> rgba;
};

bool WritePedSpriteAtlases(const std::string &outputRoot, const std::vector<PedBakeTarget> &targets,
    const std::vector<PedCapturedFrame> &frames, std::vector<std::string> &errors);
