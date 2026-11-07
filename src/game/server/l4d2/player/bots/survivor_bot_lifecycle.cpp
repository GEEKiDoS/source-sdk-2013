//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/weapons/terror_weapon.h"
#include "ammodef.h"
#include "nav_mesh.h"
#include "NextBot/NextBotManager.h"
#include "tier0/memdbgon.h"

extern ConVar bot_mimic;
extern ConVar sb_perf_crawl;
extern ConVar sb_perf_dump;
extern ConVar sb_perf_dump_bots;
extern ConVar sb_perf_crawl_dist;
extern ConVar sb_perf_crawl_ang;
extern ConVar sb_perf_crawl_time;
extern bool IsInCommentaryMode();

ConVar sb_replacement_interval( "sb_replacement_interval", "0", 0x00004004,
	"Force SurvivorBots to be replaced after this many seconds for stress testing" );
ConVar sb_stop( "sb_stop", "0", 0x00004004, "Forces survivor bots to stand still" );
ConVar sb_crouch( "sb_crouch", "0", 0x00004004, "Forces survivor bots to crouch" );
ConVar sb_open_fire( "sb_open_fire", "0", 0x00004004, "Forces survivor bots to fire continuously" );
ConVar sb_move( "sb_move", "1", 0x00004004, "Stop all SurvivorBots from moving" );
ConVar sb_dont_bash( "sb_dont_bash", "0", 0x00004004, "Force SurvivorBots to not bash" );
ConVar sb_flashlight( "sb_flashlight", "0", 0x00004004, "Forces survivor bots to use flashlights (-1 to force off)" );
extern ConVar sb_narrow_corridor_width;
ConVar sb_show_threat_areas( "sb_show_threat_areas", "0", 0x00004004 );

// S38F450 extends an existing suppression, never shortens it.
void SurvivorBot::SuppressFlashlight( float duration )
{
	if ( !m_witchFlashlightTimer.HasStarted() || duration > m_witchFlashlightTimer.GetRemainingTime() )
		m_witchFlashlightTimer.Start( duration );
}

// S399800. Reservation is after the look/reload/witch passes and before the
// ordinary stop/action branch. The initial-checkpoint reservation path runs
// player physics, not behavior, while the engine is still loading the human.
void SurvivorBot::PhysicsSimulate()
{
	UpdateAreaClear();
	ReloadCombatWeapon();
	if ( TheDirector->GetWitchCount() > 0 )
		SuppressFlashlight( RandomFloat( 30.0f, 45.0f ) );
	if ( m_humanSpectatorUserID <= 0 )
	{
		if ( sb_replacement_interval.GetFloat() > 0.0f &&
			m_creationTimer.GetElapsedTime() > sb_replacement_interval.GetFloat() )
			ReplaceWithBot( true );
	}
	else if ( UpdateTransitionReservation() )
	{
		if ( TheDirector->HasAnySurvivorLeftSafeArea() ) BaseClass::PhysicsSimulate();
		else CBasePlayer::PhysicsSimulate();
		return;
	}

	if ( !m_positionEntity.Get() && sb_stop.GetInt() == 0 &&
		!NextBotStop.GetBool() && !( GetFlags() & FL_FROZEN ) )
	{
		INextBot *selected = TheNextBots().GetSelected();
		if ( !selected || selected->IsSelf( this ) )
		{
			if ( sb_crouch.GetBool() ) PressCrouchButton();
			if ( sb_open_fire.GetBool() )
			{
				if ( m_alternateForcedFire ) ReleaseFireButton();
				else PressFireButton();
				m_alternateForcedFire = !m_alternateForcedFire;
			}
		}
		if ( !sb_perf_crawl.GetBool() || !UpdatePerformanceCapture() )
		{
			BaseClass::PhysicsSimulate();
			return;
		}
		if ( GetCommandContextCount() > 0 ) return;
	}
	else
	{
		if ( sb_stop.GetInt() < 0 || gpGlobals->frametime == 0.0f ) return;
		CBasePlayer::PhysicsSimulate();
		if ( GetCommandContextCount() > 0 ) return;
	}
	CUserCmd command;
	_NextBot_BuildUserCommand( &command, EyeAngles(), 0.0f, 0.0f, 0.0f, 0, 0 );
	ProcessUsercmds( &command, 1, 1, 0, false );
}

