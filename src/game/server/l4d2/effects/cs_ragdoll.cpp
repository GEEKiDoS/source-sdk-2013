//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/cs_ragdoll.h"
#include "l4d2/player/cs_player.h"
#include "l4d2/terror_shareddefs.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( cs_ragdoll, CCSRagdoll );

IMPLEMENT_SERVERCLASS_ST_NOBASE( CCSRagdoll, DT_CSRagdoll )
	SendPropVector( SENDINFO( m_vecOrigin ), -1, SPROP_COORD_MP | SPROP_CHANGES_OFTEN ),
	SendPropVector( SENDINFO( m_vecRagdollOrigin ), -1, SPROP_COORD_MP ),
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropInt( SENDINFO( m_nModelIndex ), 12 ),
	SendPropInt( SENDINFO( m_nForceBone ), 8 ),
	SendPropVector( SENDINFO( m_vecForce ), -1, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_vecRagdollVelocity ), -1, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_iDeathPose ), 13 ),
	SendPropInt( SENDINFO( m_iDeathFrame ), 5 ),
	SendPropInt( SENDINFO( m_iTeamNum ), 6 ),
	SendPropBool( SENDINFO( m_bClientSideAnimation ) ),
	SendPropBool( SENDINFO( m_bOnFire ) ),
	SendPropBool( SENDINFO( m_bInterpOrigin ) ),
	SendPropInt( SENDINFO( m_ragdollType ), 4 ),
	SendPropInt( SENDINFO( m_zombieClass ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_survivorCharacter ), 8 ),
END_SEND_TABLE()

BEGIN_DATADESC( CCSRagdoll )
	DEFINE_THINKFUNC( PruneThink ),
END_DATADESC()

ConVar z_ragdoll_discard_range( "z_ragdoll_discard_range", "2000", 0x00000006, "Discard ragdolls farther away than this" );

// S248E70 relies on the entity allocator's zeros, except for this invalid handle.
CCSRagdoll::CCSRagdoll()
{
	m_hPlayer = NULL;
}

int CCSRagdoll::ObjectCaps()
{
	return BaseClass::ObjectCaps() | FCAP_DONT_SAVE;
}

int CCSRagdoll::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CCSRagdoll::PruneThink()
{
	// S240150 -> S251280: only live, edict-backed TEAM_SURVIVOR players.
	// That helper compares their world-space centers with the corpse's origin.
	const Vector &origin = GetAbsOrigin();
	CBasePlayer *closest = NULL;
	float closestDistanceSquared = 1.0e12f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player || !player->edict() || ENTINDEX( player->edict() ) == 0 ||
			!player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		float distanceSquared = ( player->WorldSpaceCenter() - origin ).LengthSqr();
		if ( distanceSquared < closestDistanceSquared )
		{
			closestDistanceSquared = distanceSquared;
			closest = player;
		}
	}

	if ( closest && sqrtf( closestDistanceSquared ) > z_ragdoll_discard_range.GetFloat() )
	{
		UTIL_Remove( this );
		return;
	}

	// The original passes 10.0 directly to SetNextThink, not curtime + 10.
	SetNextThink( 10.0f );
}

// S245630: the death caller has already assigned the real player handle.
void CCSRagdoll::InitializeFromPlayer()
{
	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_STEP );
	SetFriction( 1.0f );
	SetCollisionBounds( VEC_DUCK_HULL_MIN, VEC_DUCK_HULL_MAX );
	m_takedamage = DAMAGE_NO;
	SetCollisionGroup( COLLISION_GROUP_DEBRIS );
	SetAbsOrigin( m_hPlayer->GetAbsOrigin() );
	SetAbsVelocity( m_hPlayer->GetAbsVelocity() );
	AddSolidFlags( FSOLID_NOT_SOLID );
	ChangeTeam( m_hPlayer->GetTeamNumber() );
	UseClientSideAnimation();
	m_bOnFire = ( m_hPlayer->GetFlags() & FL_ONFIRE ) &&
		m_hPlayer->GetEffectEntity() && m_hPlayer->GetTeamNumber() == TEAM_INFECTED;
}
