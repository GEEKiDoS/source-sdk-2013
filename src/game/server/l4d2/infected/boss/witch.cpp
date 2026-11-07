//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/infected/boss/witch_locomotion.h"
#include "l4d2/infected/boss/witch_body.h"
#include "l4d2/infected/boss/witch_vision.h"
#include "l4d2/infected/boss/witch_intention.h"
#include "l4d2/infected/boss/witch_behavior.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "particle_parse.h"
#include "igameevents.h"
#include "NextBot/NextBotVisionInterface.h"
#include "ispatialpartition.h"
#include "BasePropDoor.h"
#include "usermessages.h"
#include "tier1/utlstring.h"

#include "l4d2/terror_shareddefs.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/infected/boss/witch_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar z_witch_health( "z_witch_health", "1000", 0x00004004, "Witch max health" );
ConVar z_witch_threat_normal_range( "z_witch_threat_normal_range", "300", 0x00004004 );
ConVar z_witch_flashlight_range( "z_witch_flashlight_range", "400", 0x00004004 );
ConVar z_witch_personal_space( "z_witch_personal_space", "100", 0x00004004 );
ConVar z_witch_attack_range( "z_witch_attack_range", "60", 0x00004004 );
extern ConVar z_witch_always_kills;
extern bool IsInCommentaryMode();

namespace
{
	CUtlVector< Witch * > s_Witches;
	CUtlVector< CUtlString > s_WitchModels;

	struct MarkWitchAreas
	{
		bool operator()( TerrorNavArea *area )
		{
			area->MarkAsDamaging( 1.0f );
			return true;
		}
	};
}

LINK_ENTITY_TO_CLASS( witch, Witch );
IMPLEMENT_SERVERCLASS_ST( Witch, DT_Witch )
	SendPropFloat( SENDINFO( m_rage ), 8, SPROP_ROUNDDOWN, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( m_wanderrage ), 8, SPROP_ROUNDDOWN, 0.0f, 1.0f ),
END_SEND_TABLE()

const CUtlVector< Witch * > &GetWitchList()
{
	return s_Witches;
}

void ClearWitchList()
{
	s_Witches.RemoveAll();
}

// S:0x41C310. The inherited attention remaining ratio is a different field.
Witch::Witch()
{
	m_rage = 0.0f;
	m_wanderrage = 0.0f;
	m_ragePoseParameter = -1;
	m_rageRampStart = -1.0f;
	m_rageRampDuration = 0.0f;
	m_harasser = NULL;
	m_pendingHarasser = NULL;
	m_firstDamageTime = -1.0f;
	m_survivorDamageTypeMask = 0;
	s_Witches.AddToTail( this );
}

Witch::~Witch()
{
	if ( TheDirector )
		TheDirector->OnWitchRemoved();
	s_Witches.FindAndRemove( this );
}

int Witch::GetZombieClass() const
{
	return 7;
}

// S41B0A0: versus witches do not use the common-infected team-mask gate.
bool Witch::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	if ( TerrorGameRules()->IsVersusMode() )
		return CBaseEntity::ShouldCollide( collisionGroup, contentsMask );
	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

// S:0x41BC30 deliberately calls the combat-character precache, not the common
// population/model/wound precache. The optional variant belongs to the chapter.
void Witch::Precache()
{
	CBaseCombatCharacter::Precache();
	if ( GetMaxHealth() != 0 )
		return;

	s_WitchModels.RemoveAll();
	PrecachePlayerModelAndMaterials( this, "models/infected/witch.mdl", "WitchVariant", &s_WitchModels );
	PrecacheParticleSystem( "witch_eye_glow" );
	static const char *const sounds[] =
	{
		"WitchZombie.Despair", "WanderWitchZombie.Despair", "WitchZombie.Scream",
		"WitchZombie.Rage", "WitchZombie.Pain", "WitchZombie.PainFire",
		"WitchZombie.Surprised", "WitchZombie.GrowlLow", "WitchZombie.GrowlMedium",
		"WitchZombie.GrowlHigh", "WitchZombie.Die", "WitchZombie.HeadShot",
		"WitchZombie.ShredVictim", "WitchZombie.RetreatHorror",
		"WitchZombie.RageBeforeKillingFrenzy", "WitchZombie.KillingFrenzy"
	};
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i )
		PrecacheScriptSound( sounds[i] );
}

