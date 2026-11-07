#include "cbase.h"
#include "functorutils.h"
#include "NextBot/NextBot.h"
#include "NextBot/NextBotUtil.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBodyInterface.h"
#include "l4d2/player/terrorplayer.h"
#include "querycache.h"
#include "tier0/memdbgon.h"

extern ConVar nb_blind;

// These definitions replace the SDK knowledge lifecycle for L4D2. The canonical
// m_knownEntityVector is the native entity/position/recognition-time collection;
// it is NOT a second store alongside CKnownEntity history.
CBaseEntity *IVision::GetPrimaryKnownThreat() const
{
	CBaseEntity *entity = m_primaryThreat.Get();
	return entity && !IsIgnored( entity ) ? m_primaryThreat.Get() : NULL;
}

CBaseEntity *IVision::GetClosestKnown( const INextBotEntityFilter &filter ) const
{
	CBaseEntity *closest = NULL;
	float minimum = 1000000000.0f;
	const Vector &position = GetBot()->GetPosition();
	FOR_EACH_VEC( m_knownEntityVector, i )
	{
		CBaseEntity *entity = m_knownEntityVector[i].entity.Get();
		if ( !entity || !filter.IsAllowed( entity ) ) continue;
		const float range = ( entity->GetAbsOrigin() - position ).LengthSqr();
		if ( minimum > range ) { closest = entity; minimum = range; }
	}
	return closest;
}

CBaseEntity *IVision::GetClosestKnown( int team ) const
{
	CBaseEntity *closest = NULL;
	float minimum = 1000000000.0f;
	const Vector &position = GetBot()->GetPosition();
	FOR_EACH_VEC( m_knownEntityVector, i )
	{
		CBaseEntity *entity = m_knownEntityVector[i].entity.Get();
		if ( !entity || ( team != TEAM_ANY && entity->GetTeamNumber() != team ) ) continue;
		const float range = ( entity->GetAbsOrigin() - position ).LengthSqr();
		if ( minimum > range ) { closest = entity; minimum = range; }
	}
	return closest;
}

int IVision::GetKnownCount( int team, float rangeLimit ) const
{
	int count = 0;
	FOR_EACH_VEC( m_knownEntityVector, i )
	{
		CBaseEntity *entity = m_knownEntityVector[i].entity.Get();
		if ( entity && ( team == TEAM_ANY || entity->GetTeamNumber() == team ) &&
			( rangeLimit < 0.0f || GetBot()->IsRangeLessThan( entity, rangeLimit ) ) ) ++count;
	}
	return count;
}

// S3A7750. TEAM_ANY's apparently useless clock reads and unconditional 1e10
// result are original. The native invalid-team result is zero, not a minimum.
float IVision::GetTimeSinceVisible( int team ) const
{
	if ( team == TEAM_ANY )
	{
		for ( int i = 0; i < MAX_TEAMS; ++i )
		{
			const float timestamp = m_notVisibleTimer[i].GetStartTime();
			if ( timestamp > 0.0f && gpGlobals->curtime - timestamp < 10000000000.0f && timestamp > 0.0f )
				m_notVisibleTimer[i].GetElapsedTime();
		}
		return 10000000000.0f;
	}
	if ( (unsigned int)team > 31 ) return 0.0f;
	if ( m_notVisibleTimer[team].GetStartTime() <= 0.0f ) return (float)99999.898;
	return m_notVisibleTimer[team].GetElapsedTime();
}

// S4B06E0. Recognition equality is admitted. Raw access is separately exposed
// for the native incoming-warning reader; neither path fabricates visibility.
bool IVision::ForEachKnownEntity( IForEachKnownEntity &func )
{
	FOR_EACH_VEC( m_knownEntityVector, i )
	{
		const VisionKnownEntity &record = m_knownEntityVector[i];
		if ( gpGlobals->curtime >= record.recognizeAt && record.entity.Get() && !func.Inspect( record ) ) return false;
	}
	return true;
}

