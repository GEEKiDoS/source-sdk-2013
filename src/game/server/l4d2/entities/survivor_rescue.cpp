//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/effects/survivor_death_model.h"
#include "l4d2/nav/terror_nav.h"
#include "BasePropDoor.h"
#include "doors.h"
#include "obstacle_pushaway.h"
#include "l4d2/nav/terror_nav_search.h"
#include "AI_Criteria.h"
#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotUtil.h"
#include "ispatialpartition.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "ndebugoverlay.h"
#endif
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_survivor_rescue, CSurvivorRescue );

IMPLEMENT_SERVERCLASS_ST( CSurvivorRescue, DT_SurvivorRescue )
	SendPropEHandle( SENDINFO( m_survivor ) ),
END_SEND_TABLE()

static ConVar rescue_range( "rescue_range", "1000", 0x00004004, "if a survivor gets this close to an active info_survivor_rescue, it will be triggered" );
static ConVar rescue_yell_delay( "rescue_yell_delay", "1", 0x00004004, "Delay before yelling when activating" );
static ConVar rescue_yell_interval( "rescue_yell_interval", "6", 0x00004004, "Interval at which an info_survivor_rescue yells for help" );
extern ConVar z_survivor_respawn_health;

// S1084B8D8 is synchronous InputRescue/Spawn state, not a body/handle cache.
static CSurvivorRescue *s_rescueSpawnOverride;
CSurvivorRescue *GetTerrorRescueSpawnOverride()
{
	return s_rescueSpawnOverride;
}

