//========= Copyright Valve Corporation, All rights reserved. ============//
// Native L4D2 logic/point entity owners; see native_logic_evidence.txt.
#include "cbase.h"
#include "l4d2/entities/terror_world_logic.h"
#include "entitylist.h"
#include "eventqueue.h"
#include "filters.h"
#include "igameevents.h"
#include "tier0/icommandline.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/entities/visibility_monitor.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_gamemode, CInfoGameMode );

BEGIN_DATADESC( CInfoGameMode )
	DEFINE_INPUTFUNC( FIELD_VOID, "PostSpawnActivate", InputPostSpawnActivate ),
	DEFINE_INPUTFUNC( FIELD_VOID, "PreRoundActivate", InputPreRoundActivate ),
	DEFINE_INPUTFUNC( FIELD_VOID, "FireScavengeMatchStart", InputFireScavengeMatchStart ),
	DEFINE_OUTPUT( m_OnCoop, "OnCoop" ),
	DEFINE_OUTPUT( m_OnVersus, "OnVersus" ),
	DEFINE_OUTPUT( m_OnSurvival, "OnSurvival" ),
	DEFINE_OUTPUT( m_OnScavenge, "OnScavenge" ),
	DEFINE_OUTPUT( m_OnCoopPreRound, "OnCoopPostIO" ),
	DEFINE_OUTPUT( m_OnVersusPreRound, "OnVersusPostIO" ),
	DEFINE_OUTPUT( m_OnSurvivalPreRound, "OnSurvivalPostIO" ),
	DEFINE_OUTPUT( m_OnScavengePreRound, "OnScavengePostIO" ),
	DEFINE_OUTPUT( m_OnScavengeMatchStart, "OnScavengeMatchStart" ),
	DEFINE_OUTPUT( m_OnNavAnalyze, "OnNavAnalyze" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( logic_versus_random, CVersusRandom );

BEGIN_DATADESC( CVersusRandom )
	DEFINE_INPUTFUNC( FIELD_VOID, "PickRandom", InputPickRandom ),
	DEFINE_OUTPUT( m_OnRandom[0], "OnRandom01" ),
	DEFINE_OUTPUT( m_OnRandom[1], "OnRandom02" ),
	DEFINE_OUTPUT( m_OnRandom[2], "OnRandom03" ),
	DEFINE_OUTPUT( m_OnRandom[3], "OnRandom04" ),
	DEFINE_OUTPUT( m_OnRandom[4], "OnRandom05" ),
	DEFINE_OUTPUT( m_OnRandom[5], "OnRandom06" ),
	DEFINE_OUTPUT( m_OnRandom[6], "OnRandom07" ),
	DEFINE_OUTPUT( m_OnRandom[7], "OnRandom08" ),
	DEFINE_OUTPUT( m_OnRandom[8], "OnRandom09" ),
	DEFINE_OUTPUT( m_OnRandom[9], "OnRandom10" ),
	DEFINE_OUTPUT( m_OnRandom[10], "OnRandom11" ),
	DEFINE_OUTPUT( m_OnRandom[11], "OnRandom12" ),
	DEFINE_OUTPUT( m_OnRandom[12], "OnRandom13" ),
	DEFINE_OUTPUT( m_OnRandom[13], "OnRandom14" ),
	DEFINE_OUTPUT( m_OnRandom[14], "OnRandom15" ),
	DEFINE_OUTPUT( m_OnRandom[15], "OnRandom16" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( logic_game_event, CLogicGameEvent );

BEGIN_DATADESC( CLogicGameEvent )
	DEFINE_KEYFIELD( m_iszEventName, FIELD_STRING, "eventName" ),
	DEFINE_INPUTFUNC( FIELD_INPUT, "FireEvent", InputFireEvent ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( info_game_event_proxy, CInfoGameEventProxy );

BEGIN_DATADESC( CInfoGameEventProxy )
	DEFINE_KEYFIELD( m_iszEventName, FIELD_STRING, "event_name" ),
	DEFINE_KEYFIELD( m_flRange, FIELD_FLOAT, "range" ),
	DEFINE_INPUTFUNC( FIELD_STRING, "GenerateGameEvent", InputGenerateGameEvent ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( point_entity_finder, CPointEntityFinder );

BEGIN_DATADESC( CPointEntityFinder )
	DEFINE_KEYFIELD( m_FindMethod, FIELD_INTEGER, "method" ),
	DEFINE_KEYFIELD( m_iFilterName, FIELD_STRING, "filtername" ),
	DEFINE_FIELD( m_hFilter, FIELD_EHANDLE ),
	DEFINE_KEYFIELD( m_iRefName, FIELD_STRING, "referencename" ),
	DEFINE_FIELD( m_hReference, FIELD_EHANDLE ),
	DEFINE_OUTPUT( m_OnFoundEntity, "OnFoundEntity" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "FindEntity", InputFindEntity ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( point_broadcastclientcommand, CPointBroadcastClientCommand );
BEGIN_DATADESC( CPointBroadcastClientCommand )
	DEFINE_INPUTFUNC( FIELD_STRING, "Command", InputCommand ),
END_DATADESC()


int CInfoGameMode::ObjectCaps()
{ return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

int CInfoGameEventProxy::UpdateTransmitState()
{ return SetTransmitState( FL_EDICT_ALWAYS ); }

void CInfoGameMode::Activate()
{
	BaseClass::Activate();
	if ( CommandLine()->FindParm( "-navforceanalyze" ) || CommandLine()->FindParm( "-navanalyze" ) )
		m_OnNavAnalyze.FireOutput( NULL, this );
}
void CInfoGameMode::InputPostSpawnActivate( inputdata_t &inputdata )
{
	if ( TerrorGameRules()->IsSurvivalMode() ) m_OnSurvival.FireOutput( NULL, this );
	else if ( TerrorGameRules()->IsVersusMode() ) m_OnVersus.FireOutput( NULL, this );
	else if ( TerrorGameRules()->IsCoopMode() ) m_OnCoop.FireOutput( NULL, this );
	else if ( TerrorGameRules()->IsScavengeMode() ) m_OnScavenge.FireOutput( NULL, this );
	// S29A247: finish immediate map I/O, THEN retire its deleted entities.
	g_EventQueue.ServiceEvents();
	gEntList.CleanupDeleteList();
}
void CInfoGameMode::InputPreRoundActivate( inputdata_t &inputdata )
{
	if ( TerrorGameRules()->IsSurvivalMode() ) m_OnSurvivalPreRound.FireOutput( NULL, this );
	else if ( TerrorGameRules()->IsVersusMode() ) m_OnVersusPreRound.FireOutput( NULL, this );
	else if ( TerrorGameRules()->IsCoopMode() ) m_OnCoopPreRound.FireOutput( NULL, this );
	else if ( TerrorGameRules()->IsScavengeMode() ) m_OnScavengePreRound.FireOutput( NULL, this );
}
void CInfoGameMode::InputFireScavengeMatchStart( inputdata_t &inputdata )
{
	m_OnScavengeMatchStart.FireOutput( this, this );
}

CVersusRandom::CVersusRandom() : m_nOutputCount( 0 ), m_nReplayIndex( 0 )
{
	for ( int i = 0; i < ARRAYSIZE( m_nOutput ); ++i ) m_nOutput[i] = -1;
	m_History.EnsureCapacity( 8 );
}
void CVersusRandom::Spawn()
{
	// Native does not call BaseClass::Spawn or persist history in the datamap.
	m_nOutputCount = 0;
	for ( int i = 0; i < ARRAYSIZE( m_OnRandom ); ++i )
		if ( m_OnRandom[i].NumberOfElements() ) m_nOutput[m_nOutputCount++] = i;
	if ( !m_nOutputCount )
		DevWarning( "logic_versus_random entity %s has no outputs!\n", STRING( GetEntityName() ) );
	m_History.RemoveAll();
	m_nReplayIndex = 0;
}
void CVersusRandom::PickRandom( bool record, CBaseEntity *activator )
{
	if ( !m_nOutputCount )
	{
		DevWarning( "logic_versus_random entity %s being asked to pick random number but has no outputs!\n", STRING( GetEntityName() ) );
		return;
	}
	const int choice = m_nOutput[RandomInt( 0, m_nOutputCount - 1 )];
	m_OnRandom[choice].FireOutput( activator, this );
	// Native records after firing the output, not before it.
	if ( record ) m_History.AddToTail( choice );
}
void CVersusRandom::InputPickRandom( inputdata_t &inputdata )
{
	// This reads the rules' replicated native1173 byte, not Director1253.
	if ( !TerrorGameRules()->IsScavengeSecondHalf() )
	{
		PickRandom( true, inputdata.pActivator );
		return;
	}
	if ( m_History.Count() && m_nReplayIndex < m_History.Count() )
	{
		const int choice = m_History[m_nReplayIndex++];
		m_OnRandom[choice].FireOutput( inputdata.pActivator, this );
		return;
	}
	if ( m_History.Count() )
		DevWarning( "logic_versus_random entity %s being asked to pick more random numbers in second round than in the first round!\n", STRING( GetEntityName() ) );
	else
		DevWarning( "logic_versus_random entity %s being asked to pick random numbers in second round when none were picked first round!\n", STRING( GetEntityName() ) );
	PickRandom( false, inputdata.pActivator );
}

void CLogicGameEvent::InputFireEvent( inputdata_t &inputdata )
{
	IGameEvent *event = gameeventmanager->CreateEvent( STRING( m_iszEventName ) );
	if ( !event ) return;
	if ( HasSpawnFlags( 1 ) && inputdata.pActivator && inputdata.pActivator->IsPlayer() )
		event->SetInt( "userid", static_cast< CBasePlayer * >( inputdata.pActivator )->GetUserID() );
	gameeventmanager->FireEvent( event );
}

void CInfoGameEventProxy::Spawn()
{
	BaseClass::Spawn();
	m_flRange = static_cast< float >( static_cast< double >( m_flRange ) * 12.0 );
	if ( HasSpawnFlags( 1 ) ) VisibilityMonitor_AddEntity( this, m_flRange, OnVisible, NULL );
}
void CInfoGameEventProxy::InputGenerateGameEvent( inputdata_t &inputdata )
{
	// The input's string value is intentionally unused.
	CBasePlayer *player = inputdata.pActivator && inputdata.pActivator->IsPlayer() ?
		static_cast< CBasePlayer * >( inputdata.pActivator ) : NULL;
	IGameEvent *event = gameeventmanager->CreateEvent( STRING( m_iszEventName ) );
	if ( !event ) return;
	if ( player ) event->SetInt( "userid", player->GetUserID() );
	event->SetInt( "subject", entindex() );
	gameeventmanager->FireEvent( event );
}
bool CInfoGameEventProxy::OnVisible( CBaseEntity *entity, CBasePlayer *observer )
{
	CInfoGameEventProxy *proxy = dynamic_cast< CInfoGameEventProxy * >( entity );
	if ( !proxy ) return true;
	IGameEvent *event = gameeventmanager->CreateEvent( STRING( proxy->m_iszEventName ) );
	if ( event )
	{
		event->SetInt( "userid", observer->GetUserID() );
		event->SetInt( "subject", proxy->entindex() );
		gameeventmanager->FireEvent( event );
	}
	return false; // Consume visibility without the default entity_visible event.
}

void CPointEntityFinder::Activate()
{
	if ( m_iFilterName != NULL_STRING )
		m_hFilter = dynamic_cast< CBaseFilter * >( gEntList.FindEntityByName( NULL, m_iFilterName ) );
	BaseClass::Activate();
}
void CPointEntityFinder::FindEntity()
{
	if ( m_iRefName != NULL_STRING ) m_hReference = gEntList.FindEntityByName( NULL, m_iRefName );
	switch ( m_FindMethod )
	{
	case 0: case 1: FindByDistance(); break;
	case 2: FindRandom(); break;
	// Unsupported methods retain the previous result, as native18BB00 does.
	}
}
void CPointEntityFinder::FindByDistance()
{
	m_hFoundEntity = NULL;
	CBaseFilter *filter = m_hFilter.Get();
	float bestDistance = 0.0f;
	for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
	{
		if ( FClassnameIs( entity, "worldspawn" ) || FClassnameIs( entity, "soundent" ) ||
			FClassnameIs( entity, "player_manager" ) || FClassnameIs( entity, "bodyque" ) ||
			FClassnameIs( entity, "ai_network" ) || entity == this || ( filter && !filter->PassesFilter( this, entity ) ) )
			continue;
		// Native resolves the reference after each filter invocation.
		CBaseEntity *reference = m_hReference.Get();
		const Vector origin = reference ? reference->GetAbsOrigin() : GetAbsOrigin();
		if ( !m_hFoundEntity.Get() )
		{
			m_hFoundEntity = entity;
			const Vector delta = entity->GetAbsOrigin() - origin;
			bestDistance = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		}
		const Vector delta = entity->GetAbsOrigin() - origin;
		const float distance = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		// Strict comparisons preserve first-on-ties and COMISS/JBE NaN polarity.
		if ( ( m_FindMethod == 0 && bestDistance > distance ) || ( m_FindMethod == 1 && distance > bestDistance ) )
		{
			m_hFoundEntity = entity;
			bestDistance = distance;
		}
	}
}
void CPointEntityFinder::FindRandom()
{
	m_hFoundEntity = NULL;
	CBaseFilter *filter = m_hFilter.Get();
	CUtlVector< CBaseEntity * > candidates;
	for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
	{
		// Native random mode REQUIRES a filter and does not exclude self/world.
		if ( filter && filter->PassesFilter( this, entity ) ) candidates.AddToTail( entity );
	}
	if ( candidates.Count() ) m_hFoundEntity = candidates[RandomInt( 0, candidates.Count() - 1 )];
}
void CPointEntityFinder::InputFindEntity( inputdata_t &inputdata )
{
	FindEntity();
	// Native uses the found entity as caller, including NULL on failure.
	m_OnFoundEntity.FireOutput( inputdata.pActivator, m_hFoundEntity.Get() );
}

void CPointBroadcastClientCommand::InputCommand( inputdata_t &inputdata )
{
	if ( !inputdata.value.String()[0] ) return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && player->edict() && player->edict()->GetUnknown() )
			engine->ClientCommand( player->edict(), "%s\n", inputdata.value.String() );
	}
}