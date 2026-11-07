//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_retreat_path.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

SurvivorLegsRetreat::SurvivorLegsRetreat( CBaseEntity *threat ) : m_threat( threat )
{
}

// Original server 103BD060.
ActionResult< SurvivorBot > SurvivorLegsRetreat::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( sb_path_lookahead_range.GetFloat() );
	m_path.Invalidate();
	return Continue();
}

// 103BD5A0. The failed-search fallback really is the unnormalized mirrored
// threat position, assembled as a trivial path. Do not replace it with a
// repaired search, teleport, randomized escape, or a fixed-distance offset.
void SurvivorLegsRetreat::ComputeRetreatPath( SurvivorBot *me )
{
	CBaseEntity *threat = m_threat.Get();
	SurvivorRetreatPathBuilder builder( me, threat, me->GetThreatDangerRange( threat ) );
	CNavArea *goal = builder.ComputePath();
	if ( goal )
	{
		m_path.AssemblePrecomputedPath( me, goal->GetCenter(), goal );
	}
	else
	{
		const Vector toThreat = threat->GetAbsOrigin() - me->GetAbsOrigin();
		m_path.BuildTrivialPath( me, me->GetAbsOrigin() - toThreat );
	}
}

// 103BD940.
ActionResult< SurvivorBot > SurvivorLegsRetreat::Update( SurvivorBot *me, float interval )
{
	CBaseEntity *threat = m_threat.Get();
	if ( !threat || !threat->IsAlive() )
		return Done( "Threat is gone" );
	const float dangerRange = me->GetThreatDangerRange( threat ) * 1.5f;
	if ( ( me->GetAbsOrigin() - threat->GetAbsOrigin() ).LengthSqr() > dangerRange * dangerRange )
		return Done( "Retreated a safe distance" );

	if ( me->GetVisionInterface()->IsAbleToSee( threat, IVision::DISREGARD_FOV, NULL ) )
	{
		m_lastThreatVisibleTimer.Start();
	}
	else if ( m_lastThreatVisibleTimer.GetElapsedTime() > 1.0f )
	{
		char modifiers[256];
		V_snprintf( modifiers, sizeof( modifiers ), "Subject:%s", GetSurvivorBotSubjectName( threat ) );
		me->SpeakIfAllowed( "SurvivorBotRetreatedOutOfSight", 1, modifiers );
		return Done( "Retreated out of sight of threat" );
	}

	const SurvivorKnownEntity *known = me->GetIntentionInterface()->GetPrimaryKnownThreat();
	if ( known && known->GetEntity()->entindex() != threat->entindex() )
	{
		CBaseEntity *moreDangerous = me->GetIntentionInterface()->SelectMoreDangerousThreat( me, me, threat, known->GetEntity() );
		if ( moreDangerous == known->GetEntity() )
		{
			if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
				ConColorMsg( Color( 255, 255, 0 ), "%3.2f: %s: Changing retreat threat to more dangerous one\n", gpGlobals->curtime, me->GetPlayerName() );
			m_threat = known->GetEntity();
			m_path.Invalidate();
		}
	}
	threat = m_threat.Get();
	if ( !m_path.IsValid() || ( threat->GetAbsOrigin() - m_lastThreatPosition ).LengthSqr() > 2500.0f )
	{
		ComputeRetreatPath( me );
		m_lastThreatPosition = threat->GetAbsOrigin();
	}
	m_path.Update( me );
	return Continue();
}

// 103BD7C0. The command changes the threat but does not invalidate the path
// or restart the visibility timer; preserve the next Update's native tests.
EventDesiredResult< SurvivorBot > SurvivorLegsRetreat::OnCommandRetreat( SurvivorBot *me, CBaseEntity *threat, float range )
{
	m_threat = threat;
	return TryToSustain();
}