BEGIN_DATADESC( CSurvivorRescue )
	DEFINE_KEYFIELD( m_rescueEyePos, FIELD_POSITION_VECTOR, "rescueEyePos" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Rescue", InputRescue ),
	DEFINE_THINKFUNC( YellThink ),
	DEFINE_THINKFUNC( RescueThink ),
	DEFINE_THINKFUNC( AreaScanThink ),
END_DATADESC()

namespace
{

// S:0x2D1F30 stops expansion at the FIRST intersecting door, even when that
// door is a brush door that cannot become a rescue prop door. It does not
// select the nearest door and does not gather every door in the room.
bool CollectRescueArea( CNavArea *area, CUtlVector< CNavArea * > &areas, CUtlVector< CBasePropDoor * > *doors )
{
	if ( areas.HasElement( area ) ) return false;
	areas.AddToTail( area );
	Extent bounds;
	area->GetExtent( &bounds );
	bounds.hi.z += 71.0f;

	Ray_t ray;
	ray.Init( vec3_origin, vec3_origin, bounds.lo, bounds.hi );
	CBaseEntity *entities[16];
	CBotDoorEnumerator enumerator( entities, ARRAYSIZE( entities ) );
	partition->EnumerateElementsAlongRay( PARTITION_ENGINE_SOLID_EDICTS, ray, false, &enumerator );
	for ( int i = 0; i < enumerator.m_nAlreadyHit; ++i )
	{
		CBaseEntity *entity = entities[i];
		Vector low, high;
		entity->CollisionProp()->WorldSpaceSurroundingBounds( &low, &high );
		if ( high.x >= bounds.lo.x && bounds.hi.x >= low.x &&
			high.y >= bounds.lo.y && bounds.hi.y >= low.y &&
			high.z >= bounds.lo.z && bounds.hi.z >= low.z )
		{
			CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( entity );
			if ( door && doors )
				doors->AddToTail( door );
			return false;
		}
	}
	return true;
}
}

// S2CF0C0: actual native heap owner shared by runtime350 and analysis300.
void CollectSurvivorRescueAreas( CNavArea *start, float range,
	CUtlVector< CNavArea * > &areas, CUtlVector< CBasePropDoor * > *doors )
{
	if ( !start ) return;
	struct RescueCollector
	{
		CUtlVector< CNavArea * > &areas;
		CUtlVector< CBasePropDoor * > *doors;
		bool operator()( TerrorNavArea *area ) { return CollectRescueArea( area, areas, doors ); }
	} collector = { areas, doors };
	CollectSurroundingTerrorAreas( start, start->GetCenter(), collector, range, 0, TEAM_ANY );
}

CSurvivorRescue::CSurvivorRescue()
{
	m_survivor = NULL;
	m_rescueEyePos.Init();
	m_flowDistance = 0.0f;
	m_rescueCount = 0;
	ListenForGameEvent( "rescue_door_open" );
}


void CSurvivorRescue::Precache()
{
	BaseClass::Precache();
}

void CSurvivorRescue::Spawn()
{
	AddSpawnFlags( 1 );
	BaseClass::Spawn();
	m_survivor = NULL;
	m_rescueCount = 0;
	SetRenderMode( kRenderNone );
	m_flowDistance = 0.0f;
	DispatchUpdateTransmitState();
	SetContextThink( &CSurvivorRescue::AreaScanThink, gpGlobals->curtime + 1.0f, "AreaScan" );
	// S2CF850 removes the entity itself when rescue is disabled for this mode.
	if ( !TerrorGameRules()->AllowSurvivorRescue() )
		UTIL_Remove( this );
}

void CSurvivorRescue::UpdateOnRemove()
{
	ClearRescueThinks();
	SetContextThink( NULL, 0.0f, "AreaScan" );
	StopListeningForAllEvents();
	BaseClass::UpdateOnRemove();
}

int CSurvivorRescue::UpdateTransmitState()
{
	return SetTransmitState( m_survivor.Get() ? FL_EDICT_ALWAYS : FL_EDICT_DONTSEND );
}

int CSurvivorRescue::ObjectCaps()
{
	return BaseClass::ObjectCaps() & ~FCAP_IMPULSE_USE;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
Vector CSurvivorRescue::EyePosition()
{
	// S:0x2CC840. IsZero's strict component tests and the one-unit squared
	// distance test are independent, and the default forward offset is 64.
	if ( m_rescueEyePos.IsZero( 0.01f ) || ( m_rescueEyePos - GetAbsOrigin() ).LengthSqr() < 1.0f )
	{
		Vector forward;
		AngleVectors( GetAbsAngles(), &forward );
		return GetAbsOrigin() + 64.0f * forward + Vector( 0, 0, 62.0f );
	}
	return m_rescueEyePos;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CSurvivorRescue::ModifyOrAppendCriteria( AI_CriteriaSet &criteria )
{
	// The original override contributes only these two criteria, not the base
	// flex entity's identity. The waiting player's character supplies Who.
	CTerrorPlayer *survivor = m_survivor.Get();
	if ( !survivor )
		return;

	static const char *names[] =
	{
		"Gambler", "Producer", "Coach", "Mechanic", "NamVet", "TeenGirl", "Biker", "Manager"
	};
	int character = survivor->GetSurvivorCharacter();
	criteria.AppendCriteria( "Team", "Survivor", 1.0f );
	criteria.AppendCriteria( "Who", character >= 0 && character < ARRAYSIZE( names ) ? names[character] : "Unknown", 1.0f );
}

void CSurvivorRescue::ClearRescueThinks()
{
	SetContextThink( NULL, 0.0f, "Yell" );
	SetContextThink( NULL, 0.0f, "Rescue" );
}

void CSurvivorRescue::RefreshRescueTarget()
{
	CTerrorPlayer *survivor = m_survivor.Get();
	if ( survivor && ( survivor->GetTeamNumber() != TEAM_SURVIVOR || survivor->IsAlive() ) )
	{
		// S:0x2CD580 clears the handle directly, not through SetRescueTarget:
		// an already-restored player must not be put back into observer state.
		m_survivor = NULL;
		DispatchUpdateTransmitState();
	}
}

CTerrorPlayer *CSurvivorRescue::GetRescueTarget()
{
	return m_survivor.Get();
}

bool CSurvivorRescue::IsCallingForRescue()
{
	RefreshRescueTarget(); // S2CDDC0, not the raw S2CCA00 getter.
	return GetRescueTarget() != NULL;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CSurvivorRescue::SetRescueTarget( CTerrorPlayer *survivor )
{
	CTerrorPlayer *previous = m_survivor.Get();
	if ( previous == survivor )
		return;

	// S2CEAF0 neither chooses nor refreshes a newly assigned target.
	// Eligibility and explicit refresh belong to the reached caller.
	if ( previous && !survivor && previous->GetTeamNumber() == TEAM_SURVIVOR && !previous->IsAlive() )
		previous->State_Transition( STATE_OBSERVER_MODE );

	m_survivor = survivor;
	DispatchUpdateTransmitState();
	CNavArea *area = TheNavMesh->GetNearestNavArea( GetAbsOrigin(), false, 10000.0f, false, true );
	if ( survivor )
	{
		m_flowDistance = 0.0f;
		if ( area && ToTerrorNavArea( area )->GetFlowDistance() >= 0.0f )
			m_flowDistance = ToTerrorNavArea( area )->GetFlowDistance();

		float delay = rescue_yell_delay.GetFloat();
		SetContextThink( &CSurvivorRescue::YellThink, gpGlobals->curtime + delay, "Yell" );
		SetContextThink( &CSurvivorRescue::RescueThink, gpGlobals->curtime + delay + 0.5f, "Rescue" );
		DevMsg( "(RESCUE) Rescue spot at %.0f, %.0f, %.0f (area %d) activated for %s(%d).\n",
			GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z, area ? area->GetID() : -1,
			survivor->GetPlayerName(), survivor->GetUserID() );
	}
	else
	{
		ClearRescueThinks();
		DevMsg( "(RESCUE) Rescue spot at %.0f, %.0f, %.0f (area %d) deactivated.\n",
			GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z, area ? area->GetID() : -1 );
	}
}

void CSurvivorRescue::InputRescue( inputdata_t &inputdata )
{
	RefreshRescueTarget();
	if ( !GetRescueTarget() ) return;

	EmitSound( "Player.StopVoice" );
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->GetMusic()->ResetAfterRescue();
	s_rescueSpawnOverride = this;
	bool wasIdle = false;
	if ( CTerrorPlayer *target = GetRescueTarget() ) wasIdle = target->m_wasIdle;

	// Native consumers independently re-resolve; callbacks can change the selected player.
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->State_Transition( STATE_ACTIVE );
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->Spawn();
	if ( CTerrorPlayer *target = GetRescueTarget() ) CSurvivorDeathModel::RemoveForPlayer( target );
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->RemoveHiddenWeapon();

	CNavArea *area = TheNavMesh->GetNearestNavArea( GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY, false );
	DevMsg( "(RESCUE) Rescue spot at %.0f, %.0f, %.0f (area %d) now spawning.\n",
		GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z, area ? area->GetID() : -1 );
	const int health = z_survivor_respawn_health.GetInt();
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->SetHealth( health );
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->ApplyTempHealthOnlyMutation();
	if ( (unsigned int)TerrorGameRules()->GetDifficultyLevel() <= 1u )
	{
		if ( CTerrorPlayer *target = GetRescueTarget() ) target->m_survivorIntensity.Add( 5 );
	}
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->SpeakIfAllowed( "PlayerThanks", 1 );

	s_rescueSpawnOverride = NULL; // S2CDC0E: after thanks and before rescued event.
	IGameEvent *event = gameeventmanager->CreateEvent( "survivor_rescued" );
	if ( event )
	{
		CBasePlayer *rescuer = ToBasePlayer( inputdata.pActivator );
		event->SetInt( "rescuer", rescuer ? rescuer->GetUserID() : 0 );
		CTerrorPlayer *target = GetRescueTarget();
		event->SetInt( "victim", target ? target->GetUserID() : 0 );
		event->SetInt( "dooridx", inputdata.value.FieldType() == FIELD_INTEGER ? inputdata.value.Int() : 0 );
		gameeventmanager->FireEvent( event );
	}
	if ( wasIdle )
	{
		if ( CTerrorPlayer *target = GetRescueTarget() ) target->m_idleTimer.Start( 1.0f );
	}

	// Event observers see the selected target and pre-increment count.
	SetRescueTarget( NULL );
	++m_rescueCount;
}

void CSurvivorRescue::YellThink()
{
	RefreshRescueTarget();
	CTerrorPlayer *survivor = GetRescueTarget();
	if ( !survivor )
		return;

	SetContextThink( &CSurvivorRescue::YellThink, gpGlobals->curtime + rescue_yell_interval.GetFloat(), "Yell" );
	Speak( "CallForRescue" );
	IGameEvent *event = gameeventmanager->CreateEvent( "survivor_call_for_help" );
	if ( event )
	{
		CTerrorPlayer *target = GetRescueTarget();
		event->SetInt( "userid", target ? target->GetUserID() : 0 );
		event->SetInt( "subject", entindex() );
		gameeventmanager->FireEvent( event );
	}
	if ( CTerrorPlayer *target = GetRescueTarget() ) target->SetNoiseLevel( 1.0f );
}

void CSurvivorRescue::RescueThink()
{
	RefreshRescueTarget();
	if ( !GetRescueTarget() )
		return;

	SetContextThink( &CSurvivorRescue::RescueThink, gpGlobals->curtime + 0.5f, "Rescue" );
	Vector origin = GetAbsOrigin() + Vector( 0, 0, 35.5f );
	float nearestDistance = 100000000.0f;
	CTerrorPlayer *nearest = NULL;
	NextBotTraceFilterIgnoreActors filter( NULL, COLLISION_GROUP_NONE );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		// S2CD1A0 admits Disconnecting slots; S2CCEA0 has no CCS-state gate.
		if ( !player || !player->edict() || player->entindex() == 0 || !player->IsConnected() ||
			!player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ||
			player->IsIncapacitated() || player->IsBeingPounced() || player->IsTongueVictim() ||
			player->IsPummelVictim() || player->IsJockeyVictim() )
			continue;

		// S:0x2CCEA0 measures to WorldSpaceCenter, not to the player's origin
		// or eyes. Equal distances retain the first entity-index candidate.
		Vector center = player->WorldSpaceCenter();
		float distance = ( center - origin ).LengthSqr();
		if ( nearestDistance > distance )
		{
			trace_t trace;
			UTIL_TraceLine( origin, center, MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &trace );
			if ( trace.fraction >= 1.0f && !trace.startsolid )
			{
				nearestDistance = distance;
				nearest = player;
			}
		}
	}

	float range = rescue_range.GetFloat();
	if ( nearest && range * range > nearestDistance )
	{
		inputdata_t inputdata;
		inputdata.pActivator = nearest;
		inputdata.pCaller = NULL;
		inputdata.value.SetInt( 0 );
		inputdata.nOutputID = 0;
		InputRescue( inputdata );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CSurvivorRescue::AssociateDoor( CBasePropDoor *door )
{
	if ( door && door->IsDoorClosed() )
	{
		m_rescueDoors.AddToTail( door );
		door->SetRescueDoor( true );
	}
}

bool CSurvivorRescue::OwnsDoor( const CBaseEntity *door ) const
{
	FOR_EACH_VEC( m_rescueDoors, i )
	{
		if ( m_rescueDoors[i].Get() == door )
			return true;
	}
	return false;
}

bool CSurvivorRescue::HasClosedDoor() const
{
	FOR_EACH_VEC( m_rescueDoors, i )
	{
		CBasePropDoor *door = m_rescueDoors[i].Get();
		if ( door && door->IsDoorClosed() )
			return true;
	}
	return false;
}

bool CSurvivorRescue::CloseDoors()
{
	bool closed = false;
	FOR_EACH_VEC( m_rescueDoors, i )
	{
		CBasePropDoor *door = m_rescueDoors[i].Get();
		if ( door && !door->IsDoorClosed() )
		{
			closed = true;
			variant_t value;
			door->AcceptInput( "Close", NULL, NULL, value, 0 );
		}
	}
	return closed;
}

void CSurvivorRescue::OnDoorOpened( CBaseEntity *door, CBaseEntity *activator )
{
	CBasePropDoor *propDoor = dynamic_cast< CBasePropDoor * >( door );
	if ( !propDoor || !propDoor->IsRescueDoor() )
		return;

	IGameEvent *event = gameeventmanager->CreateEvent( "rescue_door_open" );
	if ( event )
	{
		CBasePlayer *player = ToBasePlayer( activator );
		event->SetInt( "userid", player ? player->GetUserID() : 0 );
		event->SetInt( "entindex", door->entindex() );
		gameeventmanager->FireEvent( event );
	}
	// The marker belongs to the door, not this spot's captured handle list.
	// It persists across an area re-scan, and each marking emits only once.
	propDoor->SetRescueDoor( false );
}

void CSurvivorRescue::FireGameEvent( IGameEvent *event )
{
	if ( Q_stricmp( event->GetName(), "rescue_door_open" ) )
		return;

	CBaseEntity *door = UTIL_EntityByIndex( event->GetInt( "entindex" ) );
	if ( !door || !OwnsDoor( door ) )
		return;

	RefreshRescueTarget();
	if ( !GetRescueTarget() )
	{
		if ( !m_rescueCount )
			m_rescueCount = 1;
		return;
	}

	inputdata_t inputdata;
	inputdata.pActivator = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	inputdata.pCaller = NULL;
	inputdata.value.SetInt( event->GetInt( "entindex" ) );
	inputdata.nOutputID = 0;
	InputRescue( inputdata );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
void CSurvivorRescue::AreaScanThink()
{
	// The final flag of the original lookup ignores nav blockers, not a team.
	CNavArea *start = TheNavMesh->GetNearestNavArea( GetAbsOrigin(), false, 350.0f, true, true, TEAM_ANY, true );
	if ( !start || !( ToTerrorNavArea( start )->GetFlowDistance() >= 0.0f ) )
	{
		DevWarning( "info_survivor_rescue at (%f, %f, %f) %s!\n", GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z,
			start ? "is in a nav area with a negative flow" : "failed to find a nav area" );
		return;
	}

	m_rescueDoors.RemoveAll();
	m_flowDistance = ToTerrorNavArea( start )->GetFlowDistance();
	CUtlVector< CNavArea * > areas;
	CUtlVector< CBasePropDoor * > doors;
	CollectSurvivorRescueAreas( start, 350.0f, areas, &doors );

	FOR_EACH_VEC( areas, i )
		ToTerrorNavArea( areas[i] )->AddSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
	FOR_EACH_VEC( doors, i )
		AssociateDoor( doors[i] );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x2cc730; 0x5c3bd0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace info_survivor_rescuePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "info_survivor_rescue", 0x5c3bd0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "info_survivor_rescue", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#include "l4d2/entities/survivor_rescue_console_server_callbacks.inc"
