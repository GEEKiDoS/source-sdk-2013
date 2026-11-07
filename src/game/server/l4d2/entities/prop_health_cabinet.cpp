//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/prop_health_cabinet.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "datacache/imdlcache.h"

#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( prop_health_cabinet, CPropHealthCabinet );

IMPLEMENT_SERVERCLASS_ST( CPropHealthCabinet, DT_PropHealthCabinet )
	SendPropBool( SENDINFO( m_isUsed ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CPropHealthCabinet )
	DEFINE_KEYFIELD( m_pillCount, FIELD_INTEGER, "HealthCount" ),
END_DATADESC()

CPropHealthCabinet::CPropHealthCabinet()
{
	// S:2C13E0. The FGD's HealthCount/model defaults are authored keys, not
	// constructor substitutions. Population waits for the actual NAV event.
	m_pillCount = 0;
	m_isUsed = false;
	m_isPopulated = false;
	ListenForGameEvent( "round_start_post_nav" );
}

void CPropHealthCabinet::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Doors.Medkit_Cabinet" );
}

void CPropHealthCabinet::Spawn()
{
	// S:2C1580. Do not force the FGD model over the mapper's supplied model.
	BaseClass::Spawn();
	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_VPHYSICS );
	AddSolidFlags( FSOLID_TRIGGER );
	AddEffects( EF_NOSHADOW );
	CollisionProp()->UseTriggerBounds( true, 24.0f );
	m_flFadeScale = 0.0f;
	m_isUsed = false;
	SetCollisionGroup( COLLISION_GROUP_NONE );
}

int CPropHealthCabinet::ObjectCaps()
{
	// S:2C1480: no additional disabled/team/health/count gate here.
	return m_isUsed ? BaseClass::ObjectCaps() : BaseClass::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
}

void CPropHealthCabinet::SetOpen()
{
	m_isUsed = true;
	// Original S:2C1640/S:2C1770 select ACT_DOOR_OPEN816. Resolve that
	// actual model activity by name rather than treating x86 IDs as SDK IDs.
	PropSetSequence( SelectWeightedSequence( static_cast< Activity >( LookupActivity( "ACT_DOOR_OPEN" ) ) ) );
	SetCollisionGroup( COLLISION_GROUP_INTERACTIVE );
}

void CPropHealthCabinet::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// S:2C1640. Team4 is also an original survivor team; neither +use type
	// nor cabinet health controls this one-shot open transition.
	if ( !pActivator || !pActivator->IsPlayer() )
		return;
	const int team = pActivator->GetTeamNumber();
	if ( ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) || m_isUsed )
		return;
	SetOpen();
	EmitSound( "Doors.Medkit_Cabinet" );
	// S:27C980 invalidates the actual hidden-item monitor entries whose
	// AABBs intersect this cabinet. It does not create a second item list.
	TerrorGameRules()->OnHealthCabinetOpened( this );
}

void CPropHealthCabinet::FireGameEvent( IGameEvent *pEvent )
{
	// S:2C1DB0. A second round event does not repopulate the same cabinet.
	if ( !m_isPopulated && !Q_stricmp( pEvent->GetName(), "round_start_post_nav" ) )
		Populate();
}

void CPropHealthCabinet::Populate()
{
	// S:2C1770. This guard is set BEFORE all Director suppression gates.
	if ( m_isPopulated )
		return;
	m_isPopulated = true;
	if ( TerrorGameRules()->IsVersusMode() && TerrorGameRules()->IsHealthCabinetPopulationSuppressed() )
		return;

	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	Extent checkpointBounds;
	bool bCheckpointFlag = false;
	const bool bCheckpoint = TerrorGameRules()->GetHealthCabinetCheckpoint( GetAbsOrigin(), &checkpointBounds, &bCheckpointFlag );

	if ( TerrorGameRules()->ShouldOpenHealthCabinetsInCheckpoint() && bCheckpoint && !bCheckpointFlag )
	{
		TerrorNavArea *pArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( WorldSpaceCenter(), false, 10000.0f, false, true, TEAM_UNASSIGNED ) );
		if ( pArea && pArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
		{
			// The original forced-open path has no sound, no item notification,
			// and no spawning. Do not route it through Use.
			SetOpen();
			return;
		}
	}

	bool bPills = true;
	bool bNearCheckpoint = false;
	if ( bCheckpoint )
	{
		Vector mins, maxs;
		CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		mins -= Vector( 150.0f, 150.0f, 150.0f );
		maxs += Vector( 150.0f, 150.0f, 150.0f );
		bNearCheckpoint = maxs.x >= checkpointBounds.lo.x && checkpointBounds.hi.x >= mins.x &&
			maxs.y >= checkpointBounds.lo.y && checkpointBounds.hi.y >= mins.y &&
			maxs.z >= checkpointBounds.lo.z && checkpointBounds.hi.z >= mins.z;
	}
	if ( bNearCheckpoint )
		bPills = false;
	else
	{
		TerrorNavArea *pArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( WorldSpaceCenter(), false, 10000.0f, false, true, TEAM_UNASSIGNED ) );
		if ( pArea && pArea->HasSpawnAttributes( TERROR_NAV_FINALE ) )
			bPills = false;
	}
	if ( TerrorGameRules()->IsVersusMode() )
	{
		TerrorNavArea *pArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( WorldSpaceCenter(), false, 10000.0f, false, true, TEAM_UNASSIGNED ) );
		if ( pArea && pArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			bPills = false;
	}

	const int weaponID = bPills ? TERROR_WEAPON_PAIN_PILLS : TERROR_WEAPON_FIRST_AID_KIT;
	if ( !TerrorGameRules()->AllowWeaponSpawn( weaponID ) )
		return;
	const char *pszClassname = bPills ? "weapon_pain_pills_spawn" : "weapon_first_aid_kit_spawn";
	char szConverted[64];
	if ( TerrorGameRules()->GetWeaponSpawnReplacement( weaponID, szConverted, sizeof( szConverted ) ) )
		pszClassname = szConverted;

	// Preserve the original biased shuffle exactly: four inclusive draws
	// from 0..100, each modulo4, even if HealthCount will later clamp to1.
	int attachments[4] = { 1, 2, 3, 4 };
	for ( int i = 0; i < 4; ++i )
	{
		const int other = RandomInt( 0, 100 ) % 4;
		const int value = attachments[other];
		attachments[other] = attachments[i];
		attachments[i] = value;
	}
	m_pillCount = clamp( m_pillCount, 1, 4 );
	for ( int i = 0; i < m_pillCount; ++i )
	{
		char szAttachment[16];
		V_snprintf( szAttachment, sizeof( szAttachment ), "item%d", attachments[i] );
		Vector origin;
		QAngle angles;
		if ( !GetAttachment( szAttachment, origin, angles ) )
			continue;
		CBaseEntity *pEntity = CreateEntityByName( pszClassname );
		CWeaponSpawn *pSpawn = dynamic_cast< CWeaponSpawn * >( pEntity );
		if ( !pSpawn )
			continue;
		pSpawn->SetAbsOrigin( origin );
		pSpawn->SetAbsAngles( angles );
		// S:2C1770's 0x8000 is a Director hidden-cabinet-item marker, not
		// parent attachment, NODRAW, respawn delay, or invented item policy.
		pSpawn->AddSpawnFlags( 0x8000 | SF_WEAPON_SPAWN_MUST_EXIST );
		pSpawn->KeyValue( "count", "1" );
		DispatchSpawn( pSpawn );
	}
}
