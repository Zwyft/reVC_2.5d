#include "common.h"

#include <vector>

#include "AnimBlendAssociation.h"
#include "Camera.h"
#include "Draw.h"
#include "FileMgr.h"
#include "General.h"
#include "ModelInfo.h"
#include "Ped.h"
#include "PedSpriteRenderer.h"
#include "RpAnimBlend.h"
#include "Sprite.h"
#include "Timer.h"
#include "Timecycle.h"
#include "Vehicle.h"
#include "RwHelper.h"
#include "rtpng.h"

#include <sys/stat.h>

static const char *RequiredPedSpriteStates[] = {
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

struct PedSpriteAtlas
{
	char name[32];
	char path[128];
	int32 width;
	int32 height;
	RwTexture *texture;
};

struct PedSpriteModel
{
	int32 id;
	char name[64];
};

struct PedSpriteFrame
{
	int32 model;
	char state[24];
	int32 direction;
	int32 frame;
	int32 durationMs;
	int32 atlas;
	int32 x;
	int32 y;
	int32 w;
	int32 h;
	float pivotX;
	float pivotY;
	float worldHeight;
};

static std::vector<PedSpriteAtlas> gPedSpriteAtlases;
static std::vector<PedSpriteModel> gPedSpriteModels;
static std::vector<PedSpriteFrame> gPedSpriteFrames;
static bool gPedSpritesLoaded;
static bool gPedSpritesValid;
static char gPedSpriteError[256];

static void
SetPedSpriteError(const char *fmt, ...)
{
	if(gPedSpriteError[0] != '\0')
		return;

	va_list va;
	va_start(va, fmt);
	vsnprintf(gPedSpriteError, sizeof(gPedSpriteError), fmt, va);
	va_end(va);
	Error("Ped sprite renderer: %s\n", gPedSpriteError);
}

static int32
FindAtlas(const char *name)
{
	for(size_t i = 0; i < gPedSpriteAtlases.size(); i++)
		if(strcmp(gPedSpriteAtlases[i].name, name) == 0)
			return (int32)i;
	return -1;
}

static bool
ReadManifestLine(char *line)
{
	if(line[0] == '\0' || line[0] == '#')
		return true;

	char tag[16];
	if(sscanf(line, "%15s", tag) != 1)
		return true;

	if(strcmp(tag, "atlas") == 0){
		PedSpriteAtlas atlas;
		memset(&atlas, 0, sizeof(atlas));
		if(sscanf(line, "%*s %31s %127s %d %d", atlas.name, atlas.path, &atlas.width, &atlas.height) != 4){
			SetPedSpriteError("bad atlas manifest line: %s", line);
			return false;
		}
		if(atlas.width <= 0 || atlas.height <= 0){
			SetPedSpriteError("invalid atlas size for %s", atlas.name);
			return false;
		}
		gPedSpriteAtlases.push_back(atlas);
		return true;
	}

	if(strcmp(tag, "model") == 0){
		PedSpriteModel model;
		memset(&model, 0, sizeof(model));
		if(sscanf(line, "%*s %d %63s", &model.id, model.name) != 2){
			SetPedSpriteError("bad model manifest line: %s", line);
			return false;
		}
		gPedSpriteModels.push_back(model);
		return true;
	}

	if(strcmp(tag, "frame") == 0){
		PedSpriteFrame frame;
		char atlasName[32];
		memset(&frame, 0, sizeof(frame));
		if(sscanf(line, "%*s %d %23s %d %d %d %31s %d %d %d %d %f %f %f",
		    &frame.model, frame.state, &frame.direction, &frame.frame, &frame.durationMs,
		    atlasName, &frame.x, &frame.y, &frame.w, &frame.h,
		    &frame.pivotX, &frame.pivotY, &frame.worldHeight) != 13){
			SetPedSpriteError("bad frame manifest line: %s", line);
			return false;
		}
		frame.atlas = FindAtlas(atlasName);
		if(frame.atlas < 0){
			SetPedSpriteError("frame references unknown atlas %s", atlasName);
			return false;
		}
		if(frame.direction < 0 || frame.direction >= 8 || frame.durationMs <= 0 || frame.w <= 0 || frame.h <= 0 || frame.worldHeight <= 0.0f){
			SetPedSpriteError("invalid frame data for model %d state %s direction %d", frame.model, frame.state, frame.direction);
			return false;
		}
		gPedSpriteFrames.push_back(frame);
		return true;
	}

	SetPedSpriteError("unknown manifest tag %s", tag);
	return false;
}

static bool
HasFrameForModelStateDirection(int32 model, const char *state, int32 direction)
{
	for(size_t i = 0; i < gPedSpriteFrames.size(); i++)
		if(gPedSpriteFrames[i].model == model && gPedSpriteFrames[i].direction == direction && strcmp(gPedSpriteFrames[i].state, state) == 0)
			return true;
	return false;
}

static bool
ValidateRuntimeManifestCoverage(void)
{
	if(gPedSpriteModels.empty()){
		SetPedSpriteError("manifest has no model entries");
		return false;
	}
	if(gPedSpriteAtlases.empty() || gPedSpriteFrames.empty()){
		SetPedSpriteError("manifest has no atlases or frames");
		return false;
	}

	for(size_t i = 0; i < gPedSpriteModels.size(); i++){
		for(size_t state = 0; state < ARRAY_SIZE(RequiredPedSpriteStates); state++){
			for(int32 direction = 0; direction < 8; direction++){
				if(!HasFrameForModelStateDirection(gPedSpriteModels[i].id, RequiredPedSpriteStates[state], direction)){
					SetPedSpriteError("missing manifest coverage model=%d name=%s state=%s direction=%d",
					    gPedSpriteModels[i].id, gPedSpriteModels[i].name, RequiredPedSpriteStates[state], direction);
					return false;
				}
			}
		}
	}
	return true;
}

static bool
LoadPedSpriteManifest(void)
{
	if(gPedSpritesLoaded)
		return gPedSpritesValid;
	gPedSpritesLoaded = true;

	int fd = CFileMgr::OpenFile("sprites\\peds\\manifest.txt", "rt");
	if(fd == 0){
		SetPedSpriteError("missing sprites/peds/manifest.txt");
		return false;
	}

	char line[512];
	while(CFileMgr::ReadLine(fd, line, sizeof(line))){
		for(char *p = line; *p; p++)
			if(*p == '\r' || *p == '\n'){
				*p = '\0';
				break;
			}
		if(!ReadManifestLine(line)){
			CFileMgr::CloseFile(fd);
			return false;
		}
	}
	CFileMgr::CloseFile(fd);

	if(!ValidateRuntimeManifestCoverage())
		return false;

	gPedSpritesValid = true;
	return true;
}

static bool
FileExists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

static RwTexture*
LoadAtlasTexture(PedSpriteAtlas &atlas)
{
	if(atlas.texture)
		return atlas.texture;

	char fullPath[256];
	snprintf(fullPath, sizeof(fullPath), "%s%s", CFileMgr::GetRootDirName(), atlas.path);
	for(char *p = fullPath; *p; p++)
		if(*p == '\\')
			*p = '/';
	if(!FileExists(fullPath)){
		SetPedSpriteError("missing atlas PNG %s", fullPath);
		return nil;
	}

	RwImage *image = RtPNGImageRead(fullPath);
	if(image == nil){
		SetPedSpriteError("unreadable atlas PNG %s", fullPath);
		return nil;
	}

	int32 width, height, depth, format;
	RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &width, &height, &depth, &format);
	RwRaster *raster = RwRasterCreate(width, height, depth, format);
	if(raster == nil){
		RwImageDestroy(image);
		SetPedSpriteError("could not create raster for atlas %s", atlas.path);
		return nil;
	}
	RwRasterSetFromImage(raster, image);
	RwImageDestroy(image);

	atlas.texture = RwTextureCreate(raster);
	if(atlas.texture == nil){
		RwRasterDestroy(raster);
		SetPedSpriteError("could not create texture for atlas %s", atlas.path);
		return nil;
	}
	RwTextureSetName(atlas.texture, atlas.name);
	RwTextureSetFilterMode(atlas.texture, rwFILTERNEAREST);
	RwTextureSetAddressing(atlas.texture, rwTEXTUREADDRESSCLAMP);
	return atlas.texture;
}

