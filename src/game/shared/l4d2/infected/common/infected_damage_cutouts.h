//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef INFECTED_DAMAGE_CUTOUTS_H
#define INFECTED_DAMAGE_CUTOUTS_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "tier1/utlvector.h"

class ConVar;
extern ConVar z_infected_damage_cutouts;

// DamageCutout Type tokens, not damage-info bits.
enum InfectedDamageCutoutFlags_t
{
	INFECTED_CUTOUT_GUNSHOT = 0x01,
	INFECTED_CUTOUT_EXPLOSION = 0x02,
	INFECTED_CUTOUT_DISMEMBER = 0x04,
	INFECTED_CUTOUT_SLASH = 0x08,
	INFECTED_CUTOUT_BLUNT = 0x10,
	INFECTED_CUTOUT_CHAINSAW = 0x20,
	INFECTED_CUTOUT_DECAPITATE = 0x40,
	INFECTED_CUTOUT_HEADSHOT = 0x80,
};

struct InfectedDamageCutoutRegion_t
{
	Vector m_vecCenter;
	Vector m_vecLookAt;
	Vector m_vecUp;
	Vector m_vecScale;
};

struct InfectedDamageCutout_t
{
	InfectedDamageCutout_t();

	int m_iHitbox[5];
	int m_nHitboxCount;
	int m_iExclusion[10];
	int m_nExclusionCount;
	char m_szDeathActivity[128];
	int m_nDamageType;
	int m_nSwingStart;
	int m_nSwingEnd;
	bool m_bBack;
	char m_szGibModel[8][128];
	int m_nGibCount;
	char m_szParticleAttachment[128];
	int m_nParticleAttachmentHitbox;
	char m_szParticleEffect[8][128];
	int m_nParticleWeight[8];
	int m_nTotalParticleWeight;
	int m_nParticleEffectCount;
	bool m_bFatal;
	char m_szModel[128];
	int m_nRegionCount;
	InfectedDamageCutoutRegion_t m_Region[2];
};

struct InfectedDamageCutoutSet
{
	CUtlVector<InfectedDamageCutout_t> m_Cutout;
	CUtlVector<int> m_ExplosionCutout;
};

// Native level-pre loads the numbered sets; nonempty data has DLL lifetime.
const InfectedDamageCutoutSet *GetInfectedDamageCutouts( int nModelType );
int InfectedModelTypeForModel( const char *pszModelName );

#endif // INFECTED_DAMAGE_CUTOUTS_H
