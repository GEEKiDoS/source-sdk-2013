//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/trigger_finale.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/nav/terror_nav_area.h"
#include "triggers.h"
#include "logicrelay.h"
#include "igameevents.h"
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

LINK_ENTITY_TO_CLASS( trigger_finale, CFinaleTrigger );
LINK_ENTITY_TO_CLASS( finale_trigger, CFinaleTrigger );
IMPLEMENT_SERVERCLASS_ST( CFinaleTrigger, DT_FinaleTrigger )
	SendPropInt( SENDINFO( m_triggerState ), 32 ),
	SendPropBool( SENDINFO( m_bDisabled ) ),
END_SEND_TABLE()
BEGIN_DATADESC( CFinaleTrigger )
	{ FIELD_VOID, "OnFinaleEscapeStarted", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinaleEscapeStarted", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinaleEscapeStarted ) },
	{ FIELD_VOID, "OnFinaleEscapeVehicleReadyForSurvivors", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinaleEscapeVehicleReadyForSurvivors", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinaleEscapeVehicleReadyForSurvivors ) },
	{ FIELD_VOID, "OnFinaleEscapeForceSurvivorPositions", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinaleEscapeForceSurvivorPositions", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinaleEscapeForceSurvivorPositions ) },
	{ FIELD_VOID, "OnFinaleEscapeFinished", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinaleEscapeFinished", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinaleEscapeFinished ) },
	{ FIELD_VOID, "OnFinalePause", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinalePause", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinalePause ) },
	{ FIELD_VOID, "OnFinaleWon", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinaleWon", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinaleWon ) },
	{ FIELD_VOID, "OnFinaleLost", { 0, 0 }, 1, FTYPEDESC_INPUT, "FinaleLost", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputFinaleLost ) },
	{ FIELD_VOID, "OnForceFinaleStart", { 0, 0 }, 1, FTYPEDESC_INPUT, "ForceFinaleStart", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputForceFinaleStart ) },
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	{ FIELD_VOID, "InputGauntletForceTankSpawn", { 0, 0 }, 1, FTYPEDESC_INPUT, "ForceTankSpawn", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputForceTankSpawn ) },
	DEFINE_INPUTFUNC( FIELD_VOID, "GauntletStopPanic", InputGauntletStopPanic ),
	DEFINE_INPUTFUNC( FIELD_VOID, "AdvanceFinaleState", InputAdvanceFinaleState ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SacrificeEscapeFailed", InputSacrificeEscapeFailed ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SacrificeEscapeSucceeded", InputSacrificeEscapeSucceeded ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SacrificePlayerBeginsRun", InputSacrificePlayerBeginsRun ),
	DEFINE_OUTPUT( m_OnFinaleEscapeStarted, "FinaleEscapeStarted" ),
	DEFINE_OUTPUT( m_OnFinaleWon, "FinaleWon" ),
	DEFINE_OUTPUT( m_OnFinaleLost, "FinaleLost" ),
	DEFINE_OUTPUT( m_OnFirstUseStart, "FirstUseStart" ),
	DEFINE_OUTPUT( m_OnUseStart, "UseStart" ),
	DEFINE_OUTPUT( m_OnFinaleStart, "FinaleStart" ),
	DEFINE_OUTPUT( m_OnFinalePause, "FinalePause" ),
	DEFINE_OUTPUT( m_OnEscapeVehicleLeaving, "EscapeVehicleLeaving" ),
	DEFINE_KEYFIELD( m_firstUseDelay, FIELD_FLOAT, "FirstUseDelay" ),
	DEFINE_KEYFIELD( m_useDelay, FIELD_FLOAT, "UseDelay" ),
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "startdisabled" ),
	DEFINE_KEYFIELD( m_type, FIELD_INTEGER, "type" ),
	DEFINE_KEYFIELD( m_scriptfile, FIELD_STRING, "scriptfile" ),
	DEFINE_KEYFIELD( m_flVersusTravelCompletion, FIELD_FLOAT, "VersusTravelCompletion" ),
	DEFINE_KEYFIELD( m_bIsSacrificeFinale, FIELD_BOOLEAN, "IsSacrificeFinale" ),
	DEFINE_THINKFUNC( FirstUseDelayThink ),
	DEFINE_THINKFUNC( DelayedFinaleStartThink ),
	DEFINE_THINKFUNC( DelayedSurvivalStartThink ),
END_DATADESC()

namespace
{
enum { STATE_READY_FOR_FIRST_USE, STATE_DELAY_BETWEEN_USES, STATE_READY_FOR_FINALE_USE, STATE_DELAY_BEFORE_FINALE, STATE_USED };
CTerrorPlayer *FinaleConnectedTerrorPlayer( int index )
{
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( index ) );
	return player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() ? player : NULL;
}
void SpeakFinaleConcept( const char *concept, const char *modifiers, bool survivorSlotsOnly )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FinaleConnectedTerrorPlayer( i );
		if ( !player || ( survivorSlotsOnly && player->GetTeamNumber() != TEAM_SURVIVOR ) ) continue;
		if ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			player->SpeakIfAllowed( concept, 1, modifiers );
	}
}
CTerrorPlayer *ClosestSurvivorForFinale( const Vector &origin )
{
	CTerrorPlayer *closest = NULL;
	float distance = 100000000.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FinaleConnectedTerrorPlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || player->IsIncapacitated() ||
			player->IsBeingPounced() || player->IsTongueVictim() || player->IsPummelVictim() || player->IsJockeyVictim() ) continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ) continue;
		Vector delta = player->WorldSpaceCenter() - origin;
		float range = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( distance > range ) { distance = range; closest = player; }
	}
	return closest;
}
void FireSacrificeEvent( const char *name, CBaseEntity *activator )
{
	CTerrorPlayer *player = ToTerrorPlayer( activator );
	if ( !player ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( event ) { event->SetInt( "userid", player->GetUserID() ); gameeventmanager->FireEvent( event ); }
}
}
bool AreAllSurvivorsInFinaleArea( bool includeIncapacitated )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FinaleConnectedTerrorPlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || ( !includeIncapacitated && player->IsIncapacitated() ) ) continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( !area || !area->HasSpawnAttributes( TERROR_NAV_FINALE ) ) return false;
	}
	return true;
}
CFinaleTrigger::CFinaleTrigger()
{
	m_firstUseDelay = 0.0f;
	m_useDelay = 0.0f;
	m_triggerState = STATE_READY_FOR_FIRST_USE;
	m_type = 0;
	m_bIsSacrificeFinale = false;
	m_flVersusTravelCompletion = 0.2f;
}
int CFinaleTrigger::UpdateTransmitState() { return SetTransmitState( FL_EDICT_ALWAYS ); }
int CFinaleTrigger::ObjectCaps()
{
	int caps = BaseClass::ObjectCaps();
	return !m_bDisabled && ( m_triggerState == STATE_READY_FOR_FIRST_USE || m_triggerState == STATE_READY_FOR_FINALE_USE ) ? caps | 0x810 : caps;
}
void CFinaleTrigger::Spawn()
{
	m_escapeStarted = false;
	m_hActivator = NULL;
	BaseClass::Spawn();
	SetMoveType( MOVETYPE_NONE );
	if ( GetModelName() != NULL_STRING && STRING( GetModelName() )[0] )
	{
		PrecacheModel( STRING( GetModelName() ) );
		Precache();
		SetModel( STRING( GetModelName() ) );
		SetSolid( SOLID_VPHYSICS );
		CollisionProp()->UseTriggerBounds( true, 16.0f );
		m_takedamage = DAMAGE_NO;
		SetMaxHealth( 0 );
		SetHealth( 0 );
		m_triggerState = !( m_firstUseDelay > 0.0f ) ? STATE_READY_FOR_FINALE_USE : STATE_READY_FOR_FIRST_USE;
		SetClassname( "trigger_finale" );
		if ( TerrorGameRules()->IsVersusMode() ) TheDirector->GetVersusMode()->OnFinaleTriggerSpawned();
		TheDirector->GetScenario()->OnFinaleTriggerSpawned();
	}
	else
	{
		const Vector &origin = GetAbsOrigin();
		Warning( "trigger_finale at %.0f %.0f %0.f missing modelname\n", origin.x, origin.y, origin.z );
		UTIL_Remove( this );
	}
}
void CFinaleTrigger::InputEnable( inputdata_t &data ) { m_bDisabled = false; }
void CFinaleTrigger::InputDisable( inputdata_t &data ) { m_bDisabled = true; }
void CFinaleTrigger::InputFinaleEscapeStarted( inputdata_t &data )
{
	TheDirector->OnFinaleVehicleIncoming();
	m_escapeStarted = true;
	m_OnFinaleEscapeStarted.FireOutput( this, this );
}
void CFinaleTrigger::InputFinaleEscapeVehicleReadyForSurvivors( inputdata_t &data )
{
	TheDirector->OnFinaleVehicleReady();
	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "ScenarioName:%s", TheDirector->GetScenarioName() );
	SpeakFinaleConcept( "FinalVehicleArrived", modifiers, true );
	// S:295960 invokes a genuinely empty player callback, not a movement order.
}
void CFinaleTrigger::InputFinaleEscapeFinished( inputdata_t &data ) { TheDirector->OnFinaleWon(); }
void CFinaleTrigger::InputFinalePause( inputdata_t &data ) { m_OnFinalePause.FireOutput( this, this ); }
void CFinaleTrigger::InputFinaleWon( inputdata_t &data ) { m_OnFinaleWon.FireOutput( this, this ); }
void CFinaleTrigger::InputFinaleLost( inputdata_t &data ) { m_OnFinaleLost.FireOutput( this, this ); }
void CFinaleTrigger::InputForceTankSpawn( inputdata_t &data )
{
	if ( TheDirector->GetScenario()->GetFinaleType() == 1 ) TheDirector->ForceGauntletTank();
}
void CFinaleTrigger::InputGauntletStopPanic( inputdata_t &data )
{
	if ( TheDirector->GetScenario()->GetFinaleType() == 1 ) TheDirector->GetScenario()->StopGauntletPanic();
}
void CFinaleTrigger::InputSacrificeEscapeFailed( inputdata_t &data ) { TheDirector->GetScenario()->OnSacrificeFailed(); }
void CFinaleTrigger::InputSacrificeEscapeSucceeded( inputdata_t &data )
{
	FireSacrificeEvent( "player_complete_sacrifice", data.pActivator );
	TheDirector->GetScenario()->OnSacrificeSucceeded();
}
void CFinaleTrigger::InputSacrificePlayerBeginsRun( inputdata_t &data ) { FireSacrificeEvent( "player_begin_sacrifice_run", data.pActivator ); }
void CFinaleTrigger::StartFinale( CTerrorPlayer *activator )
{
	if ( !activator ) { m_triggerState = STATE_READY_FOR_FINALE_USE; return; }
	m_triggerState = STATE_USED;
	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "TriggeredBy:%s", activator->GetResponseCharacterName() );
	SpeakFinaleConcept( "FinaleTriggered", modifiers, false );
	IGameEvent *event = gameeventmanager->CreateEvent( "finale_radio_start" );
	if ( event ) { event->SetInt( "health", GetHealth() ); gameeventmanager->FireEvent( event ); }
	TheDirector->OnFinaleStarted( m_type );
	if ( m_scriptfile != NULL_STRING && STRING( m_scriptfile )[0] ) TheDirector->BeginLocalScript( STRING( m_scriptfile ) );
	m_OnFinaleStart.FireOutput( this, this );
}
void CFinaleTrigger::StartSurvival( CTerrorPlayer *activator )
{
	m_triggerState = STATE_USED;
	char modifiers[256];
	Q_strncpy( modifiers, "TriggeredBy:", sizeof( modifiers ) );
	// Main-approved S296340 adaptation for S296430's expired activator:
	// skip ONLY the activator-dependent name call; speech/start/output remain.
	if ( activator ) Q_strncat( modifiers, activator->GetResponseCharacterName(), sizeof( modifiers ) );
	SpeakFinaleConcept( "FinaleTriggered", modifiers, false );
	TheDirector->GetSurvivalMode()->OnRoundStarted( 0, true );
	m_OnFinaleStart.FireOutput( this, this );
}
void CFinaleTrigger::FirstUseDelayThink() { m_triggerState = STATE_READY_FOR_FINALE_USE; }
void CFinaleTrigger::DelayedFinaleStartThink() { StartFinale( m_hActivator.Get() ); }
void CFinaleTrigger::DelayedSurvivalStartThink() { StartSurvival( m_hActivator.Get() ); }
void CFinaleTrigger::Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE useType, float value )
{
	CTerrorPlayer *player = ToTerrorPlayer( activator );
	if ( m_bDisabled || !player || player->GetTeamNumber() != TEAM_SURVIVOR || TheDirector->IsFinale() || TheDirector->GetSurvivalMode()->HasStarted() ) return;
	if ( TerrorGameRules()->IsSurvivalMode() )
	{
		m_triggerState = STATE_USED;
		m_hActivator = player;
		SetThink( &CFinaleTrigger::DelayedSurvivalStartThink );
		SetNextThink( gpGlobals->curtime + m_useDelay );
		m_OnUseStart.FireOutput( player, this );
		return;
	}
	if ( !AreAllSurvivorsInFinaleArea( false ) )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "waiting_checkpoint_button_used" );
		if ( event ) { event->SetInt( "userid", player->GetUserID() ); gameeventmanager->FireEvent( event ); }
		return;
	}
	if ( m_triggerState == STATE_READY_FOR_FIRST_USE )
	{
		m_triggerState = STATE_DELAY_BETWEEN_USES;
		SetThink( &CFinaleTrigger::FirstUseDelayThink );
		SetNextThink( gpGlobals->curtime + m_firstUseDelay );
		m_OnFirstUseStart.FireOutput( this, this );
	}
	else if ( m_triggerState == STATE_READY_FOR_FINALE_USE )
	{
		m_triggerState = STATE_USED;
		m_hActivator = player;
		SetThink( &CFinaleTrigger::DelayedFinaleStartThink );
		SetNextThink( gpGlobals->curtime + m_useDelay );
		m_OnUseStart.FireOutput( player, this );
	}
}
void CFinaleTrigger::InputForceFinaleStart( inputdata_t &data )
{
	if ( !TheDirector->IsFinale() && ( TerrorGameRules()->IsSurvivalMode() || AreAllSurvivorsInFinaleArea( true ) ) )
	{
		CTerrorPlayer *player = ClosestSurvivorForFinale( GetAbsOrigin() );
		if ( player ) StartFinale( player );
	}
}
void CFinaleTrigger::InputAdvanceFinaleState( inputdata_t &data )
{
	if ( !m_bDisabled && m_type == 2 && ( TerrorGameRules()->IsSurvivalMode() || AreAllSurvivorsInFinaleArea( true ) ) )
	{
		CTerrorPlayer *player = ClosestSurvivorForFinale( GetAbsOrigin() );
		if ( player )
		{
			if ( !TheDirector->IsFinale() ) StartFinale( player );
			else TheDirector->GetScenario()->AdvanceStage();
		}
	}
}
void CFinaleTrigger::InputFinaleEscapeForceSurvivorPositions( inputdata_t &data )
{
	if ( !TheDirector->IsFinale() && !TheDirector->GetScriptedMode()->IsActive() ) return;
	CUtlVector< CTerrorPlayer * > escaped;
	CBaseTrigger *trigger = dynamic_cast< CBaseTrigger * >( data.pCaller );
	CLogicRelay *relay = trigger ? NULL : dynamic_cast< CLogicRelay * >( data.pCaller );
	CUtlVectorFixedGrowable< CLogicRelay *, 8 > visited;
	while ( relay )
	{
		// Main-approved S296960 adaptation: stop at the first revisited relay.
		// Leave the accumulated trigger result unchanged; acyclic order is exact.
		if ( visited.HasElement( relay ) ) break;
		visited.AddToTail( relay );
		CBaseEntity *caller = relay->GetLastCaller();
		trigger = dynamic_cast< CBaseTrigger * >( caller );
		if ( trigger ) break;
		relay = dynamic_cast< CLogicRelay * >( caller );
	}
	if ( trigger )
	{
		const CUtlVector< EHANDLE > &touching = trigger->GetTouchingEntities();
		FOR_EACH_VEC( touching, i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( touching[i].Get() );
			if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() ) escaped.AddToTail( player );
		}
	}
	else
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = FinaleConnectedTerrorPlayer( i );
			if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() && !player->IsIncapacitated() && !player->IsHangingFromLedge() && !player->IsDominated() ) escaped.AddToTail( player );
		}
	}
	TheDirector->OnFinaleVehicleLeaving( escaped );
	TerrorGameRules()->UpdateSacrificeEscapees();
	m_OnEscapeVehicleLeaving.FireOutput( this, this );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
