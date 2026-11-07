//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/terror_trace_filter.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "basegrenade_shared.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "basegrenade_shared.h"
#include "props.h"
#include "func_break.h"
#endif
#include "tier0/memdbgon.h"

// NULL/static-prop conversion is deliberately distinct from base filtering.
static CBaseEntity *TraceFilterEntity( IHandleEntity *handle )
{
	return handle ? EntityFromEntityHandle( handle ) : NULL;
}

bool CTraceFilterIgnoreWeapons::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSkipTwoEntities::ShouldHitEntity( handle, mask ) )
		return false;
	return dynamic_cast< CBaseGrenade * >( TraceFilterEntity( handle ) ) == NULL;
}

void CTraceFilterPlayerMovement::SetPassEntity( const IHandleEntity *pass )
{
	CTraceFilterSimple::SetPassEntity( pass );
	CTerrorPlayer *player = ToTerrorPlayer( TraceFilterEntity( const_cast< IHandleEntity * >( pass ) ) );
	m_isGhost = player && player->IsGhost();
}

bool CTraceFilterPlayerMovement::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSkipTwoEntities::ShouldHitEntity( handle, mask ) || !m_isGhost )
		return false;
	CBaseEntity *entity = TraceFilterEntity( handle );
	if ( !entity )
		return true;
	if ( entity->PhysicsSolidMaskForEntity() & 0x200 )
		return false;
#ifndef CLIENT_DLL
	// Native S2D30FB..31A1 is server-only; the client has no breakable test.
	if ( entity->m_takedamage == DAMAGE_YES && (float)entity->GetHealth() <= 30.0f )
	{
		if ( FClassnameIs( entity, "func_breakable_surf" ) )
			return false;
		CBreakableProp *prop = dynamic_cast< CBreakableProp * >( entity );
		if ( prop && !prop->HasInteraction( (propdata_interactions_t)17 ) )
			return false;
		CBreakable *brush = dynamic_cast< CBreakable * >( entity );
		if ( brush && !brush->HasInteraction( (propdata_interactions_t)17 ) )
			return false;
	}
#endif
	return true;
}

bool CTraceFilterSimpleListNoGhosts::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSimpleList::ShouldHitEntity( handle, mask ) )
		return false;
	CTerrorPlayer *player = ToTerrorPlayer( TraceFilterEntity( handle ) );
	return !player || !player->IsGhost();
}

#ifndef CLIENT_DLL
bool CTraceFilterSimpleListNoInfectedOrGhosts::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSimpleList::ShouldHitEntity( handle, mask ) )
		return false;
	CBaseEntity *entity = TraceFilterEntity( handle );
	if ( entity && entity->MyInfectedPointer() )
		return false;
	return CTraceFilterSimpleListNoGhosts::ShouldHitEntity( handle, mask );
}

bool CTraceFilterSimpleNoInfectedTeamOrGhosts::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSimpleList::ShouldHitEntity( handle, mask ) )
		return false;
	CBaseEntity *entity = TraceFilterEntity( handle );
	return entity && entity->GetTeamNumber() != 3 &&
		CTraceFilterSimpleListNoGhosts::ShouldHitEntity( handle, mask );
}

bool ZombieBotCollisionTraceFilter::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSimple::ShouldHitEntity( handle, mask ) )
		return false;
	CBaseEntity *entity = TraceFilterEntity( handle );
	if ( !entity )
		return true;
	CTerrorPlayer *player = ToTerrorPlayer( entity );
	if ( player && ( player->IsGhost() || entity->GetTeamNumber() == 3 ) )
		return false;
	return !entity->MyNextBotPointer() || entity->GetTeamNumber() != 3;
}

bool CTraceFilterIgnoreActors::ShouldHitEntity( IHandleEntity *handle, int mask )
{
	if ( !CTraceFilterSimple::ShouldHitEntity( handle, mask ) )
		return false;
	CBaseEntity *entity = TraceFilterEntity( handle );
	if ( !entity || !entity->MyCombatCharacterPointer() || ( m_team != -1 && m_team != entity->GetTeamNumber() ) )
		return true;
	return m_hitTanks && entity->MyCombatCharacterPointer()->GetTeamNumber() == 3 &&
		entity->MyCombatCharacterPointer()->GetZombieClass() == 8;
}

#endif