const char*
CPedSpriteAnimResolver::ResolveState(CPed *ped)
{
	if(ped->m_nPedState == PED_DIE || ped->m_nPedState == PED_DEAD)
		return "death";
	if(ped->m_nPedState == PED_FALL || ped->m_nPedState == PED_STAGGER || ped->m_nPedState == PED_DIVE_AWAY)
		return "hit";
	if(ped->m_nPedState == PED_DRIVING || ped->m_nPedState == PED_PASSENGER || ped->m_nPedState == PED_TAXI_PASSENGER)
		return ped->m_pMyVehicle && ped->m_pMyVehicle->IsBike() ? "bike_ride" : "car_sit";
	if(ped->m_nPedState == PED_ENTER_CAR || ped->m_nPedState == PED_EXIT_CAR || ped->m_nPedState == PED_CARJACK || ped->m_nPedState == PED_DRAG_FROM_CAR)
		return "enter_exit";
	if(ped->m_nWaitState == WAITSTATE_PLAYANIM_DUCK || ped->bIsDucking)
		return "crouch";
	if(ped->m_nPedState == PED_ATTACK || ped->m_nPedState == PED_FIGHT)
		return ped->GetWeapon()->IsTypeMelee() ? "attack" : "firearm";
	if(ped->m_nPedState == PED_AIM_GUN || ped->m_objective == OBJECTIVE_AIM_GUN_AT)
		return "firearm";

	switch(ped->m_nMoveState){
	case PEDMOVE_WALK:
	case PEDMOVE_JOG:
		return "walk";
	case PEDMOVE_RUN:
		return "run";
	case PEDMOVE_SPRINT:
		return "sprint";
	default:
		return "idle";
	}
}

