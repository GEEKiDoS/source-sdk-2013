//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ZOMBIEMANAGER_H
#define L4D2_ZOMBIEMANAGER_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"
#include "NextBot/NextBot.h"
#include "l4d2/mem_zero_on_new.h"

class TerrorNavArea;
class CTerrorPlayer;
class Infected;
class Witch;

// Original ZombieManager (server global 1085D954, allocation 35C700,
// constructor 35B840). This is neither CDirector nor CDirectorItemManager.
class ZombieManager : public CMemZeroOnNew
{
public:
	enum SpawnLocation
	{
		SPAWN_ANYWHERE = 0,
		SPAWN_BEHIND_SURVIVORS,
		SPAWN_NEAR_IT,
		SPAWN_SPECIALS_IN_FRONT,
		SPAWN_SPECIALS_ANYWHERE,
		SPAWN_FAR_AWAY,
		SPAWN_ABOVE_SURVIVORS,
		SPAWN_IN_FRONT,
		SPAWN_FINALE,
		SPAWN_LARGE_VOLUME,
		SPAWN_PREFERRED_POSITION,
		SPAWN_LOCATION_COUNT
	};
	enum CommonSpawnType
	{
		WANDERER = 0,
		MOB,
		AMBIENT_MOB
	};

	ZombieManager();
	virtual ~ZombieManager();
	void OnMapStart();
	void OnRoundStart();
	void StartFrame();
	void DisableSpawning() { m_bCanSpawn = false; }
	void OnNavAreaDestroyed( TerrorNavArea *area );
	void OnSpawnAreasChanged() { m_bSpawnAreasChanged = true; }
	void ClearSpawnAreaCollections();
	void CollectMapSpawnAreas();
	void OnFlowDistancesInitialized( TerrorNavArea *startArea );
	void OnFlowDistancesRecomputed( TerrorNavArea *startArea );

	float GetZombieSpawnRange() const;
	void OnAreaLeftActiveSet( TerrorNavArea *area );
	Infected *SpawnCommonZombie( TerrorNavArea *area, const Vector &position,
		CommonSpawnType type = WANDERER );
	void SpawnMob( int count );
	void SpawnITMob( int count );
	bool IsMobSpawnNeeded() const;
	void RequestDelayedMegaMob();
	void ClearPendingMobCount();
	void SetPendingMobCount( int count );
	int GetPendingMobCount() const { return m_nPendingMobCount; }
	void ResetPopulationCounter() { m_nCommonSpawnCount = 0; }
	int GetPopulationCounter() const { return m_nCommonSpawnCount; }
	int GetMobSpawnAreaCount() const { return m_mobSpawnAreas.Count(); }
	TerrorNavArea *GetMobSpawnArea( int index ) const { return m_mobSpawnAreas[index].m_area; }
	CommonSpawnType GetCurrentCommonSpawnType() const { return m_commonSpawnType; }
	void ResetPanicSpecialSpawnCounts()
	{
		for ( int i = 1; i < 7; ++i )
			m_nSpecialSpawnCount[i] = 0;
		m_nTotalSpecialSpawns = 0;
	}
	int GetPanicSpecialSpawnCount( int zombieClass ) const { return m_nSpecialSpawnCount[zombieClass]; }
	int GetTotalPanicSpecialSpawns() const { return m_nTotalSpecialSpawns; }
	void SetSpawnCompassMask( unsigned int mask ) { m_nSpawnCompassMask = mask; }

	const CUtlVector< TerrorNavArea * > &CollectSpawnAreas( SpawnLocation location, int zombieClass );
	void SetPreferredMobSpawnPosition( const Vector &position, float range );
	void CollectScriptedSpawnAreas( bool clear, const Vector &position, float radius );
	bool CanZombieSpawnHere( const Vector &position, TerrorNavArea *area,
		int zombieClass, bool mob, CTerrorPlayer *player = NULL );
	bool GetRandomPZSpawnPosition( int zombieClass, int tries,
		CTerrorPlayer *player, Vector *position );
	Witch *SpawnWitch( const Vector &position, const QAngle &angles );
	Witch *SpawnWitchBride( const Vector &position, const QAngle &angles );
	Witch *SpawnWitchInArea( TerrorNavArea *area, const QAngle &angles, bool centerFirst );
	CTerrorPlayer *SpawnTank( const Vector &position, const QAngle &angles );
	CTerrorPlayer *SpawnTankInArea( TerrorNavArea *area, const QAngle &angles, bool centerFirst );
	CTerrorPlayer *SpawnSpecial( int zombieClass, const Vector &position, const QAngle &angles );
	CTerrorPlayer *SpawnSpecialInArea( int zombieClass, TerrorNavArea *area, const QAngle &angles );
	bool IsUsingSpecialSpawnPosition() const { return m_bUseSpecialSpawnPosition; }
	const Vector &GetSpecialSpawnPosition() const { return m_specialSpawnPosition; }
	void SetSpecialSpawnPosition( const Vector &position );
	void ClearSpecialSpawnPosition() { m_bUseSpecialSpawnPosition = false; }
	bool IsTransferringTank() const { return m_bTransferringTank; }
	bool TransferTank( CTerrorPlayer *oldTank, CTerrorPlayer *newTank );
	bool RelocateOrCullZombie( CTerrorPlayer *player );

