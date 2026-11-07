//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_ENTITIES_H
#define TERROR_GLOW_ENTITIES_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseflex.h"

class C_TerrorPlayer;

class C_SurvivorRescue : public C_BaseFlex
{
public:
	DECLARE_CLASS( C_SurvivorRescue, C_BaseFlex );
	DECLARE_CLIENTCLASS();
	C_SurvivorRescue();
	virtual ~C_SurvivorRescue();
	static C_SurvivorRescue *FindForPlayer( const C_TerrorPlayer *pPlayer );
private:
	CHandle< C_TerrorPlayer > m_survivor;
};

C_BaseEntity *TerrorGlow_FindRescueModel( const C_TerrorPlayer *pPlayer );


#endif // TERROR_GLOW_ENTITIES_H
