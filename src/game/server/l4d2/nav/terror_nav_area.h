//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_NAV_AREA_H
#define TERROR_NAV_AREA_H
#ifdef _WIN32
#pragma once
#endif

#include "nav_area.h"
#include "l4d2/terror_shareddefs.h"
#include "util_shared.h"
#include "vscript_shared.h"

class TerrorNavMesh;
class PlaceDirectory;
class CTerrorPlayer;

// These are a separate namespace from CNavArea's base movement attributes.
enum TerrorSpawnAttributeType
{
	TERROR_NAV_EMPTY             = 0x000002,
	TERROR_NAV_STOP_SCAN         = 0x000004,
	TERROR_NAV_BATTLESTATION     = 0x000020,
	TERROR_NAV_FINALE            = 0x000040,
	TERROR_NAV_PLAYER_START      = 0x000080,
	TERROR_NAV_BATTLEFIELD       = 0x000100,
	TERROR_NAV_IGNORE_VISIBILITY = 0x000200,
	TERROR_NAV_NOT_CLEARABLE     = 0x000400,
	TERROR_NAV_CHECKPOINT        = 0x000800,
	TERROR_NAV_OBSCURED          = 0x001000,
	TERROR_NAV_NO_MOBS           = 0x002000,
	TERROR_NAV_THREAT            = 0x004000,
	TERROR_NAV_RESCUE_VEHICLE    = 0x008000,
	TERROR_NAV_RESCUE_CLOSET     = 0x010000,
	TERROR_NAV_ESCAPE_ROUTE      = 0x020000,
	TERROR_NAV_DOOR              = 0x040000,
	TERROR_NAV_NOTHREAT          = 0x080000,
	TERROR_NAV_LYINGDOWN         = 0x100000
};

enum TerrorFlowDirection
{
	FLOW_TOWARD_GOAL = 0,
	FLOW_FROM_START,
	NUM_TERROR_FLOW_DIRECTIONS
};

class TerrorNavArea : public CNavArea
{
public:
	DECLARE_CLASS( TerrorNavArea, CNavArea );
	DECLARE_ENT_SCRIPTDESC();
	virtual ~TerrorNavArea();
	virtual void UpdateBlocked( bool force = false, int teamID = TEAM_ANY ) OVERRIDE;
	virtual void OnEnter( CBaseCombatCharacter *who, CNavArea *areaJustLeft ) OVERRIDE;
	virtual void OnExit( CBaseCombatCharacter *who, CNavArea *areaJustEntered ) OVERRIDE;
	HSCRIPT GetScriptInstance();
	void ClearScriptInstance();
	int ScriptGetID() const { return GetID(); }
	// Native descriptor is FIELD_TYPEUNKNOWN30 for this const-value return.
	const Vector ScriptGetCorner( int corner ) const { return GetCorner( static_cast< NavCornerType >( corner ) ); }
	void ScriptConnectToArea( HSCRIPT area, int dir );
	void ScriptDisconnectArea( HSCRIPT area );
	bool ScriptIsConnectedArea( HSCRIPT area, int dir );
	void ScriptConnectToLadder( HSCRIPT ladder );
	void ScriptDisconnectLadder( HSCRIPT ladder );
	bool ScriptIsConnectedLadder( HSCRIPT ladder, int dir );
	void ScriptMarkAsBlocked( int team );
	int ScriptGetAdjacentCount( int dir ) const { return GetAdjacentCount( static_cast< NavDirType >( dir ) ); }
	void ScriptGetAdjacentAreas( int dir, HSCRIPT table );
	HSCRIPT ScriptGetAdjacentArea( int dir, int index );
	HSCRIPT ScriptGetRandomAdjacentArea( int dir );
	void ScriptGetIncomingConnections( int dir, HSCRIPT table );
	void ScriptAddIncomingConnection( HSCRIPT source, int dir );
	const char *ScriptGetPlaceName();
	void ScriptSetPlaceName( const char *name );
	int ScriptComputeDirection( const Vector &point ) const { Vector copy = point; return ComputeDirection( &copy ); }
	void ScriptGetLadders( int dir, HSCRIPT table );
	int ScriptGetPlayerCount( int team ) const { return GetPlayerCount( team ); }
	bool ScriptIsOverlapping( HSCRIPT area ) const;
	bool ScriptIsOverlappingOrigin( const Vector &point, float tolerance ) const { return IsOverlapping( point, tolerance ); }
	bool ScriptIsEdge( int dir ) const { return IsEdge( static_cast< NavDirType >( dir ) ); }
	bool ScriptContains( HSCRIPT area ) const;
	bool ScriptContainsOrigin( const Vector &point ) const { return Contains( point ); }
	HSCRIPT ScriptGetParent();
	int ScriptGetParentHow() const { return GetParentHow(); }
	void ScriptDebugDrawFilled( int r, int g, int b, int a, float duration, bool noDepth ) const { DrawFilled( r, g, b, a, duration, noDepth, 5.0f ); }
	void ScriptUnblockArea() { UnblockArea(); }
	bool ScriptIsVisible( const Vector &eye ) const { return IsVisible( eye ); }
	float ScriptGetZ( const Vector &point ) const { return GetZ( point ); }
	float ScriptGetDistanceSquaredToPoint( const Vector &point ) const { return (float)GetDistanceSquaredToPoint( point ); }
	int ScriptGetSpawnAttributes() const { return GetSpawnAttributes(); }
	bool ScriptHasSpawnAttributes( int attributes ) const { return HasSpawnAttributes( attributes ); }
	void ScriptSetSpawnAttributes( int attributes ) { AddSpawnAttributes( attributes ); }
	void ScriptRemoveSpawnAttributes( int attributes ) { RemoveSpawnAttributes( attributes ); }
	bool ScriptIsCoplanar( HSCRIPT area ) const;
	void ScriptRemoveOrthogonalConnections( int dir );
	HSCRIPT ScriptGetElevator();
	void ScriptGetElevatorAreas( HSCRIPT table );
	HSCRIPT ScriptGetDoor();
	bool ScriptIsCleared( HSCRIPT player ) const;
	void ScriptMarkAreaCleared( HSCRIPT player );
	float GetTimeSinceCleared() const;
	bool IsBottleneck() const;