	bool CanSpawnFallenSurvivor() const;
	void OnFallenSurvivorSpawned() { ++m_nFallenSurvivorCount; }
	void OnFallenSurvivorRemoved( bool killed );
	bool TrySelectJimmyGibbs();
	void StartJimmyGibbsSpawnTimer();
	void StartDelayedMegaMobTimer();

private:
	struct MobSpawnArea
	{
		TerrorNavArea *m_area;
		int m_population;
	};
	void AccumulateSpawnAreaCollection( const CUtlVector< TerrorNavArea * > &input,
		SpawnLocation location, int zombieClass, float threshold,
		CUtlVector< TerrorNavArea * > *result, bool draw, bool checkZombieBorders );
	void CollectActiveSpawnAreas( SpawnLocation location, int zombieClass,
		float threshold, CUtlVector< TerrorNavArea * > *result, bool draw );
	void ChooseMobSpawnAreas();
	TerrorNavArea *ChooseAdjacentMobSpawnArea( TerrorNavArea *area );
	void DrawSpawnableAreas();

	bool m_bCanSpawn;
	bool m_bTransferringTank;
	int m_nFallenSurvivorCount;
	CountdownTimer m_fallenSurvivorSuppressionTimer;
	bool m_bJimmyGibbsSpawned;
	CountdownTimer m_jimmyGibbsSpawnTimer;
	bool m_bJimmyGibbsEligible;
	EHANDLE m_hJimmyGibbs;
	CountdownTimer m_delayedMegaMobTimer;
	int m_nSpecialSpawnCount[7];
	int m_nTotalSpecialSpawns;
	unsigned int m_nSpawnCompassMask;
	CUtlVector< TerrorNavArea * > m_spawnAreaCollections[SPAWN_LOCATION_COUNT];
	CountdownTimer m_spawnAreaCollectionTimer[SPAWN_LOCATION_COUNT];
	CUtlVector< TerrorNavArea * > m_recentMobSpawnAreas;
	CUtlVector< MobSpawnArea > m_mobSpawnAreas;
	CountdownTimer m_mobSpawnAreaTimer;
	CountdownTimer m_mobSpawnTimer;
	int m_nPendingMobCount;
	SpawnLocation m_mobSpawnLocation;
	Vector m_preferredMobPosition;
	float m_flPreferredMobPositionRange;
	CountdownTimer m_ambientMobTimer;
	int m_nAmbientMobCount;
	TerrorNavArea *m_ambientMobArea;
	CUtlVector< TerrorNavArea * > m_finaleSpawnAreas;
	CUtlVector< TerrorNavArea * > m_battlefieldSpawnAreas;
	CUtlVector< TerrorNavArea * > m_scriptedSpawnAreas;
	int m_nCommonSpawnCount;
	Vector m_specialSpawnPosition;
	bool m_bUseSpecialSpawnPosition;
	bool m_bSpawnAreasChanged;
	CommonSpawnType m_commonSpawnType;
};

extern ZombieManager *TheZombieManager;
void CreateZombieManager();
void DestroyZombieManager();
extern bool g_bZombieSpawnBypass;
extern bool g_bIgnoreZombieSpawnRules;
extern bool g_bZombieSpawnPositionOverride;
extern Vector g_vecZombieSpawnPosition;
extern bool g_bZombieSpawnAngleOverride;
extern float g_flZombieSpawnAngle;
extern bool g_bZombieSpawnIsMob;
extern int g_iNextZombieClass; // Native1085D950, sole definition in terrorplayer_replacement.cpp.

CBaseCombatCharacter *FindClosestCharacterWhoIsIT( CBaseCombatCharacter *entity ); // Native290150, ONE shared visitor.
// Native 2901B0 selector: 0=all non-retired commons, 1=INTENSE body arousal,
// 2=not INTENSE. Dying actors remain counted until EFL_KILLME.
int GetCommonInfectedCount( int arousal = 0 );

#endif
