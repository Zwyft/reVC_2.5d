#ifndef __GTA_PEDSPRITERENDERER_H__
#define __GTA_PEDSPRITERENDERER_H__

class CPed;

class CPedSpriteAnimResolver
{
public:
	static const char *ResolveState(CPed *ped);
	static int ResolveDirection(CPed *ped);
};

class CPedSpriteRenderer
{
public:
	static bool Render(CPed *ped);
};

#endif // __GTA_PEDSPRITERENDERER_H__
