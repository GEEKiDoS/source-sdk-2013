//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/player/terror_player_resource.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_path.h"
#include "l4d2/director/zombie_spawn.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/entities/func_elevator.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/entities/visibility_monitor.h"
#include "in_buttons.h"
#include "igameevents.h"
#include "tier1/KeyValues.h"
#include <emmintrin.h>
#include "l4d2/player/terrorplayer_ghost_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// Sole definitions belong to the genuine actor replacement/spawn owner.
extern int g_iNextZombieClass;
extern int g_iNextInfectedVariant;
extern ConVar director_no_specials;
extern ConVar z_spawn_safety_range;
extern ConVar z_finale_spawn_safety_range;
extern ConVar z_noise_level_hold_time;
extern ConVar z_noise_level_fade_rate;

static ConVar z_ghost_duration( "z_ghost_duration", "1", 0x00000006,
	"dead PZs will become ghosts when they are this close to being able to spawn" );
static ConVar z_ghost_cooldown( "z_ghost_cooldown", "0", 0x00000006 );
static ConVar z_ghost_spawn_in_start( "z_ghost_spawn_in_start", "0", 0x00004004,
	"Allow ghosts to materialize while players are in the start area" );
static ConVar z_ghost_travel_distance( "z_ghost_travel_distance", "1000", 0x00004004,
	"Ghosts this far away from survivors in travel distance can materialize regardless of linear distance from survivors" );
static ConVar z_finale_spawn_safety_range_override( "z_finale_spawn_safety_range_override", "0.0", 0x00000006 );
static ConVar z_ghost_delay_min( "z_ghost_delay_min", "20", 0x00004004 );
static ConVar z_ghost_delay_max( "z_ghost_delay_max", "30", 0x00004004 );
static ConVar z_ghost_delay_minspawn( "z_ghost_delay_minspawn", "3", 0x00002006 );
static ConVar z_ghost_runner_spawn_multiplier( "z_ghost_runner_spawn_multiplier", "0.5", 0x00004006 );
static ConVar z_ghost_runner_spawn_multiplier_dist( "z_ghost_runner_spawn_multiplier_dist", "3000", 0x00004006 );
static ConVar tank_run_spawn_delay( "tank_run_spawn_delay", "15", 0x00004004 );
static ConVar z_ghost_spawn_distance( "z_ghost_spawn_distance", "300", 0x00000006,
	"ghosts will be placed this far ahead of survivors when they cross the line of scrimmage" );
static ConVar z_finale_ghost_player_spawn_on_survivor( "z_finale_ghost_player_spawn_on_survivor", "1", 0x00004004,
	"If set to zero, ghost spawn warping in finale's put the ghost out of the world\ninstead of cycling through survivors." );
ConVar z_boomer_near_dist( "z_boomer_near_dist", "180", 0x00004004 );

namespace
{
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:2FC760/32F980/3301A0. One live foreach; no detached survivor registry.
class GhostSpawnProximity
{
public:
	explicit GhostSpawnProximity( CTerrorPlayer *ghost ) : m_ghost( ghost ), m_blocked( false )
	{
		int type = TheDirector->GetScenario()->GetFinaleType();
		if ( !TheDirector->IsFinale() || type == 3 || type == 1 || type == 4 )
			m_range = z_spawn_safety_range.GetFloat();
		else
		{
			m_range = z_finale_spawn_safety_range_override.GetFloat();
			if ( !( m_range > 0.0f ) ) m_range = z_finale_spawn_safety_range.GetFloat();
		}
	}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *survivor = ToTerrorPlayer( base );
		const int team = survivor->GetTeamNumber();
		if ( ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) || !survivor->IsAlive() ) return true;
		float height = (float)fabs( (double)survivor->GetAbsOrigin().z - (double)m_ghost->GetAbsOrigin().z );
		float dx = survivor->GetAbsOrigin().x - m_ghost->GetAbsOrigin().x;
		float dy = survivor->GetAbsOrigin().y - m_ghost->GetAbsOrigin().y;
		float radius = m_range - height;
		if ( dy * dy + dx * dx > radius * radius ) return true;
		double verticalRange = (double)m_range - (double)height;
		if ( fabs( (double)survivor->GetAbsOrigin().z - (double)m_ghost->GetAbsOrigin().z ) > verticalRange + verticalRange ) return true;
		CNavArea *ghostArea = m_ghost->GetLastKnownArea();
		CNavArea *survivorArea = survivor->GetLastKnownArea();
		if ( !( (double)z_ghost_travel_distance.GetFloat() > (double)GetTerrorNavTravelDistance( survivorArea, ghostArea ) ) ) return true;
		m_blocked = true;
		return false;
	}
	bool IsBlocked() const { return m_blocked; }