	TerrorNavArea();
	TerrorNavArea *GetNextHighestFlowConnectedArea( NavTraverseType *how = NULL ) const;
	virtual void OnRoundRestart() OVERRIDE;
	virtual void Save( CUtlBuffer &fileBuffer, unsigned int version ) const;
	virtual NavErrorType Load( CUtlBuffer &fileBuffer, unsigned int version, unsigned int subVersion );
	virtual NavErrorType PostLoad();
	virtual void SaveToSelectedSet( KeyValues *areaKey ) const;
	virtual void RestoreFromSelectedSet( KeyValues *areaKey );
	virtual bool IsAbleToMergeWith( CNavArea *other ) const;
	virtual void InheritAttributes( CNavArea *first, CNavArea *second = NULL );
	virtual bool IsBlocked( int teamID, bool ignoreNavBlockers = false ) const;
	virtual bool IsPotentiallyVisibleToTeam( int team ) const;
	virtual bool IsCompletelyVisible( const CNavArea *viewedArea ) const OVERRIDE;
	virtual bool IsPotentiallyVisible( const CNavArea *viewedArea ) const OVERRIDE;
	virtual bool IsCompletelyVisibleToTeam( int teamIndex ) const OVERRIDE;
	bool IsPotentiallyVisible( const CNavArea *viewedArea, bool ignoreObscuredRange ) const;
	bool IsVisibleFrom( const Vector &eye ) const;
	void SetDoor( CBaseEntity *door ) { m_hDoor = door; }
	CBaseEntity *GetDoor() const { return m_hDoor.Get(); }
	static void BeginAASCollection();
	static void ResetAASCollection();
	bool IsAASCollectionMarked() const;
	void MarkAASCollection();
	bool IsInPlayerAAS( SurvivorCharacterType character ) const;
	bool IsInAnyPlayerAAS() const { return m_playerAASMask != 0; }
	void OnPlayerEnterAAS( SurvivorCharacterType character );
	void OnPlayerLeaveAAS( SurvivorCharacterType character );
	void SetPotentiallyVisibleByCharacter( SurvivorCharacterType character, bool visible );
	void SetCompletelyVisibleByCharacter( SurvivorCharacterType character, bool visible );
	bool IsPotentiallyVisibleByCharacter( SurvivorCharacterType character ) const;
	bool IsCompletelyVisibleByCharacter( SurvivorCharacterType character ) const;
	bool IsPotentiallyVisibleToAnyCharacter() const { return m_potentiallyVisibleCharacters != 0; }
	bool IsCompletelyVisibleToAnyCharacter() const { return m_completelyVisibleCharacters != 0; }
	unsigned char GetPotentiallyVisibleCharacterMask() const { return m_potentiallyVisibleCharacters; }
	unsigned char GetCompletelyVisibleCharacterMask() const { return m_completelyVisibleCharacters; }
	bool IsCleared( const CTerrorPlayer *player = NULL ) const;
	void MarkAsCleared( CTerrorPlayer *player = NULL );
	void MarkAsUncleared();
	int GetWanderingPopulation() const { return m_wanderingPopulation; }
	void SetWanderingPopulation( int population ) { m_wanderingPopulation = population; }
	bool IsValidForWanderingPopulation() const;
	bool IsAvailableForSpawn() const;
	Vector GetRandomSpawnPoint() const;
	void PopulateWanderers();