// S39D7A0: the full per-AI-update producer, not just INextBot::Update.
void SurvivorBot::Update()
{
	VPROF_BUDGET( "SurvivorBot::Update", "SurvivorBot::Update" );
	if ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR )
	{
		engine->ServerCommand( CFmtStr( "kick \"%s\"\n", GetPlayerName() ) );
		return;
	}
	if ( !IsAlive() || bot_mimic.GetBool() ) return;
	CBaseCombatWeapon *primary = Weapon_GetSlot( 0 );
	if ( primary )
	{
		int ammoType = primary->GetPrimaryAmmoType();
		m_isLowOnAmmo = primary->Clip1() + GetAmmoCount( ammoType ) < GetAmmoDef()->MaxCarry( ammoType, this ) / 3;
	}
	else m_isLowOnAmmo = false;
	EnforceHumanProximity();
	m_teamSituation.Update();
	if ( m_resetIntention )
	{
		m_intention->Reset();
		m_resetIntention = false;
	}
	BaseClass::Update();
	if ( !GetLastKnownArea() )
	{
		CNavArea *area = TheNavMesh->GetNearestNavArea( GetAbsOrigin(), false, 10000.0f, false, true );
		if ( area ) m_locomotor->Approach( area->GetCenter(), 1.0f );
	}
	const SurvivorKnownEntity *known = m_intention->GetPrimaryKnownThreat();
	if ( known && known->GetEntity() && known->GetEntity()->IsAlive() && known->GetEntity()->GetZombieClass() == 7 )
		m_witchFlashlightTimer.Start( 1.0f );
	if ( sb_flashlight.GetInt() == 1 || ( sb_flashlight.GetInt() != -1 &&
		m_witchFlashlightTimer.GetRemainingTime() <= 0.0f && HasLeftCheckpoint() ) )
		FlashlightTurnOn();
	else FlashlightTurnOff();
	if ( sb_show_threat_areas.GetBool() )
		FOR_EACH_VEC( m_unclearedAreas, i ) m_unclearedAreas[i]->DrawFilled( 255, 0, 0, 128, 0.01023f, true );
	if ( !sb_move.GetBool() )
	{
		ReleaseForwardButton(); ReleaseBackwardButton();
		ReleaseLeftButton(); ReleaseRightButton(); ReleaseJumpButton();
	}
	if ( sb_dont_bash.GetBool() ) ReleaseAltFireButton();
	Vector motion = m_locomotor->GetMotionVector();
	motion.z = 0.0f;
	VectorNormalize( motion );
	Vector across( -motion.y, motion.x, 0.0f );
	across *= sb_narrow_corridor_width.GetFloat() * 0.5f;
	const Vector &feet = m_locomotor->GetFeet();
	if ( IsDebugging( NEXTBOT_LOCOMOTION ) )
		NDebugOverlay::HorzArrow( feet - across, feet + across, 2.0f, 255, 0, 255, 128, true, 0.1f );
	if ( m_locomotor->IsPotentiallyTraversable( feet, feet + across, ILocomotion::IMMEDIATELY ) ||
		m_locomotor->IsPotentiallyTraversable( feet, feet - across, ILocomotion::IMMEDIATELY ) )
		m_isInNarrowCorridor = m_locomotor->HasPotentialGap( feet, feet + across ) &&
			m_locomotor->HasPotentialGap( feet, feet - across );
	else m_isInNarrowCorridor = true;
	UpdateHumanAttention();
	UpdatePendingSpeech();
}

// S397410. Removing with FastRemove intentionally changes the next target.
void SurvivorBot::UpdateAreaClear()
{
	if ( !IsSafeForCombatAction() || IsInCombat( false ) ) return;
	CNavArea *current = GetLastKnownArea();
	if ( current && m_unclearedAreas.Count() && m_lookAroundTimer.GetRemainingTime() <= 0.0f )
	{
		for ( int i = 0; i < m_unclearedAreas.Count(); ++i )
			if ( !current->IsPotentiallyVisible( m_unclearedAreas[i] ) ) m_unclearedAreas.FastRemove( i-- );
		m_lookAroundTimer.Start( 1.0f );
	}
	if ( !m_unclearedAreas.Count() ) { LookAround(); return; }
	CNavArea *target = m_unclearedAreas.Tail();
	float eyeHeight = m_body->GetStandHullHeight() * 0.75f;
	Vector targetPoint = target->GetCenter() + Vector( 0, 0, eyeHeight );
	float dwell = clamp( sqrtf( target->GetSizeX() * target->GetSizeY() ) * 0.005f, 0.0f, 1.0f );
	m_body->AimHeadTowards( targetPoint, IBody::INTERESTING, 2.0f, NULL, "Clearing an area", true, 0.25f );
	const Vector &view = m_body->GetViewVector();
	for ( int i = 0; i < m_unclearedAreas.Count(); ++i )
	{
		Vector to = m_unclearedAreas[i]->GetCenter() + Vector( 0, 0, eyeHeight ) - EyePosition();
		float range = VectorNormalize( to );
		if ( range >= 100.0f && ( DotProduct( view, to ) <= 0.8f || m_body->GetHeadSteadyDuration() <= dwell ) ) continue;
		m_unclearedAreas.FastRemove( i-- );
	}
}

