//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/func_simpleladder.h"
#include <stdlib.h>
#ifdef CLIENT_DLL
#include "l4d2/gamerules/terror_gamerules.h"
#include "model_types.h"
#endif
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( FuncSimpleLadder, DT_FuncSimpleLadder )
BEGIN_NETWORK_TABLE( CFuncSimpleLadder, DT_FuncSimpleLadder )
#ifdef CLIENT_DLL
	RecvPropVector( RECVINFO( m_climbableNormal ) ),
#else
	SendPropVector( SENDINFO( m_climbableNormal ), -1, SPROP_NOSCALE ),
#endif
END_NETWORK_TABLE()

CFuncSimpleLadder::CFuncSimpleLadder()
{
	m_climbableNormal.Init();
}

BEGIN_DATADESC( CFuncSimpleLadder )
	DEFINE_FIELD( m_climbableNormal, FIELD_VECTOR ),
END_DATADESC()

#ifdef CLIENT_DLL
// C0F0FA0: ladders are visible only in player-controlled-zombie modes,
// and the ordinary+shadow-depth combined pass is always suppressed.
int CFuncSimpleLadder::DrawModel( int flags )
{
	if ( !TerrorGameRules()->IsPlayerControlledZombies() ||
		( ( flags & STUDIO_RENDER ) && ( flags & STUDIO_SHADOWDEPTHTEXTURE ) ) )
		return 0;
	return BaseClass::DrawModel( flags );
}
#endif

#ifndef CLIENT_DLL
LINK_ENTITY_TO_CLASS( func_simpleladder, CFuncSimpleLadder );
extern ConVar sv_showladders;


void CFuncSimpleLadder::Spawn()
{
	// S:0xEA280. SetModel does not supply or derive the authored normal.
	SetClassname( "func_simpleladder" );
	Precache();
	SetSolid( SOLID_BSP );
	SetModel( STRING( GetModelName() ) );
}

void CFuncSimpleLadder::Activate()
{
	BaseClass::Activate();
	if ( sv_showladders.GetBool() )
		m_debugOverlays |= OVERLAY_TEXT_BIT;
}

bool CFuncSimpleLadder::KeyValue( const char *key, const char *value )
{
	// S:0xEA420 also passes handled keys to the base implementation.
	if ( !Q_stricmp( key, "normal.x" ) )
		m_climbableNormal.SetX( atof( value ) );
	else if ( !Q_stricmp( key, "normal.y" ) )
		m_climbableNormal.SetY( atof( value ) );
	else if ( !Q_stricmp( key, "normal.z" ) )
		m_climbableNormal.SetZ( atof( value ) );
	else if ( !Q_stricmp( key, "team" ) )
	{
		int team = atoi( value );
		ChangeTeam( team == 1 ? 2 : ( team == 2 ? 3 : 0 ) );
	}
	return BaseClass::KeyValue( key, value );
}
#endif