int
CPedSpriteAnimResolver::ResolveDirection(CPed *ped)
{
	float rel = CGeneral::LimitRadianAngle(ped->m_fRotationCur - TheCamera.GetForward().Heading());
	if(rel < 0.0f)
		rel += TWOPI;
	return ((int)Floor((rel + PI / 8.0f) / (PI / 4.0f))) & 7;
}

static float
GetPedSpriteAnimProgress(CPed *ped)
{
	if(ped->m_rwObject == nil || RwObjectGetType(ped->m_rwObject) != rpCLUMP)
		return -1.0f;

	CAnimBlendAssociation *assoc = RpAnimBlendClumpGetMainAssociation(ped->GetClump(), nil, nil);
	if(assoc == nil || assoc->hierarchy == nil || assoc->hierarchy->totalLength <= 0.0f)
		return -1.0f;

	return Clamp(assoc->currentTime / assoc->hierarchy->totalLength, 0.0f, 0.9999f);
}

static const PedSpriteFrame*
FindFrame(CPed *ped, const char *state, int32 direction)
{
	int32 model = ped->GetModelIndex();
	int32 totalDuration = 0;
	for(size_t i = 0; i < gPedSpriteFrames.size(); i++)
		if(gPedSpriteFrames[i].model == model && gPedSpriteFrames[i].direction == direction && strcmp(gPedSpriteFrames[i].state, state) == 0)
			totalDuration += gPedSpriteFrames[i].durationMs;

	if(totalDuration <= 0)
		return nil;

	float progress = GetPedSpriteAnimProgress(ped);
	int32 t;
	if(progress >= 0.0f)
		t = Min((int32)Floor(progress * totalDuration), totalDuration - 1);
	else
		t = CTimer::GetTimeInMilliseconds() % totalDuration;

	for(size_t i = 0; i < gPedSpriteFrames.size(); i++){
		const PedSpriteFrame &frame = gPedSpriteFrames[i];
		if(frame.model != model || frame.direction != direction || strcmp(frame.state, state) != 0)
			continue;
		if(t < frame.durationMs)
			return &frame;
		t -= frame.durationMs;
	}
	return nil;
}


