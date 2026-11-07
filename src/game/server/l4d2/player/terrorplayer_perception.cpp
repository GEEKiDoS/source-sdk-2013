//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/weapons/terror_weapon.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotBodyInterface.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "l4d2/nav/terror_nav_area.h"
#include "in_buttons.h"
#include "recipientfilter.h"
#include "tier0/memdbgon.h"

static ConVar intensity_factor( "intensity_factor", "0.25", 0x00004004, "How quickly intensity increases" );
static ConVar intensity_averaged_following_decay( "intensity_averaged_following_decay", "20", 0x00004004, "Seconds for the 'time averaged intensity' to meet the baseline intensity" );
static ConVar intensity_decay_time( "intensity_decay_time", "30", 0x00004004, "Seconds to decay full intensity to zero" );
static ConVar intensity_lock( "intensity_lock", "-1", 0x00004004, "Lock players' intensities at this value" );
static ConVar survivor_calm_intensity( "survivor_calm_intensity", "0.25", 0x00004004,
	"survivors are not calm if their intensity level is above this point" );
static ConVar survivor_calm_recent_enemy_delay( "survivor_calm_recent_enemy_delay", "5", 0x00004004,
	"survivors are not calm for this many seconds after seeing an enemy" );
static ConVar survivor_calm_no_flashlight( "survivor_calm_no_flashlight", "1", 0x00004004,
	"survivors are not calm if they have their flashlight on, to keep the weapon pointing along the flashlight beam" );
static ConVar survivor_intensity_decay_threat_range( "survivor_intensity_decay_threat_range", "750", 0x00004004 );
static ConVar z_alert_dot( "z_alert_dot", "0.3", 0x00004004 );
extern ConVar still_vel_limit;
extern ConVar still_view_accel_limit;
static ConVar z_last_man_run_interval( "z_last_man_run_interval", "3", 0x00004004 );
static ConVar z_run_exertion_interval( "z_run_exertion_interval", "1", 0x00004004 );
ConVar sb_neighbor_range( "sb_neighbor_range", "300", 0x00004004, "How close a friend needs to be to feel safe" );
static ConVar z_infected_flashlight( "z_infected_flashlight", "1", 0x00004004 );
extern ConVar flashlight;

