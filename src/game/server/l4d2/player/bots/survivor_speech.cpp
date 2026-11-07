//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_speech.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/ui/instructor/tutor.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/infected/common/infected.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "soundent.h"
#include "team.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"


static ConVar shout_view_motion_threshold( "shout_view_motion_threshold", "0.01", 0x00004004 );
// The spelling is the original console API.
static ConVar shout_view_target_threahold( "shout_view_target_threahold", "0.01", 0x00004004 );
static ConVar shout_threat_range( "shout_threat_range", "500", 0x00004004 );
static ConVar shout_min_warn_interval( "shout_min_warn_interval", "10", 0x00004004 );
static ConVar shout_min_special_warn_interval( "shout_min_special_warn_interval", "5", 0x00004004 );
static ConVar shout_max_warn_range( "shout_max_warn_range", "2000", 0x00004004 );
static ConVar shout_min_use_range( "shout_min_use_range", "200", 0x00004004 );
static ConVar shout_make_way_interval( "shout_make_way_interval", "2", 0x00004004 );
static ConVar shout_going_to_die_interval( "shout_going_to_die_interval", "20", 0x00004004 );
static ConVarRef survivor_fog_vocalize_percent( "survivor_fog_vocalize_percent" );
static bool s_HasAnnouncedNearbyGoal;

static bool IsSurvivorTeam( int team )
{
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}

// S351280. Model variants are not zombie classes or actor IDs.
const char *GetInfectedVariantResponseName( int variant )
{
	static const char *names[] = { "ceda", "crawler", "undistractable", "fallen", "riot_control", "clown", "jimmy", "hospital_patient" };
	return variant >= 11 && variant <= 18 ? names[variant - 11] : "";
}

class CSurvivorUseTargetScan : public TargetScan< CTerrorPlayer >
{
public:
	CSurvivorUseTargetScan( CTerrorPlayer *player ) : TargetScan< CTerrorPlayer >( player, TEAM_SURVIVOR,
		shout_view_target_threahold.GetFloat(), shout_max_warn_range.GetFloat(), 50.0f, false ) {}
	bool operator()( CBasePlayer *player ) { return TargetScan< CTerrorPlayer >::operator()( static_cast< CTerrorPlayer * >( player ) ); }
protected:
	virtual bool IsPotentialTarget( CTerrorPlayer *player ) const OVERRIDE { return !player->IsGhost(); }
};

class CInfectedUseTargetScan : public TargetScan< CBaseCombatCharacter >
{
public:
	CInfectedUseTargetScan( CTerrorPlayer *player ) : TargetScan< CBaseCombatCharacter >( player, TEAM_INFECTED,
		shout_view_target_threahold.GetFloat(), shout_max_warn_range.GetFloat(), 50.0f, false ) {}
protected:
	virtual bool IsPotentialTarget( CBaseCombatCharacter *actor ) const OVERRIDE
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( actor );
		return !player || !player->IsGhost();
	}
};

// S356CF0 / S357120: ALL connected player slots, including NextBot players.
class COtherSpecialWarningScan
{
public:
	COtherSpecialWarningScan( int zombieClass, int actorID ) : m_ZombieClass( zombieClass ), m_ActorID( actorID ), m_Found( false ) {}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( IsSurvivorTeam( player->GetTeamNumber() ) && player->IsAlive() &&
			static_cast< CTerrorExpresser * >( player->GetMultiplayerExpresser() )->GetSurvivorSpeech()->GetLastWarnedActorID( m_ZombieClass ) == m_ActorID )
		{
			m_Found = true;
			return false;
		}
		return true;
	}
	bool Found() const { return m_Found; }
private:
	int m_ZombieClass;
	int m_ActorID;
	bool m_Found;
};

