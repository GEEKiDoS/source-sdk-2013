//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/survivor_death_model.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "activitylist.h"
#include "igameevents.h"
#include "l4d2/entities/visibility_monitor.h"
#include "particle_parse.h"

#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( survivor_death_model, CSurvivorDeathModel );

IMPLEMENT_SERVERCLASS_ST( CSurvivorDeathModel, DT_SurvivorDeathModel )
	SendPropInt( SENDINFO( m_nCharacterType ), 32 ),
END_SEND_TABLE()

BEGIN_DATADESC( CSurvivorDeathModel )
	DEFINE_INPUTFUNC( FIELD_VOID, "Shock", InputShock ),
END_DATADESC()

static CUtlVector< CSurvivorDeathModel * > s_SurvivorDeathModels;

static Activity SurvivorDeathActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}


// S:0x318BA0/0x318C20 register pointers for the whole object lifetime.
// Entity handles are not assigned until PostConstructor, after this constructor.
CSurvivorDeathModel::CSurvivorDeathModel()
{
	s_SurvivorDeathModels.AddToTail( this );
}

CSurvivorDeathModel::~CSurvivorDeathModel()
{
	s_SurvivorDeathModels.FindAndRemove( this );
}


int CSurvivorDeathModel::ObjectCaps()
{
	return BaseClass::ObjectCaps() | FCAP_DONT_SAVE;
}

int CSurvivorDeathModel::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

// S:0x3229E0 is the actual death helper; S:0x322B00 is only the factory.
CSurvivorDeathModel *CSurvivorDeathModel::Create( CTerrorPlayer *pPlayer )
{
	if ( !pPlayer )
		return NULL;

	CSurvivorDeathModel *pBody = static_cast< CSurvivorDeathModel * >(
		CBaseEntity::Create( "survivor_death_model", pPlayer->GetAbsOrigin(), pPlayer->GetAbsAngles() ) );
	if ( pBody )
	{
		pBody->Initialize( pPlayer );
		VisibilityMonitor_AddEntity( pBody, 400.0f, OnVisible, NULL );
	}
	return pBody;
}

void CSurvivorDeathModel::Initialize( CTerrorPlayer *pPlayer )
{
	// Register by name, never reinterpret original PE32 activity numbers as
	// target activity IDs. The body uses the player's actual mounted model.
	Activity deathActivity = SurvivorDeathActivity( pPlayer->IsIncapacitated() ? "ACT_DIE_INCAP" : "ACT_DIE_STANDING" );
	m_nCharacterType = pPlayer->GetSurvivorCharacter();
	SetModelIndex( pPlayer->GetModelIndex() );
	SetSequence( SelectWeightedSequence( deathActivity ) );
	SetPlaybackRate( 1.0f );

	// S:0x30C310 uses the DUCK hull, not the standing player hull.
	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_STEP );
	SetFriction( 1.0f );
	SetCollisionBounds( VEC_DUCK_HULL_MIN, VEC_DUCK_HULL_MAX );
	m_takedamage = DAMAGE_NO;
	SetCollisionGroup( COLLISION_GROUP_DEBRIS );
	if ( CTerrorPlayer *survivor = GetSurvivor() )
		SetAbsOrigin( survivor->GetAbsOrigin() );
	SetAbsVelocity( vec3_origin );
	AddSolidFlags( FSOLID_NOT_SOLID );
	ChangeTeam( TEAM_SURVIVOR );
	UseClientSideAnimation();
}

CTerrorPlayer *CSurvivorDeathModel::GetSurvivor() const
{
	// S:0x3054F0 ->0x350D40 ->0x303E20. Resolve the current participant
	// afresh: retail does not bind a corpse to its original connection,
	// require the player to be dead, or compare a current-body handle.
	if ( GetCharacterType() == SurvivorCharacter_Unknown )
		return NULL;

	int character = MapTerrorSurvivorCharacter( GetCharacterType() );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && pPlayer->edict() && pPlayer->entindex() != 0 && pPlayer->IsConnected() &&
			( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
			pPlayer->GetSurvivorCharacter() == character )
		{
			return pPlayer;
		}
	}
	return NULL;
}

CSurvivorDeathModel *CSurvivorDeathModel::FindForPlayer( const CTerrorPlayer *pPlayer )
{
	if ( !pPlayer )
		return NULL;

	FOR_EACH_VEC( s_SurvivorDeathModels, i )
	{
		CSurvivorDeathModel *pBody = s_SurvivorDeathModels[i];
		if ( pBody && pBody->GetSurvivor() == pPlayer )
			return pBody;
	}
	return NULL;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:0x3D2D90 uses origin distance and FInViewCone, not a new LOS/range policy.
CSurvivorDeathModel *CSurvivorDeathModel::FindNearest( CTerrorPlayer *pRescuer, float flRange )
{
	if ( !pRescuer )
		return NULL;

	CSurvivorDeathModel *pNearest = NULL;
	float flNearestSqr = flRange * flRange;
	FOR_EACH_VEC( s_SurvivorDeathModels, i )
	{
		CSurvivorDeathModel *pBody = s_SurvivorDeathModels[i];
		if ( !pBody || !pRescuer->FInViewCone( pBody->GetAbsOrigin() ) )
			continue;
		float flDistanceSqr = ( pBody->GetAbsOrigin() - pRescuer->GetAbsOrigin() ).LengthSqr();
		if ( flNearestSqr > flDistanceSqr )
		{
			flNearestSqr = flDistanceSqr;
			pNearest = pBody;
		}
	}
	return pNearest;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CSurvivorDeathModel::RemoveForPlayer( CTerrorPlayer *pPlayer )
{
	if ( !pPlayer )
		return;

	// S:0x2CD830 retires only the first character-resolved match.
	CSurvivorDeathModel *pBody = FindForPlayer( pPlayer );
	if ( pBody )
		UTIL_Remove( pBody );
}

bool CSurvivorDeathModel::Defibrillate( CTerrorPlayer *pRescuer )
{
	// Resolve the participant once, but leave the body transform and respawn
	// health reads to S:0x31F460 after the player's Spawn callbacks.
	CTerrorPlayer *pPlayer = GetSurvivor();
	if ( !pPlayer )
		return false;
	pPlayer->RestoreAfterDeath( pRescuer, this );
	return true;
}

// S:0x2FD410 is presentation only. Shock must never itself restore a player.
void CSurvivorDeathModel::InputShock( inputdata_t &inputdata )
{
	DispatchParticleEffect( "item_defibrillator_body", PATTACH_ABSORIGIN, this );
	EntityMessageBegin( this, false );
		WRITE_BYTE( 0 );
	MessageEnd();
}

// S:0x30B040 runs before the shared monitor's general entity_visible event.
bool CSurvivorDeathModel::OnVisible( CBaseEntity *pEntity, CBasePlayer *pObserver )
{
	CSurvivorDeathModel *pBody = static_cast< CSurvivorDeathModel * >( pEntity );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "dead_survivor_visible" );
	if ( pEvent )
	{
		CTerrorPlayer *pDeadPlayer = pBody->GetSurvivor();
		pEvent->SetInt( "userid", pObserver->GetUserID() );
		pEvent->SetInt( "subject", pBody->entindex() );
		pEvent->SetInt( "deadplayer", pDeadPlayer ? pDeadPlayer->GetUserID() : 0 );
		gameeventmanager->FireEvent( pEvent );
	}
	return true;
}