static bool IsPerceptionSurvivorTeam( int team )
{
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S29C910: the two timer members construct before these body stores.
CSurvivorIntensity::CSurvivorIntensity()
{
	Reset();
}

// S29C6F0. The hold countdown is deliberately not reset.
void CSurvivorIntensity::Reset()
{
	m_current = 0.0f;
	m_timeAveraged = 0.0f;
	const double now = static_cast< double >( gpGlobals->curtime );
	if ( static_cast< double >( m_updateTimer.m_timestamp.Get() ) != now )
		m_updateTimer.m_timestamp.SetDirect( static_cast< float >( now ) );
}

// S29C670 compares the unspilled remaining time and deadline before float stores.
void CSurvivorIntensity::Hold( float duration )
{
	const double remaining = static_cast< double >( m_holdTimer.m_timestamp.Get() ) -
		static_cast< double >( gpGlobals->curtime );
	if ( static_cast< double >( duration ) > remaining )
	{
		const double deadline = static_cast< double >( gpGlobals->curtime ) +
			static_cast< double >( duration );
		if ( static_cast< double >( m_holdTimer.m_timestamp.Get() ) != deadline )
			m_holdTimer.m_timestamp.SetDirect( static_cast< float >( deadline ) );
		if ( m_holdTimer.m_duration.Get() != duration )
			m_holdTimer.m_duration.SetDirect( duration );
	}
}

// S29C860: MULSS, ADDSS, upper-only cap, Hold, then one lock-value read.
void CSurvivorIntensity::Add( int severity )
{
	float increase = 0.0f;
	switch ( severity )
	{
	case 1: increase = 0.05f; break;
	case 2: increase = 0.2f; break;
	case 3: increase = 0.5f; break;
	case 4: increase = 1.0f; break;
	case 5: increase = 999999.88f; break;
	}
	const float scaledIncrease = intensity_factor.GetFloat() * increase;
	m_current = scaledIncrease + m_current;
	if ( m_current > 1.0f )
		m_current = 1.0f;
	Hold( 5.0f );
	const float lock = intensity_lock.GetFloat();
	if ( lock >= 0.0f )
	{
		m_current = lock;
		m_timeAveraged = lock;
	}
}

// S29C740 retains the native spills, ordered hold gate, and unordered log branch.
void CSurvivorIntensity::Update()
{
	const float elapsed = m_updateTimer.m_timestamp.Get() > 0.0f ?
		static_cast< float >( static_cast< double >( gpGlobals->curtime ) -
			static_cast< double >( m_updateTimer.m_timestamp.Get() ) ) : 99999.9f;
	const double now = static_cast< double >( gpGlobals->curtime );
	if ( static_cast< double >( m_updateTimer.m_timestamp.Get() ) != now )
		m_updateTimer.m_timestamp.SetDirect( static_cast< float >( now ) );
	if ( static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( m_holdTimer.m_timestamp.Get() ) )
	{
		const float decay = elapsed / intensity_averaged_following_decay.GetFloat();
		m_current -= decay;
		if ( m_current < 0.0f )
			m_current = 0.0f;
	}
	const float difference = m_current - m_timeAveraged;
	if ( difference < 0.000001f )
		m_timeAveraged = m_current;
	else
		m_timeAveraged += exp( log( static_cast< double >( difference ) ) /
			intensity_decay_time.GetFloat() * elapsed );
	const float lock = intensity_lock.GetFloat();
	if ( lock >= 0.0f )
	{
		m_current = lock;
		m_timeAveraged = lock;
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S306D40. JA skips only ordered greater; equality and unordered extend.
void CTerrorPlayer::DelayCalm( float duration )
{
	const double timestamp = m_calmWeaponTimer.m_timestamp.Get();
	if ( timestamp > 0.0 && timestamp - static_cast< double >( gpGlobals->curtime ) > duration )
		return;
	const double deadline = static_cast< double >( gpGlobals->curtime ) + static_cast< double >( duration );
	const float storedDeadline = static_cast< float >( deadline );
	if ( static_cast< double >( m_calmWeaponTimer.m_timestamp.Get() ) != deadline )
		m_calmWeaponTimer.m_timestamp.GetForModify() = storedDeadline;
	if ( m_calmWeaponTimer.m_duration.Get() != duration )
		m_calmWeaponTimer.m_duration.GetForModify() = duration;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// S3492A0. The mission/intro override precedes all injury and weapon gates.
// The separate S2C8E90 call after m_isGoingToDie in the binary is unreachable:
// it returns false whenever that same byte is false. Do not evaluate the
// getter-driven health-buffer decay while reproducing that short circuit.
bool CTerrorPlayer::ShouldBeCalm()
{
	if ( !IsPerceptionSurvivorTeam( GetTeamNumber() ) )
		return false;
	if ( TheDirector->IsPlayingIntro() )
		return true;
	if ( IsIncapacitated() || m_isGoingToDie ||
		( m_calmWeaponTimer.m_timestamp.Get() > 0.0f && m_calmWeaponTimer.GetRemainingTime() > 0.0f ) )
		return false;
	if ( GetIntensity() >= survivor_calm_intensity.GetFloat() ||
		GetTimeSinceEnemySeen() < survivor_calm_recent_enemy_delay.GetFloat() )
		return false;
	// S243630 is precisely the real EF_DIMLIGHT bit reader. It must not call
	// SDK CBasePlayer's unrelated default-false flashlight virtual.
	if ( ( IsEffectActive( EF_DIMLIGHT ) && survivor_calm_no_flashlight.GetBool() ) ||
		m_hasPendingCough || GetFOV() != GetDefaultFOV() )
	{
		DelayCalm( 4.0f );
		return false;
	}
	return GetUseEntity() == NULL;
}

double CTerrorPlayer::GetTimeSinceEnemySeen() const
{
	// S304F70 returns the wide difference; callers supply their own spills.
	if ( !( m_enemySeenInterval.GetStartTime() > 0.0f ) )
		return static_cast< double >( 99999.9f );
	return static_cast< double >( gpGlobals->curtime ) -
		static_cast< double >( m_enemySeenInterval.GetStartTime() );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
double CTerrorPlayer::GetTimeSinceVisibleEnemy() const
{
	// S304FE0 returns the wide difference; frustration compares before any float spill.
	if ( !( m_visibleEnemyInterval.GetStartTime() > 0.0f ) )
		return static_cast< double >( 99999.9f );
	return static_cast< double >( gpGlobals->curtime ) -
		static_cast< double >( m_visibleEnemyInterval.GetStartTime() );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// S3089A0. Personal combat is neither the Music combat flag nor UI combat.
bool CTerrorPlayer::IsInCombat( bool recent ) const
{
	if ( m_jockeyAttacker.Get() )
		return true;
	const float weaponDelay = recent ? 1.5f : 3.0f;
	const float visibleDelay = recent ? 3.0f : 6.0f;
	return ( GetTimeSinceWeaponFired() < weaponDelay && m_visibleEnemyInterval.HasStarted() &&
		m_visibleEnemyInterval.GetElapsedTime() < visibleDelay ) ||
		( m_terrorInjuryInterval.HasStarted() && m_terrorInjuryInterval.GetElapsedTime() < weaponDelay );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S308B00: MAX compares fire age wide, but stores its selected value as float.
// Only the injury-winning return recomputes the unspilled clock difference.
double CTerrorPlayer::GetTimeSinceCombat() const
{
	const float visible = m_visibleEnemyInterval.GetElapsedTime();
	const double fired = GetTimeSinceWeaponFired();
	const float firingAtEnemy = fired > static_cast< double >( visible ) ?
		static_cast< float >( GetTimeSinceWeaponFired() ) : m_visibleEnemyInterval.GetElapsedTime();
	const float injury = m_terrorInjuryInterval.GetElapsedTime();
	if ( injury > firingAtEnemy )
		return firingAtEnemy;
	if ( !( m_terrorInjuryInterval.GetStartTime() > 0.0f ) )
		return static_cast< double >( 99999.9f );
	return static_cast< double >( gpGlobals->curtime ) -
		static_cast< double >( m_terrorInjuryInterval.GetStartTime() );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S2FC090 intentionally uses distance/sqrt and strict range>, not a squared
// distance replacement. It neither checks PVS nor excludes incapacitated allies.
int CTerrorPlayer::CountNearbyFriends( float range, bool visibleOnly )
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *other = UTIL_PlayerByIndex( i );
		if ( other && other->IsPlayer() && other->entindex() != entindex() && other->IsAlive() &&
			other->InSameTeam( this ) && range > other->GetAbsOrigin().DistTo( GetAbsOrigin() ) &&
			( !visibleOnly || IsAbleToSee( other, CBaseCombatCharacter::DISREGARD_FOV ) ) )
			++count;
	}
	return count;
}

class CResponseZombiePresence
{
public:
	explicit CResponseZombiePresence( bool *present ) : m_present( present ) {}
	bool operator()( CBaseCombatCharacter *actor )
	{
		if ( actor && actor->IsAlive() && actor->GetTeamNumber() == TEAM_INFECTED )
			m_present[actor->GetZombieClass()] = true;
		return true;
	}
private:
	bool *m_present;
};

// S344D80/344590/344D00: all actors, not just visible actors. In particular
// there is no invented classname, range, FOV, or extra ghost predicate here.
void CTerrorPlayer::CollectResponseZombiePresence( bool present[9] ) const
{
	CResponseZombiePresence collect( present );
	ForEachActor( collect );
}

// S349470, replacing the earlier running-only extraction of this same body.
void CTerrorPlayer::UpdateSneaking()
{
	if ( ( m_nButtons & ( IN_SPEED | IN_DUCK ) ) ||
		!( m_nButtons & ( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT ) ) )
	{
		m_commonRunningTimer.Invalidate();
		if ( m_nButtons & ( IN_SPEED | IN_DUCK ) )
			m_isSneaking = true;
	}
	else
	{
		const float walkSpeed = GetWalkTopSpeed();
		if ( GetAbsVelocity().LengthSqr() > walkSpeed * walkSpeed )
		{
			m_isSneaking = false;
			if ( !m_commonRunningTimer.HasStarted() )
			{
				m_commonRunningTimer.Start();
				m_runExertionTimer.Invalidate();
			}
		}
	}
}

// S2C9020 is reached from the per-command ItemPostFrame owner, not from the
// scheduled 10Hz perception pass. Eye derivatives use REALTIME; timers use
// game curtime, including the original <=0 unstarted semantics at map time0.
void CTerrorPlayer::UpdateStillness()
{
	if ( !IsAlive() )
		return;
	float delta = gpGlobals->realtime - m_flLastSpeechEyeSample;
	if ( delta > 0.0f )
	{
		QAngle angles = EyeAngles();
		float inverse = 1.0f / delta;
		Vector velocity( AngleDiff( angles.x, m_angLastSpeechEye.x ) * inverse,
			AngleDiff( angles.y, m_angLastSpeechEye.y ) * inverse, 0.0f );
		m_vecSpeechEyeAcceleration = ( velocity - m_vecSpeechEyeVelocity ) * inverse;
		m_vecSpeechEyeVelocity = velocity;
		m_angLastSpeechEye = angles;
		m_flLastSpeechEyeSample = gpGlobals->realtime;
	}
	float limit = still_vel_limit.GetFloat();
	bool positionStill = ( ( GetFlags() & FL_ONGROUND ) && GetAbsVelocity().LengthSqr() < limit * limit ) ||
		IsIncapacitated() || m_isHangingFromTongue || IsHangingFromLedge();
	if ( positionStill )
	{
		if ( !m_positionStillTimer.HasStarted() )
			m_positionStillTimer.Start();
	}
	else
		m_positionStillTimer.Invalidate();
	float acceleration = GetDefaultFOV() * m_vecSpeechEyeAcceleration.Length() / GetFOV();
	if ( acceleration < still_view_accel_limit.GetFloat() )
	{
		if ( !m_viewStillTimer.HasStarted() )
			m_viewStillTimer.Start();
	}
	else
		m_viewStillTimer.Invalidate();
}

float CTerrorPlayer::GetStillnessDuration( bool positionOnly ) const
{
	if ( !m_positionStillTimer.HasStarted() || ( !positionOnly && !m_viewStillTimer.HasStarted() ) )
		return 0.0f;
	const float position = m_positionStillTimer.GetElapsedTime();
	return positionOnly ? position : MIN( position, m_viewStillTimer.GetElapsedTime() );
}

bool CTerrorPlayer::IsStill( bool positionOnly ) const
{
	return m_positionStillTimer.HasStarted() && ( positionOnly || m_viewStillTimer.HasStarted() );
}

// S301C90. Eight reader slots, although the original writer only handles0..3.
float CTerrorPlayer::GetTimeSinceFriendSeen( CTerrorPlayer *other ) const
{
	if ( !const_cast< CTerrorPlayer * >( this )->IsAlive() || GetTeamNumber() != TEAM_SURVIVOR || other->GetSurvivorCharacter() == 8 ||
		other->GetSurvivorCharacter() == GetSurvivorCharacter() )
		return -1.0f;
	return m_friendSeenTimer[other->GetSurvivorCharacter()].GetElapsedTime();
}

// S306AB0. The native environmental callback does not immediately speak.
void CTerrorPlayer::OnCough( CBaseEntity *source )
{
	if ( !IsPerceptionSurvivorTeam( GetTeamNumber() ) )
		return;
	if ( !m_hasPendingCough )
	{
		m_hasPendingCough = true;
		const float remaining = MAX( 0.0f, m_coughTimer.GetRemainingTime() );
		m_coughTimer.Start( RandomFloat( remaining, 1.0f ) );
	}
	m_coughSource = source && source->IsPlayer() ? source : NULL;
	m_coughSourceTimer.Start( 1.1f );
}

// S3495D0, after personal intensity in the alive survivor scheduled branch.
void CTerrorPlayer::UpdateCoughAndExertion()
{
	if ( m_hasPendingCough && m_coughTimer.GetRemainingTime() <= 0.0f &&
		m_vocalizeTimer.GetRemainingTime() <= 0.0f )
	{
		m_coughTimer.Start( 1.0f );
		m_vocalizeTimer.Start( 1.0f );
		m_hasPendingCough = false;
		SpeakIfAllowed( "SurvivorCoughing", 1 );
		static ConVarRef noise( "z_noise_level_vocalize" );
		SetNoiseLevel( noise.GetFloat() );
	}
	if ( m_commonRunningTimer.HasStarted() &&
		m_commonRunningTimer.GetElapsedTime() > z_last_man_run_interval.GetFloat() &&
		m_runExertionTimer.GetRemainingTime() <= 0.0f )
	{
		m_runExertionTimer.Start( z_run_exertion_interval.GetFloat() );
		const int health = GetHealth();
		if ( GetHealthBuffer() + health <= 66.0f )
		{
			const int criticalHealth = GetHealth();
			SpeakIfAllowed( GetHealthBuffer() + criticalHealth > 33.0f ? "PlayerExertionMajor" : "PlayerExertionCritical", 0 );
		}
		else
			SpeakIfAllowed( "PlayerExertionMinor", 0 );
	}
}

class CIntensityThreatScan
{
public:
	explicit CIntensityThreatScan( CTerrorPlayer *player ) : m_player( player ) {}
	bool operator()( CBaseCombatCharacter *enemy )
	{
		if ( !enemy->IsAlive() || enemy->GetTeamNumber() != TEAM_INFECTED )
			return true;
		INextBot *bot = enemy->MyNextBotPointer();
		if ( bot && bot->GetBodyInterface() && bot->GetBodyInterface()->IsArousal( IBody::NEUTRAL ) )
			return true;
		const Vector to = enemy->EyePosition() - m_player->EyePosition();
		const float range = survivor_intensity_decay_threat_range.GetFloat();
		// S3466A0 passes this RELATIVE vector to the WORLD-position overload.
		// This double-origin subtraction is original behavior, not a typo.
		return !m_player->IsLookingTowards( to, 0.0f ) || to.LengthSqr() > range * range ||
			!m_player->IsAbleToSee( enemy, CBaseCombatCharacter::DISREGARD_FOV );
	}
private:
	CTerrorPlayer *m_player;
};

class CIntenseActorCounter
{
public:
	CIntenseActorCounter() : m_count( 0 ) {}
	bool operator()( INextBot *bot )
	{
		CBaseCombatCharacter *actor = bot->GetEntity()->MyCombatCharacterPointer();
		// S26BAA0/2901B0(1) deliberately does not test IsAlive here.
		if ( actor && ( actor->GetTeamNumber() != TEAM_INFECTED || actor->GetZombieClass() == 0 ) &&
			!actor->IsEFlagSet( EFL_KILLME ) && bot->GetBodyInterface()->IsArousal( IBody::INTENSE ) )
			++m_count;
		return true;
	}
	int m_count;
};

// S34B5A0/34C990. This is the concrete native mob scan, not a replacement
// vision interface. The Music owner consumes only the attacking count here.
class CVisibleMobScan
{
public:
	CVisibleMobScan( CTerrorPlayer *player, int limit ) : m_player( player ),
		m_eye( player->EyePosition() ), m_area( player->GetLastKnownArea() ), m_limit( limit ) {}
	bool operator()( CBaseCombatCharacter *actor )
	{
		if ( !actor->IsAlive() || actor->GetTeamNumber() != TEAM_INFECTED )
			return true;
		CBaseCombatCharacter *enemy = actor->MyCombatCharacterPointer();
		if ( !enemy )
			return true;
		INextBot *bot = enemy->MyNextBotPointer();
		IBody *body = bot->GetBodyInterface();
		if ( body && body->IsArousal( IBody::NEUTRAL ) )
			return true;
		CNavArea *enemyArea = enemy->GetLastKnownArea();
		if ( !enemyArea || !m_area || !m_area->IsCompletelyVisible( enemyArea ) )
			return true;
		const Vector to = actor->EyePosition() - m_eye;
		if ( !m_player->IsLookingTowards( to, 0.0f ) )
			return true;
		static ConVarRef range( "music_dynamic_attack_CI_distmax" );
		if ( to.Length() > range.GetFloat() )
			return true;
		m_attacking.AddToTail( enemy );
		return m_attacking.Count() < m_limit;
	}
	CUtlVector< CBaseCombatCharacter * > m_attacking;
private:
	CTerrorPlayer *m_player;
	Vector m_eye;
	CNavArea *m_area;
	int m_limit;
};

void CTerrorPlayer::UpdateMusicMobSightings()
{
	if ( m_musicMobScanTimer.GetRemainingTime() > 0.0f || !IsPerceptionSurvivorTeam( GetTeamNumber() ) )
		return;
	m_musicMobScanTimer.Start( 0.1f );
	if ( GetMusic()->IsInCombat() )
	{
		m_musicMobScanTimer.Start( 1.0f );
		static ConVarRef stopSize( "director_music_dynamic_scanmobstop_size" );
		CVisibleMobScan scan( this, TheDirector->GetDirectorOptionInt( "MusicDynamicMobScanStopSize", stopSize.GetInt() ) );
		TheNextBots().ForEachCombatCharacter( scan );
		GetMusic()->OnMobApproachingNear( scan.m_attacking.Count() );
	}
	else
	{
		CIntenseActorCounter count;
		TheNextBots().ForEachBot( count );
		static ConVarRef spawnSize( "director_music_dynamic_mob_size" );
		if ( count.m_count > TheDirector->GetDirectorOptionInt( "MusicDynamicMobSpawnSize", spawnSize.GetInt() ) )
		{
			static ConVarRef mobSize( "z_mob_music_size" );
			CVisibleMobScan scan( this, mobSize.GetInt() );
			TheNextBots().ForEachCombatCharacter( scan );
			static ConVarRef debug( "z_show_infected" );
			if ( debug.GetInt() > 0 )
			{
				DevMsg( "***ATTACKING: %i\n", scan.m_attacking.Count() );
				FOR_EACH_VEC( scan.m_attacking, i )
				{
					const Vector origin = scan.m_attacking[i]->GetAbsOrigin();
					NDebugOverlay::Line( origin - Vector( 0, 0, 5 ), origin + Vector( 0, 0, 5 ), 255, 0, 0, true, 2.0f );
					NDebugOverlay::Line( origin - Vector( 0, 5, 0 ), origin + Vector( 0, 5, 0 ), 255, 0, 0, true, 2.0f );
					NDebugOverlay::Line( origin - Vector( 5, 0, 0 ), origin + Vector( 5, 0, 0 ), 255, 0, 0, true, 2.0f );
				}
			}
			if ( scan.m_attacking.Count() >= mobSize.GetInt() )
				GetMusic()->OnMobApproachingNear( scan.m_attacking.Count() );
		}
	}
}

void CTerrorPlayer::RememberVisibleEnemies( const CUtlVector< CBaseCombatCharacter * > &enemies,
	CBaseCombatCharacter *best, float dot )
{
	m_visibleEnemyInterval.Start();
	m_bestVisibleEnemyDot = dot;
	m_lastVisibleEnemyDistance = WorldSpaceCenter().DistTo( best->WorldSpaceCenter() );
	m_visibleEnemies.RemoveAll();
	FOR_EACH_VEC( enemies, i )
		m_visibleEnemies.AddToTail( enemies[i] );
}

// S34CCB0. One player per global frame can perform this half-second scan.
// Music mob scanning is outside that quota, as it is in the original.
void CTerrorPlayer::CheckEnemySightings()
{
	UpdateMusicMobSightings();
	static int lastScanFrame;
	if ( lastScanFrame == gpGlobals->framecount || m_enemySightCheckTimer.GetRemainingTime() > 0.0f )
		return;
	lastScanFrame = gpGlobals->framecount;
	m_enemySightCheckTimer.Start( 0.5f );
	if ( IsPerceptionSurvivorTeam( GetTeamNumber() ) )
	{
		CIntensityThreatScan threat( this );
		if ( !ForEachActor( threat ) )
			m_survivorIntensity.Hold( 0.5f );
	}
	const int enemyTeam = IsPerceptionSurvivorTeam( GetTeamNumber() ) ? TEAM_INFECTED : TEAM_SURVIVOR;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *other = UTIL_PlayerByIndex( i );
		if ( other && !FNullEnt( other->edict() ) && other->IsPlayer() && other->IsConnected() &&
			other->IsAlive() && other->GetTeamNumber() == enemyTeam && other->entindex() != entindex() &&
			other->IsAbleToSee( this, CBaseCombatCharacter::USE_FOV ) )
		{
			m_seenByEnemyInterval.Start();
			break;
		}
	}
	TargetScan< CBaseCombatCharacter > scan( this, enemyTeam, z_alert_dot.GetFloat(), 2000.0f, 50.0f, false );
	ForEachActor( scan );
	const CUtlVector< CBaseCombatCharacter * > &enemies = scan.GetAllTargets();
	bool visibleThreat = false;
	FOR_EACH_VEC( enemies, i )
	{
		CBaseCombatCharacter *actor = enemies[i];
		if ( actor->IsPlayer() )
		{
			visibleThreat = true;
			break;
		}
		CBaseCombatCharacter *enemy = actor->MyCombatCharacterPointer();
		if ( enemy && ( enemy->GetTeamNumber() != TEAM_INFECTED || enemy->GetZombieClass() == 7 ||
			enemy->MyNextBotPointer()->GetBodyInterface()->GetArousal() != IBody::NEUTRAL ) )
		{
			visibleThreat = true;
			break;
		}
	}
	m_hasVisibleThreats = visibleThreat;
	if ( CBaseCombatCharacter *best = scan.GetTarget() )
	{
		m_enemySeenInterval.Start();
		RememberVisibleEnemies( enemies, best, scan.GetTargetDot() );
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
		if ( weapon && GetAbsOrigin().DistToSqr( best->GetAbsOrigin() ) < 1048576.0f )
			weapon->SuppressHelpingHand( 0.0f );
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		TargetScan< CBaseCombatCharacter > tankScan( this, enemyTeam, 2.0f, 10000.0f, 50.0f, false );
		// S344C80 enumerates player slots only, including genuine NextBot players.
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( player && !FNullEnt( player->edict() ) && player->IsPlayer() && player->IsConnected() )
				tankScan( player );
		}
		if ( tankScan.GetTarget() )
			RememberVisibleEnemies( tankScan.GetAllTargets(), tankScan.GetTarget(), tankScan.GetTargetDot() );
		// The native tank branch retains its old vector if this scan fails.
	}
	else
		m_visibleEnemies.RemoveAll();
}

// S34C3F0/34D930. The writer starts a timer for a continuously VISIBLE friend
// and invalidates it when hidden or outside the FOV. It is not a lost timer.
void CTerrorPlayer::UpdateFriendSeenTimers()
{
	if ( GetTeamNumber() != TEAM_SURVIVOR || m_friendSightCheckTimer.GetRemainingTime() > 0.0f )
		return;
	m_friendSightCheckTimer.Start( 0.5f );
	const float neighborRangeSqr = sb_neighbor_range.GetFloat() * sb_neighbor_range.GetFloat();
	bool alone = true;
	CUtlVector< CTerrorPlayer * > visible;
	CUtlVector< CTerrorPlayer * > hidden;
	CUtlVector< CTerrorPlayer * > dead;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *other = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !other || FNullEnt( other->edict() ) || !other->IsConnected() ||
			other->GetTeamNumber() != TEAM_SURVIVOR || other->entindex() == entindex() )
			continue;
		const bool seen = IsAbleToSee( other, CBaseCombatCharacter::DISREGARD_FOV );
		if ( other->IsAlive() )
		{
			if ( seen )
			{
				if ( GetAbsOrigin().DistToSqr( other->GetAbsOrigin() ) < neighborRangeSqr )
					alone = false;
				if ( IsInFieldOfView( other ) )
				{
					visible.AddToTail( other );
					continue;
				}
			}
			hidden.AddToTail( other );
		}
		else if ( seen )
			dead.AddToTail( other );
	}
	FOR_EACH_VEC( hidden, i )
	{
		const int character = hidden[i]->GetSurvivorCharacter();
		if ( character < 4 )
		{
			m_friendSeenTimer[character].Invalidate();
			m_hasCommentedOnDeadFriend[character] = false;
		}
	}
	FOR_EACH_VEC( visible, i )
	{
		const int character = visible[i]->GetSurvivorCharacter();
		if ( character < 4 )
		{
			if ( !m_friendSeenTimer[character].HasStarted() )
				m_friendSeenTimer[character].Start();
			m_hasCommentedOnDeadFriend[character] = false;
		}
	}
	if ( !alone )
		m_friendNearbyInterval.Start();
	FOR_EACH_VEC( dead, i )
	{
		CTerrorPlayer *other = dead[i];
		const int character = other->GetSurvivorCharacter();
		if ( character < 4 && !m_hasCommentedOnDeadFriend[character] )
		{
			const float distanceSqr = other->m_deathPosition.DistToSqr( GetAbsOrigin() );
			if ( distanceSqr < 6400.0f )
			{
				char modifiers[256];
				Q_snprintf( modifiers, sizeof( modifiers ), "DeadCharacter:%s,Distance:%d",
					other->GetResponseCharacterName(), static_cast< int >( sqrt( distanceSqr ) ) );
				if ( SpeakIfAllowed( "PlayerSeeDeadPlayer", 1, modifiers ) )
					m_hasCommentedOnDeadFriend[character] = true;
			}
		}
	}
}

int CCSPlayer::FlashlightIsOn()
{
	// S243630 is inherited unchanged by Terror.
	return IsEffectActive( EF_DIMLIGHT );
}

bool CCSPlayer::FlashlightTurnOn( bool playSound )
{
	// S243640: preserve the four guards and the sound flag's argument path.
	if ( flashlight.GetInt() <= 0 || !IsAlive() || !CanUseFlashlight() || IsEffectActive( EF_DIMLIGHT ) )
		return false;
	AddEffects( EF_DIMLIGHT );
	if ( playSound )
	{
		CPASAttenuationFilter filter( this, "Player.FlashlightOn" );
		filter.RemoveRecipient( this );
		EmitSound( filter, entindex(), "Player.FlashlightOn" );
	}
	return true;
}

void CCSPlayer::FlashlightTurnOff( bool playSound )
{
	// S248AB0 queries IsAlive after removing the effect, even for silent calls.
	if ( !IsEffectActive( EF_DIMLIGHT ) )
		return;
	RemoveEffects( EF_DIMLIGHT );
	if ( IsAlive() && playSound )
	{
		CPASAttenuationFilter filter( this, "Player.FlashlightOff" );
		filter.RemoveRecipient( this );
		EmitSound( filter, entindex(), "Player.FlashlightOff" );
	}
}

bool CTerrorPlayer::CanUseFlashlight() const
{
	// S325520, native CCS virtual544/+2176.
	return IsPerceptionSurvivorTeam( GetTeamNumber() );
}

bool CTerrorPlayer::FlashlightTurnOn( bool playSound )
{
	// S2FCB40 consumes the real CCS success result before the area bookkeeping.
	if ( !BaseClass::FlashlightTurnOn( playSound ) )
		return false;
	if ( !m_hasUsedFlashlightOutsideSafeArea )
	{
		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		if ( area && !( area->GetSpawnAttributes() & 0x10880 ) )
			m_hasUsedFlashlightOutsideSafeArea = true;
	}
	return true;
}

void CTerrorPlayer::FlashlightTurnOff( bool playSound )
{
	// S303560: living infected keep the effect while their policy is enabled.
	if ( GetTeamNumber() == TEAM_INFECTED && IsAlive() && z_infected_flashlight.GetInt() != 0 )
		return;
	BaseClass::FlashlightTurnOff( playSound );
}

const Vector &CTerrorPlayer::GetInfectedAlertPosition()
{
	m_infectedAlertPosition = EyePosition();
	return m_infectedAlertPosition;
}
