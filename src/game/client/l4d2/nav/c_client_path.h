//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_CLIENT_PATH_H
#define L4D2_C_CLIENT_PATH_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"
class C_TerrorPlayer;
class C_ClientPath : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_ClientPath, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	virtual datamap_t *GetPredDescMap() OVERRIDE;
	C_ClientPath();
	virtual ~C_ClientPath();
	virtual bool ShouldDraw() OVERRIDE;
	virtual int ComputeModelTranslucencyType() OVERRIDE;
	void Draw( C_TerrorPlayer *player );
private:
	float m_flAlpha;
	float m_flSegmentAlpha[256];
	int m_posX[256], m_posY[256], m_posZ[256];
	int m_posCount;
};
void DrawTerrorClientPaths();
#endif // L4D2_C_CLIENT_PATH_H
