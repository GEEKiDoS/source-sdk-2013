//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_FX_SHARED_H
#define L4D2_TERROR_FX_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#ifdef CLIENT_DLL
class C_TerrorPlayer;
#define CTerrorPlayer C_TerrorPlayer
#else
class CTerrorPlayer;
#endif
class CTerrorWeaponInfo;

// soundTime is the scheduled weapon deadline, not the lag-compensation clock.
void FireTerrorBullets( CTerrorPlayer *pPlayer, const Vector &vecOrigin, const QAngle &angles, int nWeaponID, int nMode, int nSeed, float flSoundTime );
#ifndef CLIENT_DLL
void TE_Bullets( int nPlayerIndex, const Vector &vecOrigin, const QAngle &angles, int nWeaponID, int nMode, int nSeed, float flSoundTime );
#endif
struct TerrorBulletShotContext
{
	CTerrorPlayer *m_pShooter;
	const CTerrorWeaponInfo *m_pInfo;
	Vector m_vecOrigin;
	Vector m_vecTracerSource;
	int m_nAmmoType;
	int m_nDamageType;
	float m_flPenetrationPowerSquared;
	float m_flPenetrationMaxDistanceSquared;
	float m_flCharacterPenetrationMaxDistanceSquared;
	bool m_bInitiallyInWater;
	bool m_bCanCutTongue;
	bool m_bSingleBullet;
};

struct TerrorBulletPelletResult
{
	Vector m_vecDirection;
	trace_t m_FirstConsumedTrace;
	trace_t m_LastConsumedTrace;
	Vector m_vecWaterPoint;
	bool m_bNoConsumedImpacts;
	bool m_bHitWater;
	bool m_bHitCharacter;
};

typedef void ( *TerrorTraceImpactCallback )( trace_t &trace, const Vector &direction, float flRangeFromStart, const TerrorBulletShotContext &shot, bool bEntryCombatCharacter, void *pContext );
typedef void ( *TerrorTraceFinishCallback )( const TerrorBulletShotContext &shot, const TerrorBulletPelletResult &pellet, void *pContext );
void TerrorTraceBullet( CTerrorPlayer *pPlayer, const Vector &vecOrigin, const QAngle &angles, const CTerrorWeaponInfo &info, int nSeed, bool bTraceWater,
	TerrorTraceImpactCallback pImpactCallback = NULL, void *pContext = NULL, TerrorTraceImpactCallback pExitCallback = NULL, TerrorTraceFinishCallback pFinishCallback = NULL );
const CTerrorWeaponInfo *GetTerrorWeaponInfo( int nWeaponID );
#ifdef CLIENT_DLL
// Main-approved replacement for native VEngineClient013 slot159 capability.
void TerrorLevelWaterInit( const char *mapName );
void TerrorLevelWaterShutdown();
bool TerrorLevelContainsWater();
// C2F3260: one actual cosmetic ricochet owner for bullets and minigun.
void TerrorRicochet( const trace_t &trace, int damageType );
#endif

#ifndef CLIENT_DLL
class CTongue;
CTongue *CollectTerrorTongueSegments( const Vector &start, const Vector &end, Vector &hitPoint, float &distance );
#endif

#endif // L4D2_TERROR_FX_SHARED_H
