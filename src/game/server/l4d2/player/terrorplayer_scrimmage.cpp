//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/escape_route.h"
#include "ndebugoverlay.h"
#include "tier0/memdbgon.h"

static ConVar z_scrimmage_sphere( "z_scrimmage_sphere", "1", 0x00000006 );
ConVar z_scrimmage_creep_delay( "z_scrimmage_creep_delay", "30", 0x00002006 );
ConVar z_scrimmage_creep_rate( "z_scrimmage_creep_rate", "50", 0x00002006 );
static ConVar z_ghost_line_of_scrimmage_near( "z_ghost_line_of_scrimmage_near", "1000", 0x00000006 );
static ConVar z_ghost_line_of_scrimmage_far( "z_ghost_line_of_scrimmage_far", "2000", 0x00000006 );
static ConVar z_ghost_line_of_scrimmage_threat_bonus( "z_ghost_line_of_scrimmage_threat_bonus", "1000", 0x00000006 );
static ConVar z_ghost_line_of_scrimmage_safety( "z_ghost_line_of_scrimmage_safety", "10", 0x00000006,
	"If the survivors are this close to the checkpoint, allow ghosts based on time" );
static ConVar z_ghost_los_expected_progress( "z_ghost_los_expected_progress", "1500", 0x00000006 );

namespace
{
// S:305D40. Midpoint of the two actual extreme-flow survivors, NOT a Tank
// center or average of all actors. Failed native spawn selection retains zero.
Vector GetScrimmageCenter()
{
	CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
	CTerrorPlayer *rear = FindLowestFlowSurvivor( 0 );
	if ( front && rear )
	{
		Vector rearCenter = rear->WorldSpaceCenter();
		Vector frontCenter = front->WorldSpaceCenter();
		return ( rearCenter + frontCenter ) * 0.5f;
	}
	Vector center = vec3_origin;
	TerrorGameRules()->GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &center, NULL, NULL );
	return center;
}

bool IsStrictlyNearOrigin( const Vector &position )
{
	return position.x > -0.01f && position.x < 0.01f &&
		position.y > -0.01f && position.y < 0.01f &&
		position.z > -0.01f && position.z < 0.01f;
}

}
// S:300220. The same original boolean selects interpolation, reverse-flow
// lookup AND optional ground probing. Do not decouple or repair that policy.
double FindScrimmagePointAtFlow( float flow, bool reverse, Vector *position, QAngle *angles )
{
	if ( !GetCriticalPath() ) return 0.0; // Actual native entry guard.
	float current = flow;
	float minimum = flow - 360.0f;
	TerrorFlowDirection direction = reverse ? FLOW_FROM_START : FLOW_TOWARD_GOAL;
	if ( flow > minimum )
	{
		for ( ;; )
		{
			current = (float)GetCriticalPath()->GetPositionAtFlowDistance( current, reverse, position, direction );
			if ( !reverse ) break;
			Vector start( position->x, position->y, position->z + 32.0f );
			Vector end( position->x, position->y, position->z - 64.0f );
			CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
			trace_t trace;
			UTIL_TraceLine( start, end, 0x0201420B, &filter, &trace );
			if ( r_visualizetraces.GetBool() ) NDebugOverlay::Line( trace.startpos, trace.endpos, 255, 0, 0, true, -1.0f );
			if ( trace.startsolid || trace.fraction < 1.0f ) break;
			current -= 25.0f;
			if ( !( current > minimum ) ) break;
		}
	}
	float backwards = 200.0f;
	Vector behind;
	for ( ;; )
	{
		GetCriticalPath()->GetPositionAtFlowDistance( current - backwards, reverse, &behind, direction );
		float x = behind.x - position->x;
		float y = behind.y - position->y;
		float z = behind.z - position->z;
		if ( !IsStrictlyNearOrigin( behind ) && ( ( y * y + x * x ) + z * z ) > 0.0f ) break;
		backwards += 100.0f;
		if ( !( backwards < 2000.0f ) ) break;
	}
	if ( IsStrictlyNearOrigin( *position ) || IsStrictlyNearOrigin( behind ) ) return 0.0;
	VectorAngles( *position - behind, *angles );
	return (double)current;
}

// S:3005C0. Recompute snapshots use this actual front flow only in line mode.
double CTerrorPlayer::GetLineOfScrimmageFlow() const
{
	return m_scrimmageType == 1 ? (double)m_flLineOfScrimmageFlow : -1.0;
}