void Witch::Spawn()
{
	SetModel( ( GetInfectedFlags() & 0x1000 ) ? "models/infected/witch_bride.mdl" : s_WitchModels[0].String() );
	BaseClass::Spawn();
	SetHealth( (int)z_witch_health.GetFloat() );
	SetMaxHealth( (int)z_witch_health.GetFloat() );
	RemoveFlag( FL_AIMTARGET );
	if ( TheDirector )
		TheDirector->OnWitchSpawn();

	IGameEvent *event = gameeventmanager->CreateEvent( "witch_spawn" );
	if ( event )
	{
		event->SetInt( "witchid", entindex() );
		gameeventmanager->FireEvent( event );
	}
}

void Witch::CreateComponents() const
{
	Witch *me = const_cast< Witch * >( this );
	if ( !m_locomotion ) m_locomotion = new WitchLocomotion( me );
	if ( !m_body ) m_body = new WitchBody( me );
	if ( !m_intention ) m_intention = new WitchIntention( me );
	if ( !m_vision ) m_vision = new WitchVision( me );
}

void Witch::OnModelChanged()
{
	INextBotEventResponder::OnModelChanged();
	m_ragePoseParameter = LookupPoseParameter( "rage" );
}

// S:0x41B140. No extra countdown, completion callback, or timeout owns rage.
void Witch::StartRageRamp( float duration )
{
	m_rageRampStart = gpGlobals->curtime;
	m_rageRampDuration = duration;
}

#pragma float_control( precise, on, push )
void Witch::ChangeRage( float amount )
{
	const float previousRage = m_rage;
	float rage = m_rage + amount;
	if ( rage > 1.0f ) rage = 1.0f;
	else if ( rage < 0.0f ) rage = 0.0f;
	m_rage = rage;
	if ( previousRage < 0.5f && m_rage >= 0.5f )
	{
		// S41C646 -> S2958D0: direct TEAM2 speech, not the delayed concept queue.
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() ||
				!player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
			static_cast< CTerrorPlayer * >( player )->SpeakIfAllowed( "WitchGettingAngry", 1, NULL, NULL, 0, NULL );
		}
	}
}
#pragma float_control( pop )

void Witch::SetWanderRage( float rage )
{
	m_wanderrage = rage;
}

// S:0x41B990. Replacing an existing harasser still emits the native event;
// assigning NULL only clears the handle and never fabricates a release event.
void Witch::SetHarasser( CBaseEntity *harasser )
{
	const bool first = m_harasser.Get() == NULL;
	m_harasser = harasser;
	CBasePlayer *player = ToBasePlayer( harasser );
	if ( !player )
		return;
	IGameEvent *event = gameeventmanager->CreateEvent( "witch_harasser_set" );
	if ( event )
	{
		event->SetInt( "userid", player->GetUserID() );
		event->SetInt( "witchid", entindex() );
		event->SetBool( "first", first );
		gameeventmanager->FireEvent( event );
	}
}

void Witch::Update()
{
	BaseClass::Update();
	if ( m_rageRampStart > 0.0f )
	{
		float rage = RemapValClamped( gpGlobals->curtime, m_rageRampStart,
			m_rageRampStart + m_rageRampDuration, 0.0f, 1.0f );
		if ( rage <= m_rage ) rage = m_rage;
		ChangeRage( rage - m_rage );
		if ( rage >= 1.0f )
		{
			if ( m_pendingHarasser.Get() )
			{
				SetHarasser( m_pendingHarasser.Get() );
				m_pendingHarasser = NULL;
			}
			INextBotEventResponder::OnCommandAttack( m_harasser.Get() );
			m_rageRampStart = -1.0f;
			m_rageRampDuration = 0.0f;
		}
	}
	SetPoseParameter( m_ragePoseParameter, m_rage );
	MarkWitchAreas mark;
	const float range = TheDirector->GetWitchCount() > 1 ? 120.0f : z_witch_threat_normal_range.GetFloat();
	CollectSurroundingTerrorAreas( GetLastKnownArea(), GetAbsOrigin(), mark, range, 0, TEAM_ANY );
}