private:
	CTerrorPlayer *m_ghost;
	bool m_blocked;
	float m_range;
};
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:3050E0/3093A0 with the original include-controlled=true and ignored=NULL.
// It selects the FARTHEST living survivor, not the nearest or highest-flow one.
class FarthestSurvivorFromGhostSphere
{
public:
	explicit FarthestSurvivorFromGhostSphere( const Vector &center ) : m_center( center ), m_distanceSquared( -1.0f ), m_player( NULL ) {}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = ToTerrorPlayer( base );
		if ( player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) return true;
		const Vector center = player->WorldSpaceCenter();
		float dx = center.x - m_center.x, dy = center.y - m_center.y, dz = center.z - m_center.z;
		float distance = ( dx * dx + dy * dy ) + dz * dz;
		if ( distance > m_distanceSquared ) { m_distanceSquared = distance; m_player = player; }
		return true;
	}
	CTerrorPlayer *GetPlayer() const { return m_player; }
private:
	Vector m_center;
	float m_distanceSquared;
	CTerrorPlayer *m_player;
};

// S:100C0B80 at its actual ghost call (input low0). Final interpolation is
// widened, while the division and clamp operand remain original float.
double RemapGhostSpawnDelay( float distance, float radius, float maximum, float minimum )
{
	if ( radius == 0.0f ) return distance >= radius ? (double)minimum : (double)maximum;
	float scale = ( distance - 0.0f ) / ( radius - 0.0f );
	if ( scale < 0.0f ) scale = 0.0f;
	else if ( scale > 1.0f ) scale = 1.0f;
	return (double)maximum + ( (double)minimum - (double)maximum ) * (double)scale;
}

float GhostDistanceYZX( const Vector &a, const Vector &b )
{
	float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return sqrtf( ( dy * dy + dz * dz ) + dx * dx );
}
}

// S:32FC20. This is the actual state8 entry, not a detached ghost flag setter.
void CTerrorPlayer::OnEnterGhost()
{
	GetMusic()->ClearPlayerDSP(); // Music v+12 /2AAE30, NOT Music::Reset.
	StopObserverMode();
	SetMoveType( MOVETYPE_WALK, MOVECOLLIDE_DEFAULT );
	RemoveSolidFlags( FSOLID_NOT_SOLID );
	m_Local.m_iHideHUD = 0;
	if ( VPhysicsGetObject() ) VPhysicsGetObject()->Sleep(); // CCS2417D0
	m_isGhost = true;
	m_bNativeGhostStateFlag11489 = false;
	m_bNativeGhostFinaleGate11490 = true;
	g_iNextZombieClass = GetZombieClass();
	Spawn(); // Actual virtual full3269D0, while genuine m_isGhost is true.
	m_afButtonLast = 0;
	SetNextThink( -1.0f );
	m_Local.m_bAllowAutoMovement = true;
	EnableControl( true );
	SetViewEntity( NULL ); // Base178400, NOT SetObserverTarget.
	m_Local.m_iHideHUD &= ~HIDEHUD_ALL;
	WarpGhostToInitialPosition( true ); // S:321A70
	SetModelFromClass(); // Actual virtual31A3B0.
	m_ghostSpawnClockTimer.Start( 0.0f );
	m_ghostSpawnState = 0;

	float elapsed = gpGlobals->curtime - m_lastAliveTime;
	(void)TheDirector->GetInfectedSpawnDelay( elapsed ); // Side effects; native discards x87 return.
	m_ghostSpawnClockMaxDelay = ( m_flLineOfScrimmageFlow > 0.0f || m_flRearLineOfScrimmageFlow > 0.0f ) ?
		1 : _mm_cvttss_si32( _mm_set_ss( z_ghost_duration.GetFloat() ) );
	m_ghostSpawnClockCurrentDelay = m_ghostSpawnClockMaxDelay;
	m_ghostSpawnBotTimer.Start( RandomFloat( 0.0f, 15.0f ) );

	if ( gpGlobals->eLoadType != MapLoad_Transition && ( GetViewEntity() == this || !GetViewEntity() ) )
	{
		KeyValues *data = new KeyValues( "data" );
		data->SetInt( "zombieClass", GetZombieClass() );
		ShowViewPortPanel( "spawnmode_bar", true, data );
		data->deleteThis();
	}
	if ( TheDirector->GetScriptedMode()->IsTankRun() && !IsBot() ) MaterializeFromGhost();
}

// S:317A30. No invented timer/line reset, physics change or state transition.
void CTerrorPlayer::OnLeaveGhost()
{
	ShowViewPortPanel( "spawnmode_bar", false, NULL );
	m_isGhost = false;
}

