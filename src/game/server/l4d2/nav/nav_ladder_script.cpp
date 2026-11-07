//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "nav_ladder.h"
#include "l4d2/nav/terror_nav_area.h"
#include "vscript_server.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

class CNavLadderScriptInstanceHelper : public IScriptInstanceHelper
{
	bool ToString( void *instance, char *buffer, int size ) OVERRIDE
	{
		CNavLadder *ladder = static_cast< CNavLadder * >( instance );
		V_snprintf( buffer, size, "([%u] Ladder)", ladder->GetID() );
		return true;
	}
};
static CNavLadderScriptInstanceHelper g_NavLadderScriptInstanceHelper;

DEFINE_SCRIPT_INSTANCE_HELPER( CNavLadder, &g_NavLadderScriptInstanceHelper )
BEGIN_ENT_SCRIPTDESC_ROOT( CNavLadder, "Navigation ladders class" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetID, "GetID", "Get ladder ID." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptConnectTo, "ConnectTo", "Connect this ladder to given area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptDisconnect, "Disconnect", "Disconnect this ladder from given area" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsConnected, "IsConnected", "Return true if given ladder is connected in given direction" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetBottomOrigin, "GetBottomOrigin", "World coords of the bottom of the ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetTopOrigin, "GetTopOrigin", "World coords of the top of the ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetBottomArea, "GetBottomArea", "Area of the bottom of the ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetTopArea, "GetTopArea", "Area of the top of the ladder" )
	DEFINE_SCRIPTFUNC_NAMED( IsUsableByTeam, "IsUsableByTeam", "Returns true if ladder is usable for team" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetLadderEntity, "GetLadderEntity", "Returns the ladder entity" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsInUse, "IsInUse", "Return true if someone is on this ladder (other than 'ignore')" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetLength, "GetLength", "Return the length of the ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetWidth, "GetWidth", "Return the width of the ladder" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetDir, "GetDir", "Return the way the ladder faces (ie: surface normal of climbable side)" )
	DEFINE_SCRIPTFUNC_NAMED( GetPosAtHeight, "GetPosAtHeight", "Return x,y coordinate of the ladder at a given height" )
END_SCRIPTDESC()

HSCRIPT CNavLadder::GetScriptInstance()
{
	if ( !m_hScriptInstance )
		m_hScriptInstance = g_pScriptVM->RegisterInstance( GetScriptDesc(), this );
	return m_hScriptInstance;
}
void CNavLadder::ClearScriptInstance()
{
	HSCRIPT instance = m_hScriptInstance;
	m_hScriptInstance = NULL;
	if ( instance && g_pScriptVM ) g_pScriptVM->RemoveInstance( instance );
}
void CNavLadder::ScriptConnectTo( HSCRIPT script )
{
	TerrorNavArea *area = ToNavArea( script );
	if ( area ) ConnectTo( area );
}
void CNavLadder::ScriptDisconnect( HSCRIPT script )
{
	TerrorNavArea *area = ToNavArea( script );
	if ( area ) Disconnect( area );
}
bool CNavLadder::ScriptIsConnected( HSCRIPT script, int dir )
{
	TerrorNavArea *area = ToNavArea( script );
	return dir < NUM_LADDER_DIRECTIONS && area ? IsConnected( area, static_cast< LadderDirectionType >( dir ) ) : false;
}
HSCRIPT CNavLadder::ScriptGetBottomArea()
{
	return ToHScript( m_bottomArea );
}
HSCRIPT CNavLadder::ScriptGetTopArea()
{
	// Native uses top-FORWARD, not a search of four possible top connections.
	return ToHScript( m_topForwardArea );
}
HSCRIPT CNavLadder::ScriptGetLadderEntity()
{
	return ToHScript( GetLadderEntity() );
}
bool CNavLadder::ScriptIsInUse( HSCRIPT script ) const
{
	CBaseEntity *entity = script ? ToEnt( script ) : NULL;
	return IsInUse( dynamic_cast< CBasePlayer * >( entity ) );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
