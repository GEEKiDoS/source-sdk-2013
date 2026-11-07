//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_NAV_WORLD_H
#define TERROR_NAV_WORLD_H
#ifdef _WIN32
#pragma once
#endif

#include "nav.h"
#include "ehandle.h"
#include "tier1/utlvector.h"

class CNavArea;
class TerrorNavArea;
class CBasePropDoor;
class TerrorNavMesh;

// A connected CHECKPOINT region owned by TerrorNavMesh, not a second mesh or
// per-player safe-area latch. Geometry/links remain the native CNavArea data.
class CTerrorCheckpoint
{
public:
	CTerrorCheckpoint();
	void AddArea( TerrorNavArea *area );
	void BindToWorld();
	void OnAreaRemoved( TerrorNavArea *area );
	bool IsOutward() const { return m_isOutward; }
	bool AreAllDoorsClosed() const;
	bool HasLockedDoor() const;
	CBasePropDoor *GetOpenDoor() const;
	const Extent &GetBounds() const { return m_bounds; }
	Vector GetCenter() const { return ( m_bounds.lo + m_bounds.hi ) * 0.5f; }
	TerrorNavArea *GetLargestSpawnArea() const;
	TerrorNavArea *GetFarthestSpawnArea( const Vector &position ) const;
	bool GetSpawnPosition( Vector *origin, QAngle *angles, TerrorNavArea **area ) const;
	void CloseAndLockDoors();
	void RespawnPlayers();
	const CUtlVector< TerrorNavArea * > &GetAreas() const { return m_areas; }
private:
	friend class TerrorNavMesh;
	CUtlVector< TerrorNavArea * > m_areas;
	float m_totalArea;
	Extent m_bounds;
	CUtlVector< TerrorNavArea * > m_spawnAreas;
	float m_spawnAreaSize;
	CUtlVector< CHandle< CBasePropDoor > > m_doors;
	bool m_isOutward;
	bool m_hasRespawnedPlayers;
};

// S:0x2BDAB0: ordinary spawn orientation uses the real weapon-spawn registry,
// a strict 1000-unit bound and actual FVisible. No dummy item/camera is made.
void OrientTerrorSpawnToNearbyItem( const Vector &origin, QAngle *angles );
void ResetCheckpointSpawnOverride();
double FindScrimmagePointAtFlow( float flow, bool reverse, Vector *position, QAngle *angles );

// S258220: same formatter/console policy, shared with actual flow/world owners.
void FlowMessage( int indent, const char *format, ... );
// S2CF0C0: one native rescue-room traversal shared by runtime and NAV analysis.
void CollectSurvivorRescueAreas( CNavArea *start, float range,
	CUtlVector< CNavArea * > &areas, CUtlVector< CBasePropDoor * > *doors );

#endif // TERROR_NAV_WORLD_H