// S356D80 / S357590 / S357EE0. The last test is the threatened survivor's
// direction, not the speaker's direction, and the closest candidate wins.
class CLookOutThreatScan
{
public:
	CLookOutThreatScan( CTerrorPlayer *player, CTerrorPlayer *friendPlayer ) :
		m_Player( player ), m_Friend( friendPlayer ), m_Threat( NULL ), m_Range( 999999.875f ), m_InFront( false ) {}
	bool operator()( CBaseCombatCharacter *actor )
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( actor );
		if ( !actor->IsAlive() || actor->GetTeamNumber() != TEAM_INFECTED || ( player && player->IsGhost() ) )
			return true;
		Vector toFriend = m_Friend->GetAbsOrigin() - actor->GetAbsOrigin();
		float range = VectorNormalize( toFriend );
		if ( range > shout_threat_range.GetFloat() || range > m_Range )
			return true;
		Vector velocity = actor->GetAbsVelocity();
		if ( VectorNormalize( velocity ) <= 75.0f || DotProduct( velocity, toFriend ) <= 0.70709997f )
			return true;
		CTraceFilterNoNPCsOrPlayer filter( m_Player, COLLISION_GROUP_NONE );
		trace_t trace;
		UTIL_TraceLine( m_Player->EyePosition(), actor->WorldSpaceCenter(),
			MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &trace );
		if ( !trace.DidHit() )
		{
			m_Threat = actor;
			m_Range = range;
			Vector forward;
			m_Friend->EyeVectors( &forward );
			m_InFront = DotProduct( forward, toFriend ) > 0.0f;
		}
		return true;
	}
	bool IsThreatInFront() const { return m_Threat && m_InFront; }
private:
	CTerrorPlayer *m_Player;
	CTerrorPlayer *m_Friend;
	CBaseCombatCharacter *m_Threat;
	float m_Range;
	bool m_InFront;
};

CSurvivorSpeech::CSurvivorSpeech( CTerrorPlayer *player ) : m_Player( player ), m_LastEyeForward( vec3_origin ),
	m_HasJoinedScenario( false ), m_HasAnnouncedNearbyGoal( false ), m_PendingGoalConcept( NULL )
{
	// S3571A0 initializes the nine timers but DOES NOT initialize the actor-ID
	// array at +132..+167. Preserve that native first-query bookkeeping bug.
}

void CSurvivorSpeech::Reset()
{
	// S356D50, reached from Spawn. ScenarioJoin is not reset here.
	m_HasAnnouncedNearbyGoal = false;
	m_PendingGoalConcept = NULL;
	s_HasAnnouncedNearbyGoal = false;
}

bool CSurvivorSpeech::Speak( AIConcept_t concept, int priority, const char *modifiers )
{
	if ( m_Player->IsGhost() || m_Player->GetExpresser()->IsSpeaking() || m_Player->m_vocalizeTimer.GetRemainingTime() > 0.0f )
		return false;
	return m_Player->SpeakIfAllowed( concept, priority, modifiers );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool CSurvivorSpeech::WarnAboutSpecial( CBaseCombatCharacter *target )
{
	// S357850. A recognized target consumes this update even if neither the
	// tutorial nor the response can dispatch. Only the success writes state.
	Infected *infected = dynamic_cast< Infected * >( target );
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( target );
	if ( !infected && !player )
		return false;
	if ( player && ( player->IsIncapacitated() || player->IsGhost() ) )
		return false;
	int zombieClass = target->GetZombieClass();
	if ( zombieClass == 0 )
	{
		// Native IsNoticeable is 11..17, although the name table includes 18.
		if ( static_cast< unsigned int >( infected->GetGender() - 11 ) > 6 )
			return false;
		const char *variant = GetInfectedVariantResponseName( infected->GetGender() );
		if ( !variant[0] )
			return false;
		COtherSpecialWarningScan others( 0, target->GetActorID() );
		ForEachPlayer( others );
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "SpecialType:%s", variant );
		if ( Speak( others.Found() ? "PlayerAlsoWarnSpecial" : "PlayerWarnSpecial", 1, modifiers ) )
		{
			m_SpecialWarningTimer[0].Start();
			m_LastWarnedActorID[0] = target->GetActorID();
			return true;
		}
		return true;
	}
	if ( m_LastWarnedActorID[zombieClass] == target->GetActorID() ||
		!( static_cast< double >( gpGlobals->curtime ) -
			static_cast< double >( m_SpecialWarningTimer[zombieClass].GetStartTime() ) >
			static_cast< double >( shout_min_special_warn_interval.GetFloat() ) ) )
		return false;
	COtherSpecialWarningScan others( zombieClass, target->GetActorID() );
	ForEachPlayer( others );
	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "SpecialType:%s", GetZombieClassName( zombieClass ) );
	if ( TryWarnSpecialTutorial( zombieClass, target ) ||
		Speak( others.Found() ? "PlayerAlsoWarnSpecial" : "PlayerWarnSpecial", 1, modifiers ) )
	{
		m_SpecialWarningTimer[zombieClass].Start();
		m_LastWarnedActorID[zombieClass] = target->GetActorID();
		if ( zombieClass == 7 )
			m_LastWarnedSoundOwner = target;
	}
	return true;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CSurvivorSpeech::OnMoveBlocked( CBaseEntity *entity )
{
	// S356FC0: retain the first survivor contact until the update consumes it.
	if ( !IsSurvivorTeam( m_Player->GetTeamNumber() ) )
		return;
	if ( !m_BlockingPlayer.Get() && ( entity->IsPlayer() || FClassnameIs( entity, "info_transitioning_player" ) ) &&
		IsSurvivorTeam( entity->GetTeamNumber() ) )
	{
		m_BlockingPlayer = entity;
		return;
	}
	m_BlockingEntity = entity;
}