class CollectTerrorVisibleActors
{
public:
	explicit CollectTerrorVisibleActors( IVision *vision ) : m_vision( vision ) {}
	bool operator()( CBaseEntity *entity )
	{
		if ( m_vision->IsIgnored( entity ) || !entity->IsAlive() ) return true;
		CTerrorPlayer *player = ToTerrorPlayer( entity );
		if ( player && player->IsGhost() ) return true;
		if ( entity != m_vision->GetBot()->GetEntity() && m_vision->IsAbleToSee( entity, IVision::USE_FOV ) ) m_visible.AddToTail( entity );
		return true;
	}
	bool Contains( CBaseEntity *entity ) const
	{
		FOR_EACH_VEC( m_visible, i ) if ( entity->entindex() == m_visible[i]->entindex() ) return true;
		return false;
	}
	IVision *m_vision;
	CUtlVector< CBaseEntity * > m_visible;
};

// S4B1330. Old records are ordered-removed, not FastRemove'd. Invisible records
// survive until curtime-.5 is STRICTLY greater than their recognition deadline.
void IVision::UpdateRecognizedSet()
{
	VPROF_BUDGET( "IVision::UpdateRecognizedSet", "NextBot" );
	CollectTerrorVisibleActors visible( this );
	ForEachActor( visible );
	FOR_EACH_VEC( m_knownEntityVector, i )
	{
		VisionKnownEntity &record = m_knownEntityVector[i];
		CBaseEntity *entity = record.entity.Get();
		if ( !entity ) { m_knownEntityVector.Remove( i-- ); continue; }
		if ( visible.Contains( entity ) )
		{
			record.position = entity->GetAbsOrigin();
			m_notVisibleTimer[entity->GetTeamNumber()].Start();
		}
		else if ( gpGlobals->curtime - 0.5f > record.recognizeAt )
		{
			if ( GetBot()->IsDebugging( NEXTBOT_VISION ) )
				ConColorMsg( Color( 255, 0, 0, 255 ), "%3.2f: %s Lost sight of %s(#%d)\n", gpGlobals->curtime, GetBot()->GetDebugIdentifier(), entity->GetClassname(), entity->entindex() );
			GetBot()->OnLostSight( entity );
			m_knownEntityVector.Remove( i-- );
		}
	}
	FOR_EACH_VEC( visible.m_visible, i )
	{
		CBaseEntity *entity = visible.m_visible[i];
		int index;
		for ( index = 0; index < m_knownEntityVector.Count(); ++index ) if ( m_knownEntityVector[index].entity.Get() == entity ) break;
		if ( index == m_knownEntityVector.Count() )
		{
			VisionKnownEntity record;
			record.entity = entity;
			record.position = entity->GetAbsOrigin();
			record.recognizeAt = GetMinRecognizeTime() + gpGlobals->curtime;
			m_knownEntityVector.AddToTail( record );
		}
	}
}

class SelectTerrorPrimaryThreat : public IVision::IForEachKnownEntity
{
public:
	SelectTerrorPrimaryThreat( INextBot *bot, float previousUpdate ) : m_bot( bot ), m_previousUpdate( previousUpdate ), m_threat( NULL ) {}
	virtual bool Inspect( const VisionKnownEntity &record ) OVERRIDE
	{
		CBaseEntity *entity = record.entity.Get();
		CBaseCombatCharacter *actor = entity->MyCombatCharacterPointer();
		if ( actor && actor->IsAlive() && m_bot->IsEnemy( actor ) )
		{
			if ( m_threat ) m_threat = m_bot->GetIntentionInterface()->SelectMoreDangerousThreat( m_bot, m_bot->GetEntity(), m_threat, actor );
			else m_threat = actor;
		}
		if ( !( record.recognizeAt <= m_previousUpdate ) )
		{
			if ( m_bot->IsDebugging( NEXTBOT_VISION ) )
			{
				ConColorMsg( Color( 0, 255, 0, 255 ), "%3.2f: %s caught sight of %s(#%d)\n", gpGlobals->curtime, m_bot->GetDebugIdentifier(), entity->GetClassname(), entity->entindex() );
				IBody *body = m_bot->GetBodyInterface();
				if ( body ) NDebugOverlay::Line( body->GetEyePosition(), entity->EyePosition(), 255, 255, 0, false, 0.2f );
			}
			m_bot->OnSight( entity );
		}
		else if ( m_bot->IsDebugging( NEXTBOT_VISION ) )
		{
			IBody *body = m_bot->GetBodyInterface();
			if ( body ) NDebugOverlay::Line( body->GetEyePosition(), entity->EyePosition(), 255, 0, 0, false, 0.2f );
		}
		return true;
	}
	INextBot *m_bot;
	float m_previousUpdate;
	CBaseEntity *m_threat;
};

