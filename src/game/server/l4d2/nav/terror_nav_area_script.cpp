//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "nav_mesh.h"
#include "nav_ladder.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/player/terrorplayer.h"
#include "vscript_server.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

DEFINE_SCRIPT_INSTANCE_HELPER( TerrorNavArea, &g_NavAreaScriptInstanceHelper )
BEGIN_ENT_SCRIPTDESC_ROOT( TerrorNavArea, "Navigation areas class" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetID, "GetID", "Get area ID." )
	DEFINE_SCRIPTFUNC_NAMED( GetAttributes, "GetAttributes", "Get area attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( SetAttributes, "SetAttributes", "Set area attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( HasAttributes, "HasAttributes", "Has area attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( RemoveAttributes, "RemoveAttributes", "Removes area attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( GetCenter, "GetCenter", "Get center origin of area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetCorner, "GetCorner", "( corner ) - Get corner origin of area" )
	DEFINE_SCRIPTFUNC_NAMED( GetRandomSpawnPoint, "FindRandomSpot", "Get random origin within extent of area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptConnectToArea, "ConnectTo", "( area, dir ) - Connect this area to given area in given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptDisconnectArea, "Disconnect", "( area ) - Disconnect this area from given area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsConnectedArea, "IsConnected", "( area, dir ) - Return true if given area is connected in given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptConnectToLadder, "ConnectToLadder", "( ladder, dir ) - Connect this area to given ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptDisconnectLadder, "DisconnectLadder", "( ladder ) - Disconnect this area from given ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsConnectedLadder, "IsConnectedLadder", "( ladder, dir ) - Return true if given area is connected to ladder in given direction" )
	DEFINE_SCRIPTFUNC_NAMED( IsDamaging, "IsDamaging", "Return true if continuous damage (ie: fire) is in this area" )
	DEFINE_SCRIPTFUNC_NAMED( MarkAsDamaging, "MarkAsDamaging", "( duration ) - Mark this area is damaging for the next 'duration' seconds" )
	DEFINE_SCRIPTFUNC_NAMED( IsBlocked, "IsBlocked", "( team ) - Return true if team is blocked in this area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptMarkAsBlocked, "MarkAsBlocked", "( team ) - Mark this area as blocked for team" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetAdjacentCount, "GetAdjacentCount", "( dir ) - Get the number of adjacent areas in the given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetAdjacentAreas, "GetAdjacentAreas", "( dir, table ) - Fills a passed in table with all adjacent areas in the given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetAdjacentArea, "GetAdjacentArea", "( dir, n ) - Return the i'th adjacent area in the given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetRandomAdjacentArea, "GetRandomAdjacentArea", "( dir ) - Return a random adjacent area in the given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetIncomingConnections, "GetIncomingConnections", "( dir, table ) - Fills a passed in table with areas connected TO this area by a ONE-WAY link (ie: we have no connection back to them)" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptAddIncomingConnection, "AddIncomingConnection", "( area, dir ) - Add areas that connect TO this area by a ONE-WAY link" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetPlaceName, "GetPlaceName", "Get place name" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetPlaceName, "SetPlaceName", "( name ) - Set place name" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptComputeDirection, "ComputeDirection", "( point ) - Return direction from this area to the given point" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetLadders, "GetLadders", "( table ) - Fills a passed in table of ladders in direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetPlayerCount, "GetPlayerCount", "( team ) - Return number of players of given team currently within this area (team of zero means any/all)" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsOverlapping, "IsOverlapping", "( area ) - Return true if 'area' overlaps our 2D extents" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsOverlappingOrigin, "IsOverlappingOrigin", "( pos, tolerance ) - Return true if 'pos' is within 2D extents of area" )
	DEFINE_SCRIPTFUNC_NAMED( IsPotentiallyVisibleToTeam, "IsPotentiallyVisibleToTeam", "( team ) - Return true if any portion of this area is visible to anyone on the given team" )
	DEFINE_SCRIPTFUNC_NAMED( IsCompletelyVisibleToTeam, "IsCompletelyVisibleToTeam", "( team ) - Return true if given area is completely visible from somewhere in this area by someone on the team" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsEdge, "IsEdge", "( dir ) - Return true if there are no bi-directional links on the given side" )
	DEFINE_SCRIPTFUNC_NAMED( HasAvoidanceObstacle, "HasAvoidanceObstacle", "( maxheight ) - Returns true if there's a large, immobile object obstructing this area" )
	DEFINE_SCRIPTFUNC_NAMED( MarkObstacleToAvoid, "MarkObstacleToAvoid", "( height ) - Marks the obstructed status of the nav area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptContains, "Contains", "( area ) - Return true if other area is on or above this area, but no others" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptContainsOrigin, "ContainsOrigin", "( point ) - Return true if given point is on or above this area, but no others" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetParent, "GetParent", "Returns the area just prior to this one in the search path" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetParentHow, "GetParentHow", "Returns how we get from parent to us" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptDebugDrawFilled, "DebugDrawFilled", "Draw area as a filled rect of the given color" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptUnblockArea, "UnblockArea", "Unblocks this area" )
	DEFINE_SCRIPTFUNC_NAMED( IsRoughlySquare, "IsRoughlySquare", "Return true if this area is approximately square" )
	DEFINE_SCRIPTFUNC_NAMED( IsFlat, "IsFlat", "Return true if this area is approximately flat" )
	DEFINE_SCRIPTFUNC_NAMED( IsDegenerate, "IsDegenerate", "Return true if this area is badly formed" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsVisible, "IsVisible", "( point ) - Return true if area is visible from the given eyepoint" )
	DEFINE_SCRIPTFUNC_NAMED( GetSizeX, "GetSizeX", "Return the area size along the X axis" )
	DEFINE_SCRIPTFUNC_NAMED( GetSizeY, "GetSizeY", "Return the area size along the Y axis" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetZ, "GetZ", "( pos ) - Return Z of area at (x,y) of 'pos'" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetDistanceSquaredToPoint, "GetDistanceSquaredToPoint", "( pos ) - Return shortest distance between point and this area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetSpawnAttributes, "GetSpawnAttributes", "Get spawn attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptHasSpawnAttributes, "HasSpawnAttributes", "( attributes ) - Has spawn attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetSpawnAttributes, "SetSpawnAttributes", "( attributes ) - Set spawn attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptRemoveSpawnAttributes, "RemoveSpawnAttributes", "( attributes ) - Remove spawn attribute bits" )
	DEFINE_SCRIPTFUNC_NAMED( IsUnderwater, "IsUnderwater", "Return true if area is underwater" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsCoplanar, "IsCoplanar", "( area ) - Return true if this area and given area are approximately co-planar" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptRemoveOrthogonalConnections, "RemoveOrthogonalConnections", "( dir ) - Removes all connections in directions to left and right of specified direction" )
	DEFINE_SCRIPTFUNC_NAMED( GetAvoidanceObstacleHeight, "GetAvoidanceObstacleHeight", "Returns the maximum height of the obstruction above the ground" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetElevator, "GetElevator", "Returns the elevator if in an elevator's path" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetElevatorAreas, "GetElevatorAreas", "( table ) - Fills table with a collection of areas reachable via elevator from this area" )
	DEFINE_SCRIPTFUNC_NAMED( IsAvailableForSpawn, "IsSpawningAllowed", "Returns true if spawning is allowed" )
	DEFINE_SCRIPTFUNC_NAMED( MarkAsUncleared, "MarkAreaNotCleared", "Mark this area as not cleared" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetDoor, "GetDoor", "Returns the door entity above the area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsCleared, "IsCleared", "( player ) - Returns true if this area has been cleared for the player" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptMarkAreaCleared, "MarkAreaCleared", "( player ) - Mark this area as cleared for the player" )
	DEFINE_SCRIPTFUNC_NAMED( GetTimeSinceCleared, "GetTimeSinceCleared", "Returns the time since the area was cleared" )
	DEFINE_SCRIPTFUNC_NAMED( IsBottleneck, "IsBottleneck", "Returns true if area is a bottleneck" )
	DEFINE_SCRIPTFUNC_NAMED( IsValidForWanderingPopulation, "IsValidForWanderingPopulation", "Returns true if area is valid for wandering population" )
END_SCRIPTDESC()

HSCRIPT TerrorNavArea::GetScriptInstance()
{
	if ( !m_hScriptInstance )
		m_hScriptInstance = g_pScriptVM->RegisterInstance( GetScriptDesc(), this );
	return m_hScriptInstance;
}
void TerrorNavArea::ClearScriptInstance()
{
	HSCRIPT instance = m_hScriptInstance;
	m_hScriptInstance = NULL;
	if ( instance && g_pScriptVM ) g_pScriptVM->RemoveInstance( instance );
}
TerrorNavArea::~TerrorNavArea()
{
	// Native2E79C0 ->35AAE0 precedes removal of this object's VM instance.
	if ( TheZombieManager ) TheZombieManager->OnNavAreaDestroyed( this );
	ClearScriptInstance();
}
void TerrorNavArea::UpdateBlocked( bool force, int teamID )
{
	// S2E2820: rescue areas do NOT run the generic blocked refresh here.
	if ( !HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
		BaseClass::UpdateBlocked( force, teamID );
	else if ( ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() ) ||
		TheNavMesh->IsRescueVehicleNavUnblocked() )
		UnblockArea();
}
void TerrorNavArea::ScriptGetAdjacentAreas( int dir, HSCRIPT table )
{
	if ( dir >= NUM_DIRECTIONS || !IsValid( table ) ) return;
	const NavConnectVector *connections = GetAdjacentAreas( static_cast< NavDirType >( dir ) );
	FOR_EACH_VEC( *connections, index )
		if ( (*connections)[index].area )
			g_pScriptVM->SetValue( table, CFmtStr( "area%i", index ), ToHScript( (*connections)[index].area ) );
}
HSCRIPT TerrorNavArea::ScriptGetAdjacentArea( int dir, int index )
{
	if ( dir >= NUM_DIRECTIONS ) return NULL;
	// GetAdjacentArea itself rejects negative/out-of-range indices. Do not
	// add an unsigned direction check absent in the native wrapper.
	return ToHScript( GetAdjacentArea( static_cast< NavDirType >( dir ), index ) );
}
HSCRIPT TerrorNavArea::ScriptGetRandomAdjacentArea( int dir )
{
	return dir >= NUM_DIRECTIONS ? NULL : ToHScript( GetRandomAdjacentArea( static_cast< NavDirType >( dir ) ) );
}
void TerrorNavArea::ScriptGetIncomingConnections( int dir, HSCRIPT table )
{
	if ( dir >= NUM_DIRECTIONS || !IsValid( table ) ) return;
	const NavConnectVector *connections = GetIncomingConnections( static_cast< NavDirType >( dir ) );
	FOR_EACH_VEC( *connections, index )
		if ( (*connections)[index].area )
			g_pScriptVM->SetValue( table, CFmtStr( "area%i", index ), ToHScript( (*connections)[index].area ) );
}
void TerrorNavArea::ScriptAddIncomingConnection( HSCRIPT source, int dir )
{
	TerrorNavArea *area = ToNavArea( source );
	if ( dir < NUM_DIRECTIONS && area ) AddIncomingConnection( area, static_cast< NavDirType >( dir ) );
}
void TerrorNavArea::ScriptConnectToArea( HSCRIPT script, int dir )
{
	TerrorNavArea *area = ToNavArea( script );
	if ( dir >= NUM_DIRECTIONS || !area ) return;
	if ( dir == -1 )
	{
		Vector center;
		float width;
		dir = ComputeLargestPortal( area, &center, &width );
		if ( dir == NUM_DIRECTIONS ) return;
	}
	ConnectTo( area, static_cast< NavDirType >( dir ) );
}
void TerrorNavArea::ScriptDisconnectArea( HSCRIPT script )
{
	TerrorNavArea *area = ToNavArea( script );
	if ( area ) Disconnect( area );
}
bool TerrorNavArea::ScriptIsConnectedArea( HSCRIPT script, int dir )
{
	TerrorNavArea *area = ToNavArea( script );
	if ( dir > NUM_DIRECTIONS || !area ) return false;
	if ( dir == -1 ) dir = NUM_DIRECTIONS;
	return IsConnected( area, static_cast< NavDirType >( dir ) );
}
void TerrorNavArea::ScriptConnectToLadder( HSCRIPT script )
{
	CNavLadder *ladder = ToNavLadder( script );
	if ( ladder ) ConnectTo( ladder );
}
void TerrorNavArea::ScriptDisconnectLadder( HSCRIPT script )
{
	CNavLadder *ladder = ToNavLadder( script );
	if ( ladder ) Disconnect( ladder );
}
bool TerrorNavArea::ScriptIsConnectedLadder( HSCRIPT script, int dir )
{
	CNavLadder *ladder = ToNavLadder( script );
	// Native wrapper compares against FOUR, although ladder directions are TWO.
	return dir < NUM_DIRECTIONS && ladder ? IsConnected( ladder, static_cast< CNavLadder::LadderDirectionType >( dir ) ) : false;
}
void TerrorNavArea::ScriptMarkAsBlocked( int team )
{
	SetAttributes( GetAttributes() | static_cast< int >( 0x80000000U ) );
	MarkAsBlocked( team, NULL, true );
}
const char *TerrorNavArea::ScriptGetPlaceName()
{
	return TheNavMesh->PlaceToName( GetPlace() );
}
void TerrorNavArea::ScriptSetPlaceName( const char *name )
{
	if ( !name ) { SetPlace( UNDEFINED_PLACE ); return; }
	Place place = TheNavMesh->NameToPlace( name );
	if ( place != UNDEFINED_PLACE ) SetPlace( place );
}
void TerrorNavArea::ScriptGetLadders( int dir, HSCRIPT table )
{
	if ( dir >= CNavLadder::NUM_LADDER_DIRECTIONS || !IsValid( table ) ) return;
	const NavLadderConnectVector *connections = GetLadders( static_cast< CNavLadder::LadderDirectionType >( dir ) );
	FOR_EACH_VEC( *connections, index )
		if ( (*connections)[index].ladder )
			g_pScriptVM->SetValue( table, CFmtStr( "ladder%i", index ), ToHScript( (*connections)[index].ladder ) );
}
bool TerrorNavArea::ScriptIsOverlapping( HSCRIPT script ) const
{
	TerrorNavArea *area = ToNavArea( script );
	return area ? IsOverlapping( area ) : false;
}
bool TerrorNavArea::ScriptContains( HSCRIPT script ) const
{
	TerrorNavArea *area = ToNavArea( script );
	return area ? Contains( area ) : false;
}
HSCRIPT TerrorNavArea::ScriptGetParent()
{
	return ToHScript( GetParent() );
}
bool TerrorNavArea::ScriptIsCoplanar( HSCRIPT script ) const
{
	TerrorNavArea *area = ToNavArea( script );
	return area ? IsCoplanar( area ) : false;
}
void TerrorNavArea::ScriptRemoveOrthogonalConnections( int dir )
{
	if ( dir < NUM_DIRECTIONS ) RemoveOrthogonalConnections( static_cast< NavDirType >( dir ) );
}
HSCRIPT TerrorNavArea::ScriptGetElevator()
{
	return ToHScript( (CBaseEntity *)GetElevator() );
}
void TerrorNavArea::ScriptGetElevatorAreas( HSCRIPT table )
{
	if ( !IsValid( table ) ) return;
	const NavConnectVector &areas = GetElevatorAreas();
	FOR_EACH_VEC( areas, index )
		if ( areas[index].area )
			g_pScriptVM->SetValue( table, CFmtStr( "area%i", index ), ToHScript( areas[index].area ) );
}
HSCRIPT TerrorNavArea::ScriptGetDoor()
{
	return ToHScript( GetDoor() );
}
bool TerrorNavArea::ScriptIsCleared( HSCRIPT script ) const
{
	CBaseEntity *entity = script ? ToEnt( script ) : NULL;
	return IsCleared( dynamic_cast< CTerrorPlayer * >( entity ) );
}
void TerrorNavArea::ScriptMarkAreaCleared( HSCRIPT script )
{
	CBaseEntity *entity = script ? ToEnt( script ) : NULL;
	MarkAsCleared( dynamic_cast< CTerrorPlayer * >( entity ) );
}
float TerrorNavArea::GetTimeSinceCleared() const
{
	if ( !m_clearedCharacters || m_potentiallyVisibleCharacters ) return 0.0f;
	return m_clearedTimer.GetElapsedTime();
}
bool TerrorNavArea::IsBottleneck() const
{
	if ( GetAdjacentCount( NORTH ) == 0 && GetAdjacentCount( SOUTH ) == 0 &&
		GetAdjacentCount( EAST ) > 0 && GetAdjacentCount( WEST ) > 0 )
		return GetSizeY() < 52.499996f;
	if ( GetAdjacentCount( NORTH ) > 0 && GetAdjacentCount( SOUTH ) > 0 &&
		GetAdjacentCount( EAST ) == 0 && GetAdjacentCount( WEST ) == 0 )
		return GetSizeX() < 52.499996f;
	return false;
}

void VScriptReleaseNavScriptInstances()
{
	// SDK VM lifetime adaptation: same owners, no auxiliary handle registry.
	// Called while this VM is alive, after response payload/history teardown.
	FOR_EACH_VEC( TheNavAreas, index )
		static_cast< TerrorNavArea * >( TheNavAreas[index] )->ClearScriptInstance();
	if ( TheNavMesh )
	{
		const NavLadderVector &ladders = TheNavMesh->GetLadders();
		FOR_EACH_VEC( ladders, index ) ladders[index]->ClearScriptInstance();
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