void CSurvivorSpeech::AnnounceNearbyGoal( bool finale )
{
	m_PendingGoalConcept = finale ? "PlayerNearFinale" : "PlayerNearCheckpoint";
	if ( s_HasAnnouncedNearbyGoal || Speak( m_PendingGoalConcept, 3 ) )
	{
		s_HasAnnouncedNearbyGoal = true;
		m_HasAnnouncedNearbyGoal = true;
		m_PendingGoalConcept = NULL;
	}
}

void CSurvivorSpeech::OnEnteredNavArea( CNavArea *baseArea )
{
	// S357CF0. Use actual flow, PVS cache and traces, never geometric distance.
	if ( m_HasAnnouncedNearbyGoal || m_PendingGoalConcept || !m_Player->IsAlive() ||
		m_Player->GetTeamNumber() != TEAM_SURVIVOR || !m_Player->HasLeftCheckpoint() || !baseArea )
		return;
	TerrorNavArea *area = ToTerrorNavArea( baseArea );
	if ( area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) &&
		TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance() > area->GetFlowDistance() )
		return;
	if ( area->GetFlowDistance() > 1000.0f && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT | TERROR_NAV_FINALE ) )
	{
		AnnounceNearbyGoal( area->HasSpawnAttributes( TERROR_NAV_FINALE ) );
		return;
	}
	TerrorNavArea *current = ToTerrorNavArea( m_Player->GetLastKnownArea() );
	float flow = current ? current->GetFlowDistance() : -9999.0f;
	const CUtlVector< TerrorNavArea * > &areas = m_Player->GetPotentiallyVisibleAreas();
	FOR_EACH_VEC( areas, i )
	{
		TerrorNavArea *candidate = areas[i];
		float distance = candidate->GetFlowDistance();
		if ( distance > 1000.0f && candidate->HasSpawnAttributes( TERROR_NAV_CHECKPOINT | TERROR_NAV_FINALE ) &&
			( !candidate->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) || TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance() <= distance ) &&
			flow + 1000.0f > distance && m_Player->IsInFieldOfView( candidate->GetCenter() ) &&
			candidate->IsVisibleFrom( m_Player->EyePosition() ) )
		{
			AnnounceNearbyGoal( candidate->HasSpawnAttributes( TERROR_NAV_FINALE ) );
			return;
		}
	}
}

void CSurvivorSpeech::OnNavAreaChanged( CNavArea *newArea, CNavArea *oldArea )
{
	// S357ED0 does not inspect the old area.
	OnEnteredNavArea( newArea );
}