// S4B18B0: THIS is the real OnThreatChanged producer. Blind suppression clears
// without publishing an event and does not update the last-scan timestamp.
void IVision::Update()
{
	VPROF_BUDGET( "IVision::Update", "NextBotExpensive" );
	if ( !( gpGlobals->curtime >= m_scanTimer.m_timestamp.Get() ) ) return;
	m_scanTimer.Start( GetMinRecognizeTime() * 0.5f );
	const bool blind = nb_blind.GetBool();
	CBaseCombatCharacter *actor = GetBot()->GetEntity();
	if ( ( actor && actor->IsPlayer() && ( static_cast< CTerrorPlayer * >( actor )->GetSenseFlags() & 1 ) ) || blind )
	{
		m_knownEntityVector.RemoveAll();
		m_primaryThreat = NULL;
		return;
	}
	UpdateRecognizedSet();
	SelectTerrorPrimaryThreat select( GetBot(), m_lastVisionUpdateTimestamp );
	ForEachKnownEntity( select );
	if ( m_primaryThreat.Get() != select.m_threat )
	{
		m_primaryThreat = select.m_threat;
		GetBot()->OnThreatChanged( static_cast< CBaseCombatCharacter * >( m_primaryThreat.Get() ) );
	}
	m_lastVisionUpdateTimestamp = gpGlobals->curtime;
}

// S4B01D0. The output pointer is genuinely ignored by this original reader.
// Absence of the CACHED body has its original, narrowly proven true guard.
bool IVision::IsAbleToSee( CBaseEntity *subject, FieldOfViewCheckType checkFOV, Vector *visibleSpot ) const
{
	VPROF_BUDGET( "IVision::IsAbleToSee", "NextBotExpensive" );
	if ( !m_body ) return true;
	if ( GetBot()->IsRangeGreaterThan( subject, GetMaxVisionRange() ) ) return false;
	if ( GetBot()->GetEntity()->IsHiddenByFog( subject ) ) return false;
	if ( checkFOV == USE_FOV && !IsInFieldOfView( subject ) ) return false;
	if ( !IsVisibleEntityNoticed( subject ) ) return false;
	return IsLineOfSightClearToEntity( subject, NULL );
}

// S4B03C0. No SDK NAV-PVS pruning or additional visibility filter is inserted.
bool IVision::IsAbleToSee( const Vector &position, FieldOfViewCheckType checkFOV ) const
{
	VPROF_BUDGET( "IVision::IsAbleToSee", "NextBotExpensive" );
	if ( !m_body ) return true;
	if ( GetBot()->IsRangeGreaterThan( position, GetMaxVisionRange() ) ) return false;
	if ( GetBot()->GetEntity()->IsHiddenByFog( position ) ) return false;
	if ( checkFOV == USE_FOV && !IsInFieldOfView( position ) ) return false;
	return IsLineOfSightClear( position );
}

// S4AFE50/S4B05B0. Use IVision's actual cached cosine, not a recomputed
// SurvivorVision field. The two sign paths have different NaN/equality behavior.
bool IVision::IsInFieldOfView( CBaseEntity *subject ) const
{
	if ( IsInFieldOfView( subject->WorldSpaceCenter() ) ) return true;
	return IsInFieldOfView( subject->EyePosition() );
}