// S:2C8CE0. COMISS/JNB makes unordered timestamps/elapsed values enter the
// arithmetic, unlike a positive-value test. Preserve the resulting NaN.
double CTerrorPlayer::GetScrimmageSphereRadius() const
{
	float radius = m_scrimmageSphereInitialRadius;
	float start = m_scrimmageStartTime;
	if ( !( start <= 0.0f ) )
	{
		float elapsed = ( gpGlobals->curtime - start ) - z_scrimmage_creep_delay.GetFloat();
		if ( !( elapsed <= 0.0f ) )
		{
			radius = m_scrimmageSphereInitialRadius - z_scrimmage_creep_rate.GetFloat() * elapsed;
			if ( radius < 10.0f ) radius = 10.0f;
		}
	}
	return (double)radius;
}

// S:2FAFF0. Director404 is intensity state, NOT Rules difficulty/state356.
double CTerrorPlayer::GetLineOfScrimmageRange() const
{
	int state = TheDirector->GetIntensityState();
	float distance = ( state == 2 || state == 3 ) ? z_ghost_line_of_scrimmage_far.GetFloat() : z_ghost_line_of_scrimmage_near.GetFloat();
	if ( !TerrorGameRules()->IsPlayerControlledZombies() )
	{
		if ( TheDirector->GetTankCount() > 0 ) distance = z_ghost_line_of_scrimmage_threat_bonus.GetFloat() + distance;
		if ( TheDirector->GetWitchCount() > 0 ) distance = z_ghost_line_of_scrimmage_threat_bonus.GetFloat() * 0.5f + distance;
	}
	return (double)distance;
}

// S:3059C0. Assembly controls the front/rear arithmetic and two highest-player
// POINTER comparison: the decompiler loses these live functor-result operands.
bool CTerrorPlayer::ComputeLineOfScrimmage( bool front, float requested, float *flow, Vector *position, QAngle *angles ) const
{
	if ( GetTeamNumber() != TEAM_INFECTED || !GetCriticalPath() ) return false;
	// S:3059DF COMISS(0,requested)/JB skips the finale query on UNORDERED.
	// S:305A02 COMISS(requested,0)/JBE then sends UNORDERED to the automatic
	// path. Keep both policies explicit even when this TU uses /fp:fast.
	unsigned int requestedBits = FloatBits( requested );
	unsigned int requestedMagnitude = requestedBits & 0x7FFFFFFFu;
	bool ordered = requestedMagnitude <= 0x7F800000u;
	bool positive = ordered && requestedMagnitude != 0 && !( requestedBits & 0x80000000u );
	if ( ordered && !positive && TheDirector->IsFinale() ) return false;
	if ( positive )
	{
		TerrorNavArea *area = GetCriticalPath()->GetAreaAtFlowDistance( requested, FLOW_TOWARD_GOAL );
		while ( area && area->HasSpawnAttributes( TERROR_NAV_FINALE ) )
		{
			requested -= 25.0f;
			area = GetCriticalPath()->GetAreaAtFlowDistance( requested, FLOW_TOWARD_GOAL );
		}
	}
	else
	{
		float range = (float)GetLineOfScrimmageRange();
		CTerrorPlayer *highest = FindHighestFlowSurvivor( 0, 0 );
		CTerrorPlayer *outsideExit = FindHighestFlowSurvivor( 0, 8 );
		if ( highest != outsideExit ) return false;
		CTerrorPlayer *selected = highest;
		if ( !front )
		{
			CTerrorPlayer *lowest = FindLowestFlowSurvivor( 4 );
			if ( lowest ) selected = lowest;
		}
		if ( !selected )
		{
			if ( !front ) return false;
			double found = FindScrimmagePointAtFlow( range, false, position, angles );
			if ( !( found > 0.0 ) ) return false;
			*flow = (float)found;
			return true;
		}
		float survivorFlow = (float)selected->GetFlowDistance( 0 );
		requested = front ? survivorFlow + range : survivorFlow - range;
		if ( requested < 0.0f ) requested = 0.0f;
		TerrorNavArea *area = GetCriticalPath()->GetAreaAtFlowDistance( requested, FLOW_TOWARD_GOAL );
		while ( area && area->HasSpawnAttributes( TERROR_NAV_FINALE ) )
		{
			requested -= 25.0f;
			area = GetCriticalPath()->GetAreaAtFlowDistance( requested, FLOW_TOWARD_GOAL );
		}
		float maximum = TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance() - z_ghost_line_of_scrimmage_safety.GetFloat();
		if ( requested > maximum ) requested = maximum;
		if ( front ? survivorFlow > requested : requested > survivorFlow ) return false;
	}
	double found = FindScrimmagePointAtFlow( requested, false, position, angles );
	if ( !( found > 0.0 ) ) return false;
	*flow = (float)found;
	return true;
}