void CSurvivorSpeech::Update()
{
	// S357F80. Contact speech precedes and is independent of the .2s eye scan.
	if ( !m_Player->IsAlive() || !IsSurvivorTeam( m_Player->GetTeamNumber() ) )
		return;
	CBaseEntity *blocker = m_BlockingPlayer.Get();
	CTerrorPlayer *blockingPlayer = blocker && blocker->IsPlayer() ? static_cast< CTerrorPlayer * >( blocker ) : NULL;
	if ( !blocker )
		m_NoPlayerBlockTimer.Start();
	else
		m_BlockingPlayer = NULL;
	CBaseEntity *blockingEntity = m_BlockingEntity.Get();
	if ( !blockingEntity )
		m_NoEntityBlockTimer.Start();
	else
		m_BlockingEntity = NULL;
	if ( blocker && !m_Player->IsSneaking() && ( m_Player->m_nButtons & ( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT ) ) &&
		m_BlockedSpeechTimer.GetRemainingTime() <= 0.0f && m_Player->GetAbsVelocity().Length() < 30.0f &&
		( m_NoPlayerBlockTimer.GetElapsedTime() > 0.5f || blockingEntity ) )
	{
		if ( !blockingPlayer )
		{
			if ( m_NoPlayerBlockTimer.GetElapsedTime() > 1.0f )
			{
				CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
				if ( checkpoint )
				{
					Vector origin;
					QAngle angles;
					checkpoint->GetSpawnPosition( &origin, &angles, NULL );
					// Native ignores failure and applies the zero-initialized outputs.
					angles.x = angles.z = 0.0f;
					blocker->SetAbsOrigin( origin );
					blocker->SetAbsAngles( angles );
				}
			}
			return;
		}
		if ( Speak( ( m_Player->m_nButtons & IN_BACK ) ? "PlayerBackUp" : "PlayerEmphaticGo" ) )
		{
			m_BlockedSpeechTimer.Start( shout_make_way_interval.GetFloat() );
			IGameEvent *event = gameeventmanager->CreateEvent( "player_blocked" );
			if ( event )
			{
				event->SetInt( "userid", m_Player->GetUserID() );
				event->SetInt( "blocker", blockingPlayer->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
			return;
		}
	}
	if ( m_TargetScanTimer.GetRemainingTime() > 0.0f || m_Player->IsSneaking() || m_Player->IsBeingPounced() || m_Player->IsTongueVictim() )
		return;
	m_TargetScanTimer.Start( 0.2f );
	Vector forward;
	m_Player->EyeVectors( &forward );
	float dot = DotProduct( m_LastEyeForward, forward );
	m_LastEyeForward = forward;
	m_LookTarget = NULL;
	m_ItemTarget = NULL;
	if ( dot <= 1.0f - shout_view_motion_threshold.GetFloat() )
	{
		m_SteadyEyeTimer.Start();
	}
	else
	{
		if ( m_SteadyEyeTimer.GetElapsedTime() < 0.2f )
			return;
		CInfectedUseTargetScan infected( m_Player );
		ForEachActor( infected );
		CBaseCombatCharacter *target = infected.GetTarget();
		if ( target && m_Player->GetFogObscuredRatio( target ) <= survivor_fog_vocalize_percent.GetFloat() )
		{
			m_LookTarget = target;
			if ( WarnAboutSpecial( target ) )
				return;
		}
		CSurvivorUseTargetScan survivor( m_Player );
		ForEachPlayer( survivor );
		m_ItemTarget = survivor.GetTarget();
		if ( m_LookOutTimer.GetRemainingTime() <= 0.0f && m_ItemTarget.Get() )
		{
			CLookOutThreatScan threat( m_Player, m_ItemTarget.Get() );
			ForEachActor( threat );
			if ( threat.IsThreatInFront() )
			{
				if ( Speak( "PlayerLookOut" ) )
					m_LookOutTimer.Start( shout_min_warn_interval.GetFloat() );
				return;
			}
		}
	}
	if ( m_HeardZombieTimer.GetRemainingTime() <= 0.0f )
	{
		m_HeardZombieTimer.Start( 10.0f );
		CSound *sound = CSoundEnt::GetLoudestSoundOfType( SOUND_DANGER, m_Player->GetAbsOrigin() );
		if ( sound && sound->SoundChannel() == 10 && sound->m_hOwner.Get() != m_LastWarnedSoundOwner.Get() )
		{
			CBaseEntity *owner = sound->m_hOwner.Get();
			CBaseCombatCharacter *actor = owner ? owner->MyCombatCharacterPointer() : NULL;
			if ( actor && actor->IsAlive() )
			{
				int zombieClass = actor->GetTeamNumber() == TEAM_INFECTED ? actor->GetZombieClass() : 9;
				if ( zombieClass != 0 && zombieClass != 9 )
				{
					char modifiers[256];
					Q_snprintf( modifiers, sizeof( modifiers ), "zombieclass:%s", GetZombieClassName( zombieClass ) );
					if ( Speak( "PlayerWarnHearZombie", 1, modifiers ) )
					{
						m_LastWarnedSoundOwner = sound->m_hOwner;
						return;
					}
				}
			}
		}
	}
	if ( m_GoingToDieTimer.GetRemainingTime() <= 0.0f && m_Player->m_isGoingToDie && !m_Player->IsIncapacitated() &&
		m_Player->GetTimeSinceCombat() > 5.0f && !m_Player->IsUseActionTarget( 1 ) && !m_Player->IsPerformingSelfUseAction( 1 ) )
	{
		if ( Speak( "PlayerGoingToDie" ) )
		{
			m_GoingToDieTimer.Start( shout_going_to_die_interval.GetFloat() );
			return;
		}
	}
	else if ( m_Player->IsIncapacitated() && m_GoingToDieTimer.GetRemainingTime() < 1.0f )
	{
		m_GoingToDieTimer.Start( 1.0f );
	}
	if ( !m_HasJoinedScenario )
	{
		CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
		char modifiers[64];
		Q_snprintf( modifiers, sizeof( modifiers ), "ScenarioName:%s", TheDirector->GetScenarioName() );
		Speak( team && team->GetNumPlayers() == 4 ? "ScenarioJoinLast" : "ScenarioJoin", 1, modifiers );
		m_HasJoinedScenario = true;
	}
	if ( m_PendingGoalConcept )
	{
		TerrorNavArea *area = ToTerrorNavArea( m_Player->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT | TERROR_NAV_FINALE ) )
		{
			m_PendingGoalConcept = NULL;
			m_HasAnnouncedNearbyGoal = true;
		}
		else if ( s_HasAnnouncedNearbyGoal || Speak( m_PendingGoalConcept ) )
		{
			s_HasAnnouncedNearbyGoal = true;
			m_PendingGoalConcept = NULL;
			m_HasAnnouncedNearbyGoal = true;
			return;
		}
	}
	if ( !m_Player->IsIncapacitated() && TheDirector->IsRescueVehicleReady() &&
		m_RescueVehicleTimer.GetRemainingTime() <= 0.0f && Speak( "PlayerGetToRescueVehicle" ) )
		m_RescueVehicleTimer.Start( 5.0f );
}

bool CSurvivorSpeech::TryUseSpeech()
{
	if ( !( m_Player->m_afButtonPressed & IN_USE ) || !IsSurvivorTeam( m_Player->GetTeamNumber() ) || m_Player->GetUseEntity() )
		return false;
	// S357B10 tests handle+280; handle+284 is a SEPARATE target.
	if ( m_LookTarget.Get() )
	{
		Speak( "PlayerLook" );
		return true;
	}
	CTerrorPlayer *target = m_ItemTarget.Get();
	if ( !target || target->GetAbsOrigin().DistToSqr( m_Player->GetAbsOrigin() ) >= 250000.0f )
		return false;
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( m_Player->GetActiveWeapon() );
	if ( !weapon )
		return false;
	int id = weapon->GetWeaponID();
	if ( id != 12 && id != 15 && id != 14 && id != 13 && id != 25 )
		return false;
	if ( !target->Weapon_SlotOccupied( weapon ) || ( ( id == 12 || id == 15 ) && !target->GetWeaponOfType( id ) ) )
	{
		Speak( "PlayerItemForYou" );
		return true;
	}
	return false;
}

bool CTerrorPlayer::TryUseSpeech()
{
	return static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() )->GetSurvivorSpeech()->TryUseSpeech();
}

void CTerrorPlayer::UpdateSurvivorSpeech()
{
	static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() )->GetSurvivorSpeech()->Update();
}

void CTerrorPlayer::ResetSurvivorSpeech()
{
	static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() )->GetSurvivorSpeech()->Reset();
}

void CTerrorPlayer::Touch( CBaseEntity *other )
{
	// S307B50; one real engine contact path, not a parallel game event.
	if ( other == this )
		return;
	static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() )->GetSurvivorSpeech()->OnMoveBlocked( other );
	if ( other && m_customAbility.Get() )
		m_customAbility->OnTouch( other );
	BaseClass::Touch( other );
}