void SurvivorBot::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );
	if ( m_humanSpectatorUserID <= 0 ) return;
	CTerrorPlayer *human = ToTerrorPlayer( UTIL_PlayerByUserId( m_humanSpectatorUserID ) );
	if ( !human || !human->IsPlayer() ) return;
	SurvivorCharacterType character = GetSurvivorCharacter();
	HandleCommand_JoinTeam( TEAM_SPECTATOR, SurvivorCharacter_Unknown, false );
	human->m_wasIdle = human->GetTeamNumber() == TEAM_SPECTATOR;
	bool wasIdle = human->m_wasIdle;
	human->HandleCommand_JoinTeam( TEAM_SURVIVOR, character, false );
	if ( wasIdle ) human->m_wasIdle = true;
	ClearHumanSpectator();
}

void SurvivorBot::OnTeamChanged( int oldTeam )
{
	BaseClass::OnTeamChanged( oldTeam );
	bool wasSurvivor = oldTeam == TEAM_SURVIVOR || oldTeam == TEAM_L4D1_SURVIVOR || oldTeam == TEAM_UNASSIGNED;
	bool isSurvivor = GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR || GetTeamNumber() == TEAM_UNASSIGNED;
	if ( wasSurvivor && !isSurvivor )
		engine->ServerCommand( CFmtStr( "kickid %d survivor bot left the survivor team\n", GetUserID() ) );
}

static float s_perfCrawlTime;
static int s_perfCrawlAngle;
static int s_perfCrawlState;
static Vector s_perfCrawlPosition;

// S38B690, the actual sb_perf_crawl change callback.
static void SurvivorPerfCrawlChanged( IConVar *variable, const char *oldValue, float oldFloat )
{
	s_perfCrawlPosition.Init();
	s_perfCrawlTime = gpGlobals->curtime;
	s_perfCrawlAngle = 0;
	s_perfCrawlState = 0;
}

ConVar sb_perf_dump( "sb_perf_dump", "0", 0x00006004,
	"If true, low perf places will be dumped" );
ConVar sb_perf_dump_bots( "sb_perf_dump_bots", "", 0x00006004,
	"Which bots to use for dumping: namvet, girl, biker, manager" );
ConVar sb_perf_crawl( "sb_perf_crawl", "0", 0x00006004,
	"If true, survivor bot will be doing a perf-crawl walk through the map", SurvivorPerfCrawlChanged );
ConVar sb_perf_crawl_dist( "sb_perf_crawl_dist", "128", 0x00004004, "Distance between perf-crawl snapshots in the map" );
ConVar sb_perf_crawl_ang( "sb_perf_crawl_ang", "5", 0x00004004, "Rotation angles at perf-crawl snapshots in the map" );
ConVar sb_perf_crawl_time( "sb_perf_crawl_time", "1.0", 0x00004004, "How much time is spent at each perf-crawl spot/ang in the map" );

bool SurvivorBot::UpdatePerformanceCapture()
{
	const char *filter = sb_perf_dump_bots.GetString();
	unsigned int character = static_cast< unsigned int >( GetSurvivorCharacter() );
	if ( filter && *filter && ( character >= 4 || Q_strlen( filter ) <= character ||
		strchr( "+1Xx", filter[character] ) == NULL ) )
		return sb_perf_dump.GetBool();
	if ( s_perfCrawlState == 0 )
	{
		if ( ( m_body->GetEyePosition() - s_perfCrawlPosition ).Length() > sb_perf_crawl_dist.GetFloat() )
		{
			s_perfCrawlState = 1;
			s_perfCrawlPosition = m_body->GetEyePosition();
			s_perfCrawlTime = gpGlobals->curtime;
			sb_perf_dump.SetValue( 1 );
		}
		return false;
	}
	if ( s_perfCrawlState != 1 ) return false;
	if ( gpGlobals->curtime > sb_perf_crawl_time.GetFloat() + s_perfCrawlTime )
	{
		if ( ++s_perfCrawlAngle > sb_perf_crawl_ang.GetInt() )
		{
			s_perfCrawlState = 0;
			s_perfCrawlPosition = m_body->GetEyePosition();
			s_perfCrawlAngle = 0;
			sb_perf_dump.SetValue( 0 );
			return false;
		}
		s_perfCrawlTime = gpGlobals->curtime;
		const Vector &eye = m_body->GetEyePosition();
		const Vector &view = m_body->GetViewVector();
		float angle = 2.0 * M_PI / sb_perf_crawl_ang.GetInt();
		float cosine = cos( angle ), sine = sin( angle );
		Vector target( eye.x + ( view.x * cosine - view.y * sine ) * 100.0f,
			eye.y + ( view.x * sine + view.y * cosine ) * 100.0f,
			eye.z + view.z * 100.0f );
		m_body->AimHeadTowards( target, IBody::CRITICAL, sb_perf_crawl_time.GetFloat() * 2.0f, NULL, NULL, true, 0.25f );
	}
	m_body->Update();
	CBasePlayer::PhysicsSimulate();
	return true;
}