static void
RenderSolidScreenQuad(float left, float top, float right, float bottom, float z, float recipz, const CRGBA &color)
{
	float screenz = CSprite::GetNearScreenZ() +
		(z - CDraw::GetNearClipZ()) * (CSprite::GetFarScreenZ() - CSprite::GetNearScreenZ()) * CDraw::GetFarClipZ() /
		((CDraw::GetFarClipZ() - CDraw::GetNearClipZ()) * z);

	RwIm2DVertex verts[4];
	RwIm2DVertexSetScreenX(&verts[0], left);
	RwIm2DVertexSetScreenY(&verts[0], top);
	RwIm2DVertexSetScreenZ(&verts[0], screenz);
	RwIm2DVertexSetCameraZ(&verts[0], z);
	RwIm2DVertexSetRecipCameraZ(&verts[0], recipz);
	RwIm2DVertexSetIntRGBA(&verts[0], color.r, color.g, color.b, color.a);

	RwIm2DVertexSetScreenX(&verts[1], right);
	RwIm2DVertexSetScreenY(&verts[1], top);
	RwIm2DVertexSetScreenZ(&verts[1], screenz);
	RwIm2DVertexSetCameraZ(&verts[1], z);
	RwIm2DVertexSetRecipCameraZ(&verts[1], recipz);
	RwIm2DVertexSetIntRGBA(&verts[1], color.r, color.g, color.b, color.a);

	RwIm2DVertexSetScreenX(&verts[2], right);
	RwIm2DVertexSetScreenY(&verts[2], bottom);
	RwIm2DVertexSetScreenZ(&verts[2], screenz);
	RwIm2DVertexSetCameraZ(&verts[2], z);
	RwIm2DVertexSetRecipCameraZ(&verts[2], recipz);
	RwIm2DVertexSetIntRGBA(&verts[2], color.r, color.g, color.b, color.a);

	RwIm2DVertexSetScreenX(&verts[3], left);
	RwIm2DVertexSetScreenY(&verts[3], bottom);
	RwIm2DVertexSetScreenZ(&verts[3], screenz);
	RwIm2DVertexSetCameraZ(&verts[3], z);
	RwIm2DVertexSetRecipCameraZ(&verts[3], recipz);
	RwIm2DVertexSetIntRGBA(&verts[3], color.r, color.g, color.b, color.a);

	RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
}

static bool
RenderMissingPedSpriteMarker(CPed *ped)
{
	CVector spriteBase = ped->GetPosition();
	spriteBase.z += 1.0f;
	RwV3d screenBase;
	float screenW, screenH;
	if(!CSprite::CalcScreenCoors(spriteBase, &screenBase, &screenW, &screenH, true))
		return true;

	float size = Max(6.0f, Min(screenH * 0.35f, 28.0f));
	float recipz = 1.0f / screenBase.z;
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RenderSolidScreenQuad(screenBase.x - size, screenBase.y - size, screenBase.x + size, screenBase.y + size,
	    screenBase.z, recipz, CRGBA(220, 24, 24, 220));
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	return true;
}