	unsigned int GetSpawnAttributes() const { return m_spawnAttributes; }
	bool HasSpawnAttributes( unsigned int attributes ) const { return ( m_spawnAttributes & attributes ) != 0; }
	void SetSpawnAttributes( unsigned int attributes ) { m_spawnAttributes = attributes; }
	void AddSpawnAttributes( unsigned int attributes ) { m_spawnAttributes |= attributes; }
	void RemoveSpawnAttributes( unsigned int attributes ) { m_spawnAttributes &= ~attributes; }
	const char *GetPopulationName() const;
	unsigned int GetPopulationIndex() const { return m_population; }
	int GetRawVisibilityCount( bool includeInherited ) const;
	CNavArea::VisibilityType ComputeDebugVisibility( const TerrorNavArea *area ) const;
	double ComputeSurfaceHeightChange( const TerrorNavArea *destination ) const;

	// Index 0 is reverse-computed, then normalized to zero at the start.
	// Index 1 is raw forward distance from the start. Neither is serialized.
	// A negative/unavailable value must not be replaced with zero.
	float GetFlowDistance( TerrorFlowDirection direction = FLOW_TOWARD_GOAL ) const { return m_flowDistance[direction]; }
	float GetFlowDistanceFromStart() const { return m_flowDistance[FLOW_FROM_START]; }
	float GetFlowDistanceTowardGoal() const { return m_flowDistance[FLOW_TOWARD_GOAL]; }

	// Base serializer seams: Place indices and visibility ordinals are not area IDs.
	void OnLoadPlace( unsigned short index );
	unsigned short GetPlaceDirectoryIndex( const PlaceDirectory &directory ) const;
	bool UsesRetailBaseAttributes() const { return m_usesTerrorAttributes; }
	bool HasFlowFlatGeometry() const { return m_usesTerrorAttributes && HasAttributes( 0x20000000 ); }
	unsigned int GetSerializedBaseAttributes() const;
	void UpdateFlowFlatGeometry();
	float GetFlowMinZ() const { return GetCorner( static_cast< NavCornerType >( m_lowestCorner ) ).z; }
	float GetFlowMaxZ() const { return GetCorner( static_cast< NavCornerType >( m_highestCorner ) ).z; }
	static unsigned int ParseSpawnAttribute( const char *name );
	static const unsigned int NAV_PLAYERCLIP = 0x00040000;
	static const unsigned int PERSISTENT_SPAWN_ATTRIBUTES = 0x0019FFE6;

private:
	friend class TerrorNavMesh;
	unsigned int m_spawnAttributes;
	unsigned int m_population; // Native344; one-based population/fog index,0 unset.
	float m_flowDistance[NUM_TERROR_FLOW_DIRECTIONS];
	unsigned int m_saveOrdinal;
	unsigned short m_placeDirectoryIndex;
	Place m_loadedPlace;
	bool m_visibilityUsesOrdinals;
	unsigned char m_lowestCorner;
	unsigned char m_highestCorner;
	static unsigned int s_aasCollectionMarker;
	unsigned int m_aasCollectionMarker; // native288
	int m_wanderingPopulation; // native292, pending population rather than live actor count
	unsigned char m_clearedCharacters; // native317
	IntervalTimer m_clearedTimer; // native320
	float m_boundaryDistance; // native328, initialized by actual round boundary propagation
	unsigned int m_playerAASMask; // native340
	IntervalTimer m_unclearedTimer; // native352
	EHANDLE m_hDoor; // native364
	HSCRIPT m_hScriptInstance; // Native368, ONE lazy instance on this real area.
	unsigned char m_potentiallyVisibleCharacters; // native base125
	unsigned char m_completelyVisibleCharacters; // native base126
};

inline TerrorNavArea *ToTerrorNavArea( CNavArea *area )
{
	return static_cast< TerrorNavArea * >( area );
}

inline const TerrorNavArea *ToTerrorNavArea( const CNavArea *area )
{
	return static_cast< const TerrorNavArea * >( area );
}

inline HSCRIPT ToHScript( CNavArea *area )
{
	return area ? static_cast< TerrorNavArea * >( area )->GetScriptInstance() : NULL;
}
template <> ScriptClassDesc_t *GetScriptDesc<TerrorNavArea>( TerrorNavArea * );
inline TerrorNavArea *ToNavArea( HSCRIPT script )
{
	if ( script == NULL || script == INVALID_HSCRIPT ) return NULL;
	return static_cast< TerrorNavArea * >( g_pScriptVM->GetInstanceValue( script, GetScriptDescForClass( TerrorNavArea ) ) );
}
#endif // TERROR_NAV_AREA_H
