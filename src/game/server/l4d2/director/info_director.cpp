//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/info_director.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "tier1/strtools.h"
#include "tier0/memdbgon.h"

// S:594330 factory -> 266E60 allocator -> 262910 constructor, which only runs
// the CPointEntity base and the 13 COutputEvent constructors.
LINK_ENTITY_TO_CLASS( info_director, CInfoDirector );

// S:2725F0 / 7A51B4: 18 inputs then 13 outputs, in native order.
BEGIN_DATADESC( CInfoDirector )
	DEFINE_INPUTFUNC( FIELD_VOID, "PanicEvent", InputPanicEvent ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ScriptedPanicEvent", InputScriptedPanicEvent ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForcePanicEvent", InputForcePanicEvent ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EndCustomScriptedStage", InputEndCustomScriptedStage ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForceSurvivorPositions", InputForceSurvivorPositions ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ReleaseSurvivorPositions", InputReleaseSurvivorPositions ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RefreshInitialSpawnPositions", InputRefreshInitialSpawnPositions ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StartIntro", InputStartIntro ),
	DEFINE_INPUTFUNC( FIELD_VOID, "FinishIntro", InputFinishIntro ),
	DEFINE_INPUTFUNC( FIELD_STRING, "FireConceptToAny", InputFireConceptToAny ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "IncrementTeamScore", InputIncrementTeamScore ),
	DEFINE_INPUTFUNC( FIELD_STRING, "BeginScript", InputBeginScript ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EndScript", InputEndScript ),
	DEFINE_INPUTFUNC( FIELD_STRING, "CreateNewJournal", InputOpenJournalForWrite ),
	DEFINE_INPUTFUNC( FIELD_STRING, "WriteToJournal", InputWriteToJournal ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ExecuteJournal", InputExecuteJournalCategory ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableTankFrustration", InputEnableTankFrustration ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableTankFrustration", InputDisableTankFrustration ),

	DEFINE_OUTPUT( m_OnGameplayStart, "OnGameplayStart" ),
	DEFINE_OUTPUT( m_OnPanicEventFinished, "OnPanicEventFinished" ),
	DEFINE_OUTPUT( m_OnCustomPanicStageFinished, "OnCustomPanicStageFinished" ),
	DEFINE_OUTPUT( m_OnTeamScored, "OnTeamScored" ),
	DEFINE_OUTPUT( m_OnScavengeRoundStart, "OnScavengeRoundStart" ),
	DEFINE_OUTPUT( m_OnScavengeOvertimeStart, "OnScavengeOvertimeStart" ),
	DEFINE_OUTPUT( m_OnScavengeOvertimeCancel, "OnScavengeOvertimeCancel" ),
	DEFINE_OUTPUT( m_OnScavengeTimerExpired, "OnScavengeTimerExpired" ),
	DEFINE_OUTPUT( m_OnScavengeIntensityChanged, "OnScavengeIntensityChanged" ),
	{ FIELD_CUSTOM, "m_OnOnUserDefinedScriptEvent[0]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnUserDefinedScriptEvent[0] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnUserDefinedScriptEvent1", eventFuncs },
	{ FIELD_CUSTOM, "m_OnOnUserDefinedScriptEvent[1]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnUserDefinedScriptEvent[1] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnUserDefinedScriptEvent2", eventFuncs },
	{ FIELD_CUSTOM, "m_OnOnUserDefinedScriptEvent[2]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnUserDefinedScriptEvent[2] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnUserDefinedScriptEvent3", eventFuncs },
	{ FIELD_CUSTOM, "m_OnOnUserDefinedScriptEvent[3]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnUserDefinedScriptEvent[3] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnUserDefinedScriptEvent4", eventFuncs },
END_DATADESC()

// The panic inputs pass the activator only when it is a player; no team,
// life or control filter is applied here.
static CTerrorPlayer *PanicActivator( const inputdata_t &inputdata )
{
	CBaseEntity *activator = inputdata.pActivator;
	return activator && activator->IsPlayer() ? static_cast< CTerrorPlayer * >( activator ) : NULL;
}

// Survival arbitration runs first; only its "not handled" result 2 reaches the
// scenario owner.
static void DispatchScriptedPanic( CTerrorPlayer *activator, int reason )
{
	if ( TheDirector->GetSurvivalMode()->OnScriptedPanicEvent( activator, reason ) == 2 )
		TheDirector->GetScenario()->OnScriptedPanicEvent( activator, reason );
}

// S:25E2D0.
void CInfoDirector::InputPanicEvent( inputdata_t &inputdata )
{
	DispatchScriptedPanic( PanicActivator( inputdata ), 0 );
}

// S:26ED10. The script restarts before the panic is dispatched.
void CInfoDirector::InputScriptedPanicEvent( inputdata_t &inputdata )
{
	const char *script = inputdata.value.String();
	TheDirector->StopLocalScript();          // S:266940
	TheDirector->RunScript( script, 2, false ); // S:26AC50
	DispatchScriptedPanic( PanicActivator( inputdata ), 3 );
}

// S:25E320.
void CInfoDirector::InputForcePanicEvent( inputdata_t &inputdata )
{
	DispatchScriptedPanic( PanicActivator( inputdata ), 2 );
}

// S:25CB90.
void CInfoDirector::InputEndCustomScriptedStage( inputdata_t &inputdata )
{
	TheDirector->GetScenario()->QueueStageAdvance(); // S:287830
}

// S:26F7A0 collects every connected, alive team-2 player, ghosts included.
static void CollectSurvivorsForPositions( CUtlVector< CTerrorPlayer * > &survivors )
{
	CollectTerrorPlayers( survivors, TEAM_SURVIVOR, 2 | 4, 3, 9 );
}

// S:270A00.
void CInfoDirector::InputForceSurvivorPositions( inputdata_t &inputdata )
{
	CUtlVector< CTerrorPlayer * > survivors;
	CollectSurvivorsForPositions( survivors );
	TheDirector->PositionFinaleEscapees( survivors ); // S:26DC90
}

// S:270A40.
void CInfoDirector::InputReleaseSurvivorPositions( inputdata_t &inputdata )
{
	CUtlVector< CTerrorPlayer * > survivors;
	CollectSurvivorsForPositions( survivors );
	for ( int i = 0; i < survivors.Count(); ++i )
		survivors[i]->SetSurvivorPosition( NULL ); // S:3170F0
}

// S:270A90. One origin/angles pair is reused across survivors, as native's
// stack locals are; if the rules spawn query fails it keeps the previous
// result. The initial value is undefined natively and zero here.
void CInfoDirector::InputRefreshInitialSpawnPositions( inputdata_t &inputdata )
{
	CUtlVector< CTerrorPlayer * > survivors;
	CollectSurvivorsForPositions( survivors );
	Vector origin = vec3_origin;
	QAngle angles = vec3_angle;
	for ( int i = 0; i < survivors.Count(); ++i )
	{
		CTerrorPlayer *survivor = survivors[i];
		if ( !survivor )
			continue;
		TerrorGameRules()->GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &origin, &angles, NULL ); // S:2BDCD0
		survivor->Teleport( &origin, &angles, NULL );
		survivor->ClearLastKnownArea();      // player vtable+1328
	}

	CUtlVector< CTerrorPlayer * > ghosts;
	CollectTerrorPlayers( ghosts, TEAM_INFECTED, 4, 3, 9 ); // S:25E520 with filter {3,4,3,9}
	for ( int i = 0; i < ghosts.Count(); ++i )
	{
		if ( ghosts[i] )
			ghosts[i]->WarpGhostToInitialPosition( true ); // S:321A70
	}
}

// S:25E370.
void CInfoDirector::InputStartIntro( inputdata_t &inputdata )
{
	TerrorGameRules()->SetInIntro( true );   // S:2DFB60
	TheDirector->SetPlayingIntro( true );    // Director byte656
	TheDirector->GetScavengeMode()->CancelReadyCountdown(); // S:286470
	if ( gEntList.FindEntityByName( NULL, "branch_zoey" ) )
		TheDirector->PreferL4D1Survivor( 8 );
}

// S:25CD00 functor: non-bot Terror players that would see the MOTD get the
// host file panel.
class CFinishIntroHostFile
{
public:
	bool operator()( CBasePlayer *base )
	{
		if ( base->IsBot() )
			return true;
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( base );
		if ( player && player->ShouldShowMotD() )
			player->ShowHostFile();          // S:2FBBF0
		return true;
	}
};

// S:25F910 functor: every connected team-2 Terror player drops its forced
// animation sequence.
class CFinishIntroClearSequence
{
public:
	bool operator()( CBasePlayer *base )
	{
		if ( base->GetTeamNumber() != TEAM_SURVIVOR )
			return true;
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( base );
		if ( player )
			player->GetTerrorAnimState()->ClearSequenceOverride(); // S:33C170
		return true;
	}
};

// S:266560.
void CInfoDirector::InputFinishIntro( inputdata_t &inputdata )
{
	TerrorGameRules()->SetInIntro( false );
	TheDirector->SetPlayingIntro( false );
	if ( !TheDirector->IsInSecondHalfOfRound() ) // Director byte1253
	{
		CFinishIntroHostFile hostFile;
		ForEachPlayer( hostFile );           // S:25F890
	}
	CFinishIntroClearSequence clearSequence;
	ForEachPlayer( clearSequence );
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) ) // S:2864E0 -> S:2D9AB0
		TheDirector->GetScavengeMode()->StartReadyCountdown(); // S:2863A0
}