// S:0x41C670 bypasses the common infected's armor/damage conversion. NextBot's
// actual responder callback still precedes native combat-character HP mutation.
int Witch::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	const int oldHealth = GetHealth();
	const int result = NextBotCombatCharacter::OnTakeDamage_Alive( info );
	const int newHealth = GetHealth();
	CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
	const int userID = attacker ? attacker->GetUserID() : -1;
	if ( m_firstDamageTime == -1.0f )
		m_firstDamageTime = gpGlobals->curtime;
	if ( m_damageUserIDs.Find( userID ) == m_damageUserIDs.InvalidIndex() )
		m_damageUserIDs.AddToTail( userID );
	if ( attacker )
	{
		CTerrorPlayer *terror = static_cast< CTerrorPlayer * >( attacker );
		if ( terror->GetTeamNumber() == TEAM_SURVIVOR || terror->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			m_survivorDamageTypeMask |= info.GetDamageType();
		terror->m_checkpointDamageToWitch += oldHealth - newHealth;
	}
	return result;
}

void Witch::EmitTraceAttackBlood( float amount, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace )
{
	CBaseEntity::TraceBleed( amount, direction, trace, info.GetDamageType() );
	QAngle angles;
	VectorAngles( -direction, angles );
	DispatchParticleEffect( "blood_impact_witch_01", trace->endpos, GetAbsVelocity(), angles );
}

Action< Infected > *Witch::CreateDyingAction( const CTakeDamageInfo &info )
{
	// Unlike Infected::CreateDyingAction, the original Witch override does not
	// write the common HasStartedDying flag before constructing its action.
	return new WitchDying( info );
}

// S:0x41AF50. Recent weapon fire has its own started-interval predicate; the
// elapsed-time query's unset/previous-fire fallback is deliberately not used.
bool Witch::IsHostileTo( CBaseEntity *subject ) const
{
	CTerrorPlayer *player = ToTerrorPlayer( subject );
	if ( !player || player->GetTeamNumber() == TEAM_INFECTED ||
		( IsInCommentaryMode() && !player->IsBot() ) )
		return false;
	if ( player->FlashlightIsOn() && player->IsLookingTowards( this, 0.98f ) &&
		IsRangeLessThan( player, z_witch_flashlight_range.GetFloat() ) &&
		GetVisionInterface()->IsAbleToSee( player, IVision::USE_FOV ) )
		return true;
	if ( player->IsRecentlyAttacking() )
		return true;
	return IsRangeLessThan( player, z_witch_personal_space.GetFloat() );
}

#pragma float_control( precise, on, push )
void Witch::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );
	NotifyZombieDeath( this, 7 );

	CTerrorPlayer *attacker = dynamic_cast< CTerrorPlayer * >( info.GetAttacker() );
	const int userID = attacker ? attacker->GetUserID() : -1;
	const bool oneShot = m_damageUserIDs.Find( userID ) != m_damageUserIDs.InvalidIndex() &&
		m_damageUserIDs.Count() == 1 && m_firstDamageTime + 0.1f > gpGlobals->curtime;

	bool meleeOnly = false;
	if ( m_survivorDamageTypeMask != 0 )
	{
		m_survivorDamageTypeMask &= 0x3F9FFFFB;
		meleeOnly = attacker != NULL && m_survivorDamageTypeMask == 0;
	}
	m_survivorDamageTypeMask = 0;

	IGameEvent *event = gameeventmanager->CreateEvent( "witch_killed" );
	if ( event )
	{
		event->SetInt( "userid", userID );
		event->SetInt( "witchid", entindex() );
		event->SetBool( "oneshot", oneShot );
		event->SetBool( "melee_only", meleeOnly );
		event->SetBool( "bride", GetGender() == 19 );
		gameeventmanager->FireEvent( event );
	}

	if ( attacker )
	{
		const Vector delta = attacker->GetAbsOrigin() - GetAbsOrigin();
		const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "SubjectIsA:%s,Distance:%f,OneShot:%s",
			GetZombieClassName( GetZombieClass() ), static_cast< double >( distance ), oneShot ? "1" : "0" );
		attacker->SpeakIfAllowed( "KilledZombie", 1, modifiers );
	}
}
#pragma float_control( pop )

// Native server 0x41aee0; 0x5c5488 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace witchPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "witch", 0x5c5488 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "witch", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