void CTerrorPlayer::ClearLineOfScrimmage()
{
	m_flLineOfScrimmageFlow = m_flLineOfScrimmageCurrentFlow = 0.0f;
	m_flRearLineOfScrimmageFlow = m_flRearLineOfScrimmageCurrentFlow = 0.0f;
	if ( m_rearLineOfScrimmagePos.Get() != vec3_origin ) m_rearLineOfScrimmagePos.GetForModify() = vec3_origin;
	if ( m_rearLineOfScrimmageAngle.Get() != vec3_angle ) m_rearLineOfScrimmageAngle.GetForModify() = vec3_angle;
	m_scrimmageType = 0;
	// The original retains FRONT position/angle and the existing timers.
}

// S:310170. The center helper is called TWICE; its actual spawn fallback can
// advance the shared round-robin selector, so combining the calls is wrong.
void CTerrorPlayer::UpdateScrimmageSphere()
{
	m_scrimmageType = 2;
	Vector center = GetScrimmageCenter();
	float x = center.x - m_scrimmageSphereCenter.Get().x;
	float y = center.y - m_scrimmageSphereCenter.Get().y;
	float z = center.z - m_scrimmageSphereCenter.Get().z;
	float distance = sqrtf( ( y * y + z * z ) + x * x );
	float progress = z_ghost_los_expected_progress.GetFloat();
	// Native JBE also subtracts on unordered, rather than sanitizing to0.
	if ( distance > progress ) progress = 0.0f;
	else progress -= distance;
	center = GetScrimmageCenter();
	if ( m_scrimmageSphereCenter.Get() != center ) m_scrimmageSphereCenter.GetForModify() = center;
	double radius = GetLineOfScrimmageRange() + (double)progress;
	if ( (double)m_scrimmageSphereInitialRadius.Get() != radius ) m_scrimmageSphereInitialRadius.GetForModify() = (float)radius;
	float elapsed = 99999.8984375f;
	if ( m_stateEnterTimer.m_timestamp > 0.0f ) elapsed = (float)( (double)gpGlobals->curtime - (double)m_stateEnterTimer.m_timestamp );
	float startTime = gpGlobals->curtime - elapsed;
	if ( m_scrimmageStartTime.Get() != startTime ) m_scrimmageStartTime.GetForModify() = startTime;
}

// S:30A0D0 and actual bitwise CNetworkArray setters3099A0/309A20.
void CTerrorPlayer::UpdateScrimmageYardLines( float maximum )
{
	CEscapeRoute *route = GetCriticalPath();
	if ( !route )
	{
		for ( int i = 0; i < 5; ++i ) { m_yardLinePos.Set( i, vec3_origin ); m_yardLineAngles.Set( i, vec3_angle ); }
		return;
	}
	int index = route->GetPointIndexAtFlowDistance( m_flLineOfScrimmageFlow, FLOW_TOWARD_GOAL );
	float next = (float)GetCriticalPath()->GetPointFlowDistance( index, FLOW_FROM_START );
	if ( m_scrimmageType != 1 ) return;
	for ( int i = 0; i < 5; ++i )
	{
		float wanted = next - 360.0f;
		Vector position;
		QAngle angles;
		double found = 0.0;
		if ( !( maximum > wanted ) ) found = FindScrimmagePointAtFlow( wanted, true, &position, &angles );
		if ( found > 0.0 )
		{
			m_yardLinePos.Set( i, position );
			m_yardLineAngles.Set( i, angles );
			next = (float)found;
		}
		else { m_yardLinePos.Set( i, vec3_origin ); m_yardLineAngles.Set( i, vec3_angle ); }
	}
}