// S:25E3D0. Queued immediately (time 0) to any eligible speaker, with no
// criteria, issued by the activator.
void CInfoDirector::InputFireConceptToAny( inputdata_t &inputdata )
{
	AI_CriteriaSet criteria;
	ResponseQueueManager().Queue( inputdata.value.String(), criteria, 0.0f,
		ResponseTarget_t( ResponseTarget_t::ANY, NULL ), inputdata.pActivator ); // S:2A570
}

// S:25E440. A non-integer parameter scores zero.
void CInfoDirector::InputIncrementTeamScore( inputdata_t &inputdata )
{
	const int score = inputdata.value.FieldType() == FIELD_INTEGER ? inputdata.value.Int() : 0;
	TheDirector->IncrementTeamScore( score, NULL ); // S:25D960
}

// S:26EDA0.
void CInfoDirector::InputBeginScript( inputdata_t &inputdata )
{
	TheDirector->StopLocalScript();
	TheDirector->RunScript( inputdata.value.String(), 2, false );
}

// S:26AB90.
void CInfoDirector::InputEndScript( inputdata_t &inputdata )
{
	TheDirector->StopLocalScript();
}

// S:2665C0.
void CInfoDirector::InputOpenJournalForWrite( inputdata_t &inputdata )
{
	TheDirector->GetMapJournal()->OpenForWrite( inputdata.value.String() );
}