// S:334E40, actual state8 pre-think. No extra IsGhost/alive/service guards.
void CTerrorPlayer::OnPreThinkGhost()
{
	if ( TheDirector->IsInTransition() ) return;
	if ( (double)gpGlobals->curtime >= (double)m_ghostSpawnClockTimer.m_timestamp )
	{
		m_ghostSpawnClockTimer.Start( 0.25f );
		m_ghostSpawnState = 0;
		if ( director_no_specials.GetBool() ) m_ghostSpawnState |= 1;
		int intensity = TheDirector->GetIntensityState();
		if ( ( intensity == 3 || intensity == 2 ) && z_ghost_cooldown.GetBool() ) m_ghostSpawnState |= 0x20;
		if ( !TheDirector->HasAnySurvivorLeftSafeArea() && !z_ghost_spawn_in_start.GetBool() && !m_bNativeGhostStateFlag11489 )
			m_ghostSpawnState |= 2;
		if ( TerrorGameRules()->IsSurvivalMode() && !TheDirector->GetSurvivalMode()->HasStarted() ) m_ghostSpawnState |= 4;
		if ( !TerrorGameRules()->IsScavengeMode() && !TerrorGameRules()->IsSurvivalMode() &&
			!TheDirector->IsFinale() && !TheDirector->GetScenario()->IsFirstPanicEvent() )
		{
			bool noLiving = true;
			if ( !TheDirector->GetScenario()->AreAllLivingSurvivorsInBattlefield( &noLiving ) || noLiving )
				m_bNativeGhostFinaleGate11490 = true;
			else if ( !m_bNativeGhostFinaleGate11490 ) m_ghostSpawnState |= 4;
		}
		if ( TheDirector->IsFinaleWon() ) m_ghostSpawnState |= 0x10;
		// Native25C2C0 is literally return1; its original negated finale
		// branch never sets0x40. Do not invent an unbound policy service.
		if ( m_ghostSpawnClockMaxDelay > 0 )
		{
			if ( m_ghostSpawnClockCurrentDelay > 0 && !( m_ghostSpawnState & 2 ) ) --m_ghostSpawnClockCurrentDelay;
		}
		else
		{
			m_ghostSpawnClockMaxDelay = _mm_cvttsd_si32( _mm_set_sd( TheDirector->GetInfectedSpawnDelay( 0.0f ) ) );
			m_ghostSpawnClockCurrentDelay = m_ghostSpawnClockMaxDelay;
		}
		bool ready = !( m_ghostSpawnState & 0x7F ) && m_ghostSpawnClockMaxDelay > 0 && m_ghostSpawnClockCurrentDelay == 0;
		if ( IsBot() && ready )
		{
			Vector position;
			if ( TheZombieManager->GetRandomPZSpawnPosition( GetZombieClass(), 10, this, &position ) ) SetAbsOrigin( position );
		}
		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		if ( area && !area->IsOverlapping( GetAbsOrigin(), 100.0f ) ) area = NULL;
		if ( IsTerrorSpawnEntityVisibleToTeam( this, TEAM_SURVIVOR, 1, 0.0f, area ) ) m_ghostSpawnState |= 0x80;
		GhostSpawnProximity proximity( this );
		ForEachPlayer( proximity );
		if ( proximity.IsBlocked() ) m_ghostSpawnState |= 0x100;
		if ( area )
		{
			if ( area->HasSpawnAttributes( 0x800 ) && !TerrorGameRules()->IsScavengeMode() )
			{
				CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
				if ( ( checkpoint && checkpoint->GetAreas().Find( area ) != -1 ) || !TheDirector->HaveAllSurvivorsLeftSafeArea() )
					m_ghostSpawnState |= 0x200;
			}
			else if ( area->HasSpawnAttributes( 0x18000 ) ) m_ghostSpawnState |= 0x200;
		}
		const Vector maxs = GetPlayerMaxs();
		const Vector mins = GetPlayerMins();
		Ray_t ray;
		ray.Init( GetAbsOrigin(), GetAbsOrigin(), mins, maxs );
		trace_t trace;
		UTIL_TraceRay( ray, PlayerSolidMask( false ), this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( ( PlayerSolidMask( false ) & trace.contents ) && trace.m_pEnt ) m_ghostSpawnState |= 0x400;
	}
	if ( m_ghostSpawnState == 0 && m_ghostSpawnClockMaxDelay > 0 && m_ghostSpawnClockCurrentDelay == 0 &&
		( ( m_afButtonReleased & IN_ATTACK ) || ( IsBot() && (double)gpGlobals->curtime >= (double)m_ghostSpawnBotTimer.m_timestamp ) ) )
		MaterializeFromGhost();
}

// S:2FC7E0. The real readiness clock is11284, distinct from the two old
// private11492/11496 clocks. Original publication is guarded by this cast.
void CTerrorPlayer::SetNextGhostSpawnTime( float deadline )
{
	m_flNextGhostSpawnTime = deadline;
	CTerrorPlayerResource *resource = dynamic_cast< CTerrorPlayerResource * >( g_pPlayerResource );
	if ( resource ) resource->SetBecomeGhostAt( entindex(), deadline );
}

// S:321030. This is the actual eligibility/clock/line policy, not IsGhost.
bool CTerrorPlayer::CanBecomeGhost( bool force )
{
	if ( GetTeamNumber() != TEAM_INFECTED )
	{
		m_ghostSpawnState = 0;
		SetNextGhostSpawnTime( -1.0f );
		return false;
	}
	if ( IsGhost() )
	{
		SetNextGhostSpawnTime( -1.0f ); // Retain its existing ghost-state bits.
		return false;
	}
	if ( IsAlive() || State_Get() == STATE_DEATH_ANIM || State_Get() == STATE_DEATH_WAIT_FOR_KEY )
	{
		m_ghostSpawnState = 0;
		SetNextGhostSpawnTime( -1.0f );
		return false;
	}
	if ( director_no_specials.GetBool() )
	{
		m_ghostSpawnState = 1;
		SetNextGhostSpawnTime( -1.0f );
		return false;
	}
	// Native25C2C0 returns literaltrue, so its negated finale/first-panic
	// branch is unreachable. Do not replace it with a guessed policy.
	if ( !TheDirector->IsFinale() && !TheDirector->GetScenario()->IsFirstPanicEvent() &&
		m_scrimmageType != 0 && !force && !TerrorGameRules()->IsPlayerControlledZombies() )
	{
		if ( !m_bNativeGhostFinaleGate11490 && TheDirector->GetScenario()->AreSurvivorsInFinaleAreaBeforePanic() )
		{
			m_ghostSpawnState = 4;
			SetNextGhostSpawnTime( -1.0f );
			return false;
		}
		m_bNativeGhostFinaleGate11490 = true;
	}
	if ( TheDirector->IsFinaleWon() || TheDirector->IsFinaleVehicleLeaving() )
	{
		m_ghostSpawnState = 0x10;
		SetNextGhostSpawnTime( -1.0f );
		return false;
	}
	if ( !TheDirector->HasAnySurvivorLeftSafeArea() && TheDirector->GetScriptedMode()->IsTankRun() )
	{
		m_ghostSpawnState = 2;
		SetNextGhostSpawnTime( -1.0f );
		return false;
	}
	if ( !force && TheDirector->HasAnySurvivorLeftSafeArea() )
	{
		float minimum = TheDirector->GetDirectorOptionFloat( "ZombieGhostDelayMin", z_ghost_delay_min.GetFloat() );
		float maximum = TheDirector->GetDirectorOptionFloat( "ZombieGhostDelayMax", z_ghost_delay_max.GetFloat() );
		float delay = maximum;
		if ( TerrorGameRules()->IsSurvivalMode() ) delay = TheDirector->GetSpecialRespawnInterval();
		else
		{
			float radius = GetScrimmageSphereRadius();
			const Vector center = m_scrimmageSphereCenter;
			m_ghostSpawnState = 0;
			FarthestSurvivorFromGhostSphere farthest( center );
			ForEachPlayer( farthest );
			CTerrorPlayer *player = farthest.GetPlayer();
			if ( player )
			{
				Vector low, high;
				if ( player->m_Local.m_bDucked )
				{
					low = TerrorGameRules()->GetViewVectors()->m_vDuckHullMin + player->GetAbsOrigin();
					high = TerrorGameRules()->GetViewVectors()->m_vDuckHullMax + player->GetAbsOrigin();
				}
				else
				{
					low = TerrorGameRules()->GetViewVectors()->m_vHullMin + player->GetAbsOrigin();
					high = TerrorGameRules()->GetViewVectors()->m_vHullMax + player->GetAbsOrigin();
				}
				float diameter = GhostDistanceYZX( low, high );
				float distance = GhostDistanceYZX( player->WorldSpaceCenter(), center );
				float edgeDistance = diameter * 0.5f + distance;
				m_survivorsLineOfScrimmageDistance = radius - edgeDistance;
				delay = RemapGhostSpawnDelay( edgeDistance, radius, maximum, minimum );
			}
		}
		// S2625F0(team3,false): saved-minus-restored PLUS actual human
		// roster count. Do not replace this with live count or a cached zero.
		int saved, restored;
		TheDirector->GetSavedPlayerStates()->GetRestoringPlayerCounts( TEAM_INFECTED, &saved, &restored );
		int count = saved - restored + TheDirector->CountPlayersOnTeam( TEAM_INFECTED, false );
		int limit = TheDirector->GetMaxSpecialCount();
		float additional = 0.0f;
		if ( count < limit )
		{
			float scaled = ( (float)count / (float)limit ) * delay;
			float minimumSpawn = z_ghost_delay_minspawn.GetFloat();
			if ( !( scaled > minimumSpawn ) ) scaled = minimumSpawn; // Native JA; unordered takes minimum.
			float difference = delay - scaled;
			delay = scaled;
			additional = difference > 0.0f ? difference : 0.0f;
		}
		if ( z_ghost_runner_spawn_multiplier.GetFloat() != 0.0f && TheDirector->GetTankCount() <= 0 &&
			TheDirector->GetWitchCount() <= 0 && !TheDirector->IsFinale() && !TheDirector->GetScenario()->IsFirstPanicEvent() )
		{
			CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
			if ( front && !front->IsIncapacitated() && !front->IsDominated() && !front->IsHangingFromLedge() )
			{
				CUtlVector< CTerrorPlayer * > survivors;
				CollectMobileSurvivors( survivors );
				bool allDying = true, isolated = survivors.Count() > 1;
				if ( survivors.Count() > 1 )
				{
					FOR_EACH_VEC( survivors, i )
					{
						CTerrorPlayer *other = survivors[i];
						if ( other == front ) continue;
						int health = other->GetHealth(); // Native captures BEFORE eventful buffer getter.
						if ( (double)other->GetHealthBuffer() + (double)health > 5.0 ) allDying = false;
						float distance = z_ghost_runner_spawn_multiplier_dist.GetFloat();
						float flow = front->GetFlowDistance( 0 );
						if ( (double)distance > (double)flow - other->GetFlowDistance( 0 ) ) { isolated = false; break; }
						float dx = front->GetAbsOrigin().x - other->GetAbsOrigin().x;
						float dy = front->GetAbsOrigin().y - other->GetAbsOrigin().y;
						float dz = front->GetAbsOrigin().z - other->GetAbsOrigin().z;
						if ( ( dy * dy + dx * dx ) + dz * dz < 1440000.0f ) { isolated = false; break; }
					}
					if ( isolated && !allDying )
					{
						float scaled = z_ghost_runner_spawn_multiplier.GetFloat() * delay;
						float minimumSpawn = z_ghost_delay_minspawn.GetFloat();
						delay = scaled > minimumSpawn ? scaled : minimumSpawn;
					}
				}
			}
		}
		if ( TheDirector->GetScriptedMode()->IsTankRun() )
			delay = TheDirector->GetDirectorOptionFloat( "TankRunSpawnDelay", tank_run_spawn_delay.GetFloat() );
		if ( m_flNextGhostSpawnTime == -1.0f )
		{
			SetNextGhostSpawnTime( gpGlobals->curtime + delay );
			TheDirector->DelayNextSpecialSpawn( additional );
			IGameEvent *event = gameeventmanager->CreateEvent( "ghost_spawn_time" );
			if ( event )
			{
				event->SetInt( "userid", GetUserID() );
				event->SetInt( "spawntime", _mm_cvttss_si32( _mm_set_ss( delay ) ) );
				gameeventmanager->FireEvent( event );
			}
			m_scrimmageType = 0;
			m_ghostSpawnState = 0;
		}
		else if ( ( m_flNextGhostSpawnTime - gpGlobals->curtime ) - delay > 3.0f )
			SetNextGhostSpawnTime( gpGlobals->curtime + delay );
		return gpGlobals->curtime > m_flNextGhostSpawnTime;
	}
	m_ghostSpawnState = 0;
	return true;
}

// S:32FB10. Native state6 is observer,8 is ghost. Do not skip its observer
// transition when eligibility fails, or replace this with paired-bot removal.
void CTerrorPlayer::AttemptBecomeGhost( bool force )
{
	if ( CanBecomeGhost( force ) )
	{
		m_zombieClass = 3;
		State_Transition( STATE_GHOST );
		if ( force && m_ghostSpawnClockMaxDelay <= 0 )
		{
			m_ghostSpawnClockMaxDelay = 1;
			m_ghostSpawnClockCurrentDelay = m_ghostSpawnClockMaxDelay;
		}
	}
	else if ( State_Get() != STATE_OBSERVER_MODE ) State_Transition( STATE_OBSERVER_MODE );
}

namespace
{
// S:2FC850. This shares the real NAV marker and native neighbor implementation.
// In particular, the last area need not have greater flow than the previous one.
Vector GetGhostPositionAheadOfSurvivor( CTerrorPlayer *survivor, CTerrorPlayer *ghost, float wanted )
{
	if ( TerrorGameRules()->IsVersusMode() ) return survivor->GetAbsOrigin();
	TerrorNavArea *area = ToTerrorNavArea( survivor->GetLastKnownArea() );
	if ( !area ) return survivor->GetAbsOrigin();
	CNavArea::MakeNewMarker();
	TerrorNavArea *previous = NULL;
	while ( wanted > area->GetFlowDistance() && !area->IsMarked() )
	{
		area->Mark();
		previous = area;
		area = area->GetNextHighestFlowConnectedArea( NULL );
		if ( !area ) return survivor->GetAbsOrigin();
	}
	if ( previous != area )
	{
		Vector position = area->GetCenter();
		if ( TheZombieManager->CanZombieSpawnHere( position, area, ghost->GetZombieClass(), false, ghost ) )
			return position;
	}
	return survivor->GetAbsOrigin();
}

// S:28FDD0/260260 with actual allowIncapped=true, excludeDamaging=false,
// excludedPlayer=NULL. Distance is to WorldSpaceCenter, seeded with 1e8.
class GhostFacingSurvivor
{
public:
	explicit GhostFacingSurvivor( const Vector &origin ) : m_best( NULL ), m_origin( origin ), m_range( 100000000.0f ) {}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = ToTerrorPlayer( base );
		if ( player && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR )
		{
			const Vector &center = player->WorldSpaceCenter();
			float dx = center.x - m_origin.x, dy = center.y - m_origin.y, dz = center.z - m_origin.z;
			float range = ( dx * dx + dy * dy ) + dz * dz;
			if ( m_range > range ) { m_range = range; m_best = player; }
		}
		return true;
	}
	CTerrorPlayer *m_best;
private:
	Vector m_origin;
	float m_range;
};


bool GhostSoundFloatIsZero( float value )
{
	return ( FloatBits( value ) & 0x7fffffffu ) == 0;
}
bool GhostSoundFloatIsNaN( float value )
{
	return ( FloatBits( value ) & 0x7fffffffu ) > 0x7f800000u;
}
}
// S3019A0: shared Spawn/Materialize callback; exactly one IsPlayer query.
bool OnGhostBoomerVisible( CBaseEntity *entity, CBasePlayer *observer )
{
	if ( !entity || !entity->IsPlayer() )
		return false;
	CTerrorPlayer *boomer = static_cast< CTerrorPlayer * >( entity );
	if ( boomer->GetZombieClass() != 2 )
		return false;
	IGameEvent *event = gameeventmanager->CreateEvent( "boomer_near" );
	if ( event )
	{
		event->SetInt( "userid", boomer->GetUserID() );
		event->SetInt( "victim", observer->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	return false;
}

// S:321A70. The independent booleans govern a successful fast warp and the
// elevator-adjusted exit; neither is a second ghost identity or readiness gate.
void CTerrorPlayer::WarpGhostToInitialPosition( bool randomSurvivor )
{
	if ( !IsGhost() ) return;
	Vector position = GetAbsOrigin();
	bool warped = false, elevatorAdjusted = false;
	CTerrorPlayer *facing = NULL;
	if ( ( TheDirector->IsFinale() && TheDirector->GetScenario()->GetFinaleType() != 3 &&
		( !z_finale_ghost_player_spawn_on_survivor.GetBool() || GetZombieClass() == 8 ) ) ||
		TheDirector->GetScriptedMode()->QueryOptionBool( "cm_AutoSpawnInfectedGhosts", false ) )
	{
		if ( TheZombieManager->GetRandomPZSpawnPosition( GetZombieClass(), 10, this, &position ) )
		{
			SetAbsOrigin( position );
			warped = true;
		}
	}
	if ( !IsBot() && !warped )
	{
		int initialCharacter = m_iGhostWarpCharacter;
		do
		{
			unsigned int next = (unsigned int)m_iGhostWarpCharacter + 1u;
			m_iGhostWarpCharacter = next > 3u ? 0 : (int)next;
			int character = MapTerrorSurvivorCharacter( m_iGhostWarpCharacter );
			CTerrorPlayer *survivor = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( character ) );
			if ( survivor && survivor->IsAlive() && !survivor->GetElevator() )
			{
				float noiseTime = ( survivor->m_noiseLevel / z_noise_level_fade_rate.GetFloat() +
					z_noise_level_hold_time.GetFloat() ) - ( gpGlobals->curtime - survivor->m_lastNoiseTime );
				if ( noiseTime > 0.25f )
				{
					CNavArea *area = survivor->GetLastKnownArea();
					Vector candidate = area ? area->GetCenter() : survivor->GetAbsOrigin();
					if ( FindNearbyTransitionSpawn( this, &candidate, 0, true, 100.0f ) )
					{
						SetAbsOrigin( candidate );
						facing = survivor;
						int delay = m_ghostSpawnClockCurrentDelay;
						m_ghostSpawnClockCurrentDelay = delay >= 1 ? delay : 1;
						warped = true;
						break;
					}
				}
			}
		}
		while ( m_iGhostWarpCharacter != initialCharacter );
	}
	if ( !warped )
	{
		CTerrorPlayer *survivor = NULL;
		if ( randomSurvivor && TerrorGameRules()->IsScavengeMode() )
		{
			CUtlVector< CTerrorPlayer * > mobile;
			CollectMobileSurvivors( mobile );
			if ( mobile.Count() > 0 ) survivor = mobile[RandomInt( 0, mobile.Count() - 1 )];
		}
		if ( !survivor ) survivor = FindHighestFlowSurvivor( 0, 1 );
		if ( survivor )
		{
			position = survivor->GetAbsOrigin();
			// Native16DEA0 resolves raw8208, not checked-ground2FBF70.
			// OriginalF9EA0 admits only v380=true: exhaustive563-primary-table
			// census proves ONLY realCFuncElevator true. The raw writer/three
			// invalidators/paired transport must land atomically with this code.
			CFuncElevator *elevator = static_cast< CFuncElevator * >( survivor->GetElevator() );
			bool useLine = !TheDirector->IsFinale() && m_scrimmageType != 0;
			if ( TerrorGameRules()->IsPlayerControlledZombies() )
			{
				useLine = elevator == NULL;
				if ( survivor->m_Local.m_bDucking || ( survivor->GetFlags() & FL_DUCKING ) )
				{
					m_afButtonLast |= IN_DUCK;
					AddFlag( FL_DUCKING );
					m_Local.m_bDucked = true;
					m_Local.m_bDucking = true;
					m_Local.m_nDuckTimeMsecs = 0; // Native integer duck-clock reset; no second clock.
					SetViewOffset( GameRules()->GetViewVectors()->m_vDuckView );
					UTIL_SetSize( this, GameRules()->GetViewVectors()->m_vDuckHullMin, GameRules()->GetViewVectors()->m_vDuckHullMax );
				}
			}
			if ( useLine )
			{
				float wanted = m_flLineOfScrimmageFlow + z_ghost_spawn_distance.GetFloat();
				position = GetGhostPositionAheadOfSurvivor( survivor, this, wanted );
				SetAbsOrigin( position );
				if ( m_ghostSpawnClockCurrentDelay == 0 && !IsBot() ) m_ghostSpawnClockCurrentDelay = 1;
				warped = true;
			}
			else if ( elevator )
			{
				const FloorInfo *floor = elevator->GetFloor( elevator->GetDestinationFloor() );
				if ( floor )
				{
					double current = elevator->UpdateCurrentPosition( "CTerrorPlayer::WarpGhostToInitialPosition" );
					position.z = (float)( ( (double)floor->height - current ) + (double)position.z );
					elevatorAdjusted = true;
				}
			}
		}
		else
		{
			CUtlVector< CTerrorPlayer * > alive;
			// Native26F7A0 admits every alive TEAM2, including a real ghost.
			CollectTerrorPlayers( alive, TEAM_SURVIVOR, 2 | 4, 3, 9 );
			if ( alive.Count() > 0 ) position = alive[RandomInt( 0, alive.Count() - 1 )]->GetAbsOrigin();
			else TerrorGameRules()->GetPlayerSpawnPosition( 8, &position, NULL, NULL );
		}
		if ( !warped )
		{
			if ( !FindNearbyTransitionSpawn( this, &position, 0, true, 100.0f ) &&
				!FindNearbyTransitionSpawn( this, &position, 0, true, 1000.0f ) )
				TerrorGameRules()->GetPlayerSpawnPosition( 8, &position, NULL, NULL );
			SetAbsOrigin( position );
			m_Local.m_bDucked = true;
			m_Local.m_bDucking = false;
			AddFlag( FL_DUCKING );
			UTIL_SetSize( this, GameRules()->GetViewVectors()->m_vDuckHullMin, GameRules()->GetViewVectors()->m_vDuckHullMax );
			if ( elevatorAdjusted ) return;
		}
	}
	if ( !facing )
	{
		GhostFacingSurvivor closest( GetAbsOrigin() );
		ForEachPlayer( closest );
		facing = closest.m_best;
	}
	if ( facing )
	{
		QAngle angles;
		VectorAngles( facing->GetAbsOrigin() - GetAbsOrigin(), angles );
		if ( GhostDistanceYZX( facing->GetAbsOrigin(), GetAbsOrigin() ) < 64.0f ) angles.x = 0.0f;
		SnapEyeAngles( angles );
	}
}

// S:322310, native12172..12431 exactly. These are the existing semantic
// stat columns, not a raw matrix; network12432 m_survivorKillScore is untouched.
void CTerrorPlayer::ResetInfectedLifeStatistics()
{
	for ( int i = 0; i < 4; ++i )
	{
		m_survivorDirectDamage[i] = m_survivorAssistDamage[i] = m_survivorIncaps[i] = m_survivorKills[i] = 0;
		m_survivorPounces[i] = m_survivorLungeShoves[i] = m_survivorSmokerDrags[i] = m_survivorSmokerChokes[i] = 0;
		m_survivorVomitHits[i] = m_survivorVomitExplosions[i] = m_survivorClawHits[i] = 0;
		m_survivorTankRockHits[i] = m_survivorPhysicsHits[i] = 0;
	}
	m_directSurvivorDamage = m_assistSurvivorDamage = m_totalSurvivorIncaps = m_totalSurvivorKills = 0;
	m_totalSurvivorPounces = m_totalSurvivorLungeShoves = m_totalSurvivorSmokerDrags = m_totalSurvivorSmokerChokes = 0;
	m_totalSurvivorVomitHits = m_totalSurvivorVomitExplosions = m_totalSurvivorClawHits = 0;
	m_totalSurvivorTankRockHits = m_totalSurvivorPhysicsHits = 0;
}

// S:322310. State0 performs genuine state8 leave/active entry. There is no
// Spawn call here and no extra eligibility/IsGhost guard.
void CTerrorPlayer::MaterializeFromGhost()
{
	const char *zombieClass = GetZombieClassName( GetZombieClass() );
	DevMsg( "%s materialized from spawn mode as a %s\n", GetPlayerName(), zombieClass );
	ResetInfectedLifeStatistics();
	EmitPrivateSound( "PlayerZombie.BecomeReal" );
	State_Transition( STATE_ACTIVE );
	int kind = GetZombieClass();
	switch ( kind )
	{
	case 1: case 3: case 4: case 5: case 6:
		GetMusic()->PostponeSpecialProximity( kind );
		break;
	case 2:
		PlayTrackedSound( "BoomerZombie.Gurgle", 0.0f );
		GetMusic()->PostponeSpecialProximity( 2 );
		VisibilityMonitor_AddEntity( this, z_boomer_near_dist.GetFloat(), OnGhostBoomerVisible, NULL );
		break;
	default:
		break;
	}
	CBaseEntity::PhysicsRemoveTouchedList( this );
	IGameEvent *event = gameeventmanager->CreateEvent( "player_spawn" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	CBaseAbility *ability = static_cast< CBaseAbility * >( m_customAbility.Get() );
	if ( ability ) ability->SuppressActivation( 1.0f );
}

// S:317A90/30AD60/317B80 share pointer-identity records. They deliberately do
// not compare text, own strings, or run a periodic expiration/removal worker.
void CTerrorPlayer::PlayTrackedSound( const char *sound, float duration )
{
	TrackedSound record = { sound && *sound ? sound : NULL,
		GhostSoundFloatIsZero( duration ) ? 0.0f : gpGlobals->curtime + duration };
	FOR_EACH_VEC( m_trackedSounds, i )
	{
		TrackedSound &existing = m_trackedSounds[i];
		if ( existing.m_sound != record.m_sound ) continue;
		if ( !GhostSoundFloatIsZero( duration ) && !GhostSoundFloatIsZero( existing.m_deadline ) )
		{
			float deadline = gpGlobals->curtime + duration;
			existing.m_deadline = existing.m_deadline > deadline ? existing.m_deadline : deadline;
		}
		return;
	}
	EmitSound( sound, 0.0f, NULL );
	m_trackedSounds.AddToTail( record );
}

void CTerrorPlayer::StopTrackedSound( const char *sound )
{
	const char *key = sound && *sound ? sound : NULL;
	FOR_EACH_VEC( m_trackedSounds, i )
	{
		if ( m_trackedSounds[i].m_sound != key ) continue;
		float deadline = m_trackedSounds[i].m_deadline;
		if ( GhostSoundFloatIsZero( deadline ) ||
			( !GhostSoundFloatIsNaN( deadline ) && !GhostSoundFloatIsNaN( gpGlobals->curtime ) && deadline >= gpGlobals->curtime ) )
			StopSound( sound );
		m_trackedSounds.FastRemove( i );
		return;
	}
}

void CTerrorPlayer::TrackSound( const char *sound )
{
	TrackedSound record = { sound && *sound ? sound : NULL, 0.0f };
	FOR_EACH_VEC( m_trackedSounds, i )
		if ( m_trackedSounds[i].m_sound == record.m_sound ) return;
	m_trackedSounds.AddToTail( record );
}