bool IVision::IsInFieldOfView( const Vector &position ) const
{
	if ( !m_body ) return true;
	const float cosine = m_cosHalfFOV;
	const Vector &view = m_body->GetViewVector();
	const Vector delta = position - m_body->GetEyePosition();
	const float dot = ( view.y * delta.y + view.x * delta.x ) + view.z * delta.z;
	const float squaredCone = ( ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z ) * cosine * cosine;
	if ( cosine > 0.0f )
	{
		if ( dot < 0.0f ) return false;
		return !( dot * dot < squaredCone );
	}
	if ( dot > 0.0f ) return true;
	return !( squaredCone < dot * dot );
}

bool IVision::IsLineOfSightClear( const Vector &position ) const
{
	VPROF_BUDGET( "IVision::IsLineOfSightClear", "NextBot" );
	VPROF_INCREMENT_COUNTER( "IVision::IsLineOfSightClear", 1 );
	NextBotVisionTraceFilter filter( GetBot()->GetEntity(), COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceLine( m_body->GetEyePosition(), position, 33579073u, &filter, &trace );
	return trace.fraction >= 1.0f && !trace.startsolid;
}

// S4B0EB0 ->1AF390. Original type3 cache, target may be hit, two valid key
// points, one-second hysteresis. Neither the SDK's nine-argument type2 call nor
// its uncached center/eyes/feet fallback has these semantics.
bool IVision::IsLineOfSightClearToEntity( const CBaseEntity *subject, Vector *visibleSpot ) const
{
	VPROF_INCREMENT_COUNTER( "IVision::IsLineOfSightClearToEntity", 1 );
	VPROF_BUDGET( "IVision::IsLineOfSightClearToEntity", "NextBotSpiky" );
	CBaseEntity *entity = const_cast< CBaseEntity * >( subject );
	return IsLineOfSightBetweenTwoEntitiesClear( m_body->GetEntity(), EOFFSET_MODE_EYEPOSITION,
		entity, EOFFSET_MODE_WORLDSPACE_CENTER, entity, COLLISION_GROUP_NONE,
		33579073u, VisionTraceFilterFunction, 1.0f, false );
}

// S4ACA30. Original context queries compare ENTITY origins and resolve exact
// equality to the second entity; NaN falls through to the first. No SDK
// CKnownEntity obsolete/last-position gate belongs in this native interface.
CBaseEntity *IIntention::SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const
{
	for ( INextBotEventResponder *sub = FirstContainedResponder(); sub; sub = NextContainedResponder( sub ) )
	{
		const IContextualQuery *query = dynamic_cast< const IContextualQuery * >( sub );
		if ( query )
		{
			CBaseEntity *result = query->SelectMoreDangerousThreat( me, subject, first, second );
			if ( result ) return result;
		}
	}
	const Vector &firstOrigin = first->GetAbsOrigin();
	const Vector &firstSubjectOrigin = subject->GetAbsOrigin();
	const float firstRange = ( firstSubjectOrigin - firstOrigin ).LengthSqr();
	const Vector &secondOrigin = second->GetAbsOrigin();
	const Vector &secondSubjectOrigin = subject->GetAbsOrigin();
	const float secondRange = ( secondSubjectOrigin - secondOrigin ).LengthSqr();
	return secondRange <= firstRange ? second : first;
}

// S2FA350/2FA360; constructor323260 initializes16408 to zero. Script class
// registration330690 exposes these exact readers/writers on CTerrorPlayer.
// Setter descriptor1030DB00 return type0 is VOID, not EAX's incidental arg.
int CTerrorPlayer::GetSenseFlags() const
{
	return m_senseFlags;
}

void CTerrorPlayer::SetSenseFlags( int flags )
{
	m_senseFlags = flags;
}