// S:2725B0.
void CInfoDirector::InputWriteToJournal( inputdata_t &inputdata )
{
	TheDirector->GetMapJournal()->Write( inputdata.value.String() );
}

// S:266600.
void CInfoDirector::InputExecuteJournalCategory( inputdata_t &inputdata )
{
	TheDirector->GetMapJournal()->Execute( inputdata.value.String() );
}

// S:25CBB0 / S:25CBC0.
void CInfoDirector::InputEnableTankFrustration( inputdata_t &inputdata )
{
	TheDirector->EnableTankFrustration();
}

void CInfoDirector::InputDisableTankFrustration( inputdata_t &inputdata )
{
	TheDirector->DisableTankFrustration();
}

// Native copies into fixed 256-byte buffers with an unchecked strcpy
// (S:4F72A0); longer text would overflow. The port truncates instead.
static void CopyJournalText( CDirectorMapJournal::JournalText_t &dest, const char *source )
{
	V_strncpy( dest.Access(), source ? source : "", 256 );
}

// S:2643C0. Every existing record in the category is removed. After a removal
// the index is reset to 0 and then incremented, so the record that moves into
// slot 0 is not rechecked: this is the native behaviour and is preserved.
void CDirectorMapJournal::OpenForWrite( const char *category )
{
	for ( int i = 0; i < m_records.Count(); ++i )
	{
		if ( !V_stricmp( category, m_records[i].m_category ) )
		{
			m_records.Remove( i );
			i = 0;
		}
	}
	CopyJournalText( m_currentCategory, category );
}

// S:271000. "target:input" appended under the current category. Native reads
// the second split piece without checking that it exists (undefined when the
// text has no ':'); the port uses an empty string for any missing piece.
void CDirectorMapJournal::Write( const char *text )
{
	CUtlVector< char * > parts;
	V_SplitString( text, ":", parts );
	Record_t record;
	CopyJournalText( record.m_category, m_currentCategory );
	CopyJournalText( record.m_target, parts.Count() > 0 ? parts[0] : "" );
	CopyJournalText( record.m_input, parts.Count() > 1 ? parts[1] : "" );
	m_records.AddToTail( record );           // S:267C50 insert at Count()
	for ( int i = 0; i < parts.Count(); ++i )
		delete[] parts[i];
	parts.Purge();
}

// S:264540. Each matching record fires its input on every entity with that
// target name, then is removed. Removing at index 0 is not followed by a
// recheck of the record that moves into slot 0 (native).
void CDirectorMapJournal::Execute( const char *category )
{
	Msg( "Executing Journal Category: %s\n", category );
	for ( int i = 0; i < m_records.Count(); ++i )
	{
		if ( V_stricmp( m_records[i].m_category, category ) )
			continue;
		for ( CBaseEntity *target = gEntList.FindEntityByName( NULL, m_records[i].m_target ); target;
			target = gEntList.FindEntityByName( target, m_records[i].m_target ) )
		{
			variant_t empty;
			target->AcceptInput( m_records[i].m_input, NULL, NULL, empty, 0 );
		}
		m_records.Remove( i );
		if ( i > 0 )
			--i;
	}
}