// S:3103B0. Both requested arguments are floats. State2's nonnegative-request
// early return precedes both byte clears and every timer/world operation.
void CTerrorPlayer::UpdateLineOfScrimmage( float frontFlow, float rearFlow )
{
	if ( m_scrimmageType == 2 && ( frontFlow >= 0.0f || rearFlow >= 0.0f ) ) return;
	m_bNativeGhostStateFlag11448 = false;
	m_bNativeGhostFinaleGate11490 = false;
	if ( z_scrimmage_sphere.GetBool() )
	{
		if ( GetTeamNumber() == TEAM_INFECTED && GetCriticalPath() &&
			!TheDirector->IsFinale() && !TheDirector->GetScenario()->IsFirstPanicEvent() ) UpdateScrimmageSphere();
		else ClearLineOfScrimmage();
		return;
	}
	m_scrimmageTimer.Start( z_scrimmage_creep_delay.GetFloat() );
	float flow;
	Vector position;
	QAngle angles;
	if ( !ComputeLineOfScrimmage( true, frontFlow, &flow, &position, &angles ) )
	{
		if ( GetTeamNumber() == TEAM_INFECTED ) DevMsg( "UpdateLineOfScrimmage: %s gets timer\n", GetPlayerName() );
		ClearLineOfScrimmage();
	}
	else
	{
		m_scrimmageType = 1;
		m_flLineOfScrimmageFlow = m_flLineOfScrimmageCurrentFlow = flow;
		if ( m_lineOfScrimmagePos.Get() != position ) m_lineOfScrimmagePos.GetForModify() = position;
		if ( m_lineOfScrimmageAngle.Get() != angles ) m_lineOfScrimmageAngle.GetForModify() = angles;
		DevMsg( "UpdateLineOfScrimmage: %s has a front line of scrimmage of %f flow (wanted %f flow)\n", GetPlayerName(), (double)m_flLineOfScrimmageFlow, (double)frontFlow );
		if ( ComputeLineOfScrimmage( false, rearFlow, &flow, &position, &angles ) )
		{
			m_flRearLineOfScrimmageFlow = m_flRearLineOfScrimmageCurrentFlow = flow;
			if ( m_rearLineOfScrimmagePos.Get() != position ) m_rearLineOfScrimmagePos.GetForModify() = position;
			if ( m_rearLineOfScrimmageAngle.Get() != angles ) m_rearLineOfScrimmageAngle.GetForModify() = angles;
		}
		else
		{
			m_flRearLineOfScrimmageFlow = 0.0f; // Native RETAINS current-rear11256 here.
			if ( m_rearLineOfScrimmagePos.Get() != vec3_origin ) m_rearLineOfScrimmagePos.GetForModify() = vec3_origin;
			if ( m_rearLineOfScrimmageAngle.Get() != vec3_angle ) m_rearLineOfScrimmageAngle.GetForModify() = vec3_angle;
		}
		DevMsg( "UpdateLineOfScrimmage: %s has a rear line of scrimmage of %f flow (wanted %f flow)\n", GetPlayerName(), (double)m_flRearLineOfScrimmageFlow, (double)rearFlow );
	}
	if ( GetTeamNumber() != TEAM_INFECTED || !GetCriticalPath() )
	{
		DevMsg( GetTeamNumber() == TEAM_INFECTED ? "UpdateLineOfScrimmage: %s has no escape route and gets timer\n" : "UpdateLineOfScrimmage: %s is non-zombie and gets timer\n", GetPlayerName() );
		ClearLineOfScrimmage();
	}
	if ( GetTeamNumber() == TEAM_INFECTED && !TheDirector->IsFinale() )
	{
		bool noLiving = true;
		bool allInBattlefield = TheDirector->GetScenario()->AreAllLivingSurvivorsInBattlefield( &noLiving );
		if ( !allInBattlefield || noLiving ) m_bNativeGhostFinaleGate11490 = true;
		else
		{
			DevMsg( "UpdateLineOfScrimmage: %s saw all survivors in the finale area - waiting for finale to start\n", GetPlayerName() );
			m_bNativeGhostFinaleGate11490 = false;
		}
	}
	UpdateScrimmageYardLines( m_flLineOfScrimmageFlow );
}

// S:317880. Every admitted player receives this original notification, not
// just infected bots. The real sender2707E0 decrements Tank448 BEFORE the loop.
void CTerrorPlayer::OnTankKilled( CTerrorPlayer *tank )
{
	if ( !tank || tank->GetTeamNumber() != TEAM_INFECTED || tank->GetZombieClass() != 8 || ( IsAlive() && !IsGhost() ) ) return;
	if ( m_scrimmageType == 1 )
	{
		CTerrorPlayer *leader = FindHighestFlowSurvivor( 0 );
		if ( leader )
		{
			float flow = (float)leader->GetFlowDistance( 0 );
			bool update = (double)m_flLineOfScrimmageFlow > GetLineOfScrimmageRange() + (double)flow;
			if ( !update )
			{
				leader = FindHighestFlowSurvivor( 0 ); // Original SECOND live selector.
				if ( leader )
				{
					flow = (float)leader->GetFlowDistance( 0 );
					update = (double)flow - GetLineOfScrimmageRange() > (double)m_flRearLineOfScrimmageFlow;
				}
			}
			if ( update )
			{
				DevMsg( "%s is updating line of scrimmage at tank battle end\n", GetPlayerName() );
				UpdateLineOfScrimmage( -1.0f, -1.0f );
			}
		}
	}
	else if ( m_scrimmageType == 2 )
	{
		DevMsg( "%s is updating line of scrimmage at tank battle end\n", GetPlayerName() );
		UpdateScrimmageSphere();
		float time = gpGlobals->curtime - z_scrimmage_creep_delay.GetFloat();
		if ( m_scrimmageStartTime.Get() != time ) m_scrimmageStartTime.GetForModify() = time;
	}
}