static void
RenderAtlasFrame(float x, float y, float z, float w, float h, float recipz, const PedSpriteAtlas &atlas, const PedSpriteFrame &frame, const CRGBA &tint)
{
	float u0 = (float)frame.x / (float)atlas.width;
	float v0 = (float)frame.y / (float)atlas.height;
	float u1 = (float)(frame.x + frame.w) / (float)atlas.width;
	float v1 = (float)(frame.y + frame.h) / (float)atlas.height;

	float left = x - w * frame.pivotX;
	float right = left + w;
	float top = y - h * frame.pivotY;
	float bottom = top + h;

	if(right < 0.0f || left > SCREEN_WIDTH || bottom < 0.0f || top > SCREEN_HEIGHT)
		return;

	float screenz = CSprite::GetNearScreenZ() +
		(z - CDraw::GetNearClipZ()) * (CSprite::GetFarScreenZ() - CSprite::GetNearScreenZ()) * CDraw::GetFarClipZ() /
		((CDraw::GetFarClipZ() - CDraw::GetNearClipZ()) * z);

	RwIm2DVertex verts[4];
	RwIm2DVertexSetScreenX(&verts[0], left);
	RwIm2DVertexSetScreenY(&verts[0], top);
	RwIm2DVertexSetScreenZ(&verts[0], screenz);
	RwIm2DVertexSetCameraZ(&verts[0], z);
	RwIm2DVertexSetRecipCameraZ(&verts[0], recipz);
	RwIm2DVertexSetIntRGBA(&verts[0], tint.r, tint.g, tint.b, tint.a);
	RwIm2DVertexSetU(&verts[0], u0, recipz);
	RwIm2DVertexSetV(&verts[0], v0, recipz);

	RwIm2DVertexSetScreenX(&verts[1], right);
	RwIm2DVertexSetScreenY(&verts[1], top);
	RwIm2DVertexSetScreenZ(&verts[1], screenz);
	RwIm2DVertexSetCameraZ(&verts[1], z);
	RwIm2DVertexSetRecipCameraZ(&verts[1], recipz);
	RwIm2DVertexSetIntRGBA(&verts[1], tint.r, tint.g, tint.b, tint.a);
	RwIm2DVertexSetU(&verts[1], u1, recipz);
	RwIm2DVertexSetV(&verts[1], v0, recipz);

	RwIm2DVertexSetScreenX(&verts[2], right);
	RwIm2DVertexSetScreenY(&verts[2], bottom);
	RwIm2DVertexSetScreenZ(&verts[2], screenz);
	RwIm2DVertexSetCameraZ(&verts[2], z);
	RwIm2DVertexSetRecipCameraZ(&verts[2], recipz);
	RwIm2DVertexSetIntRGBA(&verts[2], tint.r, tint.g, tint.b, tint.a);
	RwIm2DVertexSetU(&verts[2], u1, recipz);
	RwIm2DVertexSetV(&verts[2], v1, recipz);

	RwIm2DVertexSetScreenX(&verts[3], left);
	RwIm2DVertexSetScreenY(&verts[3], bottom);
	RwIm2DVertexSetScreenZ(&verts[3], screenz);
	RwIm2DVertexSetCameraZ(&verts[3], z);
	RwIm2DVertexSetRecipCameraZ(&verts[3], recipz);
	RwIm2DVertexSetIntRGBA(&verts[3], tint.r, tint.g, tint.b, tint.a);
	RwIm2DVertexSetU(&verts[3], u0, recipz);
	RwIm2DVertexSetV(&verts[3], v1, recipz);

	RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
}

bool
CPedSpriteRenderer::Render(CPed *ped)
{
	if(!LoadPedSpriteManifest())
		return RenderMissingPedSpriteMarker(ped);

	const char *state = CPedSpriteAnimResolver::ResolveState(ped);
	int32 direction = CPedSpriteAnimResolver::ResolveDirection(ped);
	const PedSpriteFrame *frame = FindFrame(ped, state, direction);
	if(frame == nil){
		SetPedSpriteError("missing frame model=%d state=%s direction=%d", ped->GetModelIndex(), state, direction);
		return RenderMissingPedSpriteMarker(ped);
	}

	PedSpriteAtlas &atlas = gPedSpriteAtlases[frame->atlas];
	RwTexture *texture = LoadAtlasTexture(atlas);
	if(texture == nil)
		return RenderMissingPedSpriteMarker(ped);

	CVector spriteBase = ped->GetPosition();
	RwV3d screenBase;
	float screenW, screenH;
	if(!CSprite::CalcScreenCoors(spriteBase, &screenBase, &screenW, &screenH, true))
		return true;

	float height = screenH * frame->worldHeight;
	float width = height * ((float)frame->w / (float)frame->h);
	float recipz = 1.0f / screenBase.z;

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(texture));
	SetCullMode(rwCULLMODECULLNONE);

	uint8 brightness = (uint8)Clamp(CTimeCycle::GetSpriteBrightness(), 96.0f, 255.0f);
	CRGBA tint(brightness, brightness, brightness, 255);
	RenderAtlasFrame(screenBase.x, screenBase.y, screenBase.z, width, height, recipz, atlas, *frame, tint);

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);

	return true;
}
