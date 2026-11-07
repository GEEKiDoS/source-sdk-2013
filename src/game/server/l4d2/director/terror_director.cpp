//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_music_banks.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_survival_mode.h"
#include "world.h"
#include "vscript_server.h"
#include "vscript_shared.h"
#include "vscript_container_helpers.h"
#include "entitylist.h"
#include "filesystem.h"
#include "NextBot/NextBotManager.h"
#include "l4d2/director/infected_population.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/entities/info_remarkable.h"
#include "movevars_shared.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "tier0/icommandline.h"
#include <ctype.h>
#include "l4d2/terror_shareddefs.h"
#include <xmmintrin.h>
#include "l4d2/director/terror_director_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "vscript/ivscript.h"
#endif
#include "tier0/memdbgon.h"

CDirector *TheDirector = NULL;

extern ConVar rescue_interval;
extern ConVar z_mob_spawn_max_size;
extern ConVar director_transition_timeout;
extern ConVar director_min_start_players;
ConVar director_debug_threat_placement( "director_debug_threat_placement", "0", 0x00004004 );
namespace
{
	struct RemoveInfectedForRoundReset
	{
		bool operator()( INextBot *bot )
		{
			CBaseCombatCharacter *entity = bot->GetEntity();
			if ( entity->GetTeamNumber() != TEAM_INFECTED ) return true;
			if ( entity->IsPlayer() )
				engine->ServerCommand( CFmtStr( "kick \"%s\"\n", static_cast< CBasePlayer * >( entity )->GetPlayerName() ) );
			else UTIL_Remove( entity );
			return true;
		}
	};
}

BEGIN_SCRIPTDESC_ROOT( CDirector, "The director" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptResetMobTimer, "ResetMobTimer", "Trigger a mob as soon as possible when in BUILD_UP" )
	DEFINE_SCRIPTFUNC_NAMED( ResetSpecialTimers, "ResetSpecialTimers", "Reset all special timers (by type and by slot) so that hopefully mobs will spawn asap" )
	DEFINE_SCRIPTFUNC_NAMED( PlayMegaMobWarningSounds, "PlayMegaMobWarningSounds", "Plays a horde scream sound and asks survivors to speak 'incoming horde' lines" )
	DEFINE_SCRIPTFUNC_NAMED( GetAveragedSurvivorSpan, "GetAveragedSurvivorSpan", "Get the distance between the lead and trailing survivors, smoothed over time" )
	DEFINE_SCRIPTFUNC_NAMED( GetAveragedSurvivorSpeed, "GetAveragedSurvivorSpeed", "Get the rate at which the lead survivor is moving along the flow, smoothed over time" )
	DEFINE_SCRIPTFUNC_NAMED( GetFurthestSurvivorFlow, "GetFurthestSurvivorFlow", "Get the maximum distance along the flow that the survivors have reached" )
	DEFINE_SCRIPTFUNC_NAMED( HasAnySurvivorLeftSafeArea, "HasAnySurvivorLeftSafeArea", "True when one or more survivors have left the starting safe area" )
	DEFINE_SCRIPTFUNC_NAMED( GetGameMode, "GetGameMode", "Get the current game mode" )
	DEFINE_SCRIPTFUNC_NAMED( GetBaseGameMode, "GetGameModeBase", "Get the current base game mode" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetMapName, "GetMapName", "Get the current game mode" )
	DEFINE_SCRIPTFUNC_NAMED( GetSurvivorSet, "GetSurvivorSet", "1=L4D1 Survivors, 2=L4D2 Survivors" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsL4D1Campaign, "IsL4D1Campaign", "Return true is the current campaign is originally from L4D1" )
	DEFINE_SCRIPTFUNC_NAMED( IsOfflineSession, "IsSinglePlayerGame", "Return true if game is in single player" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsPlayingOnConsole, "IsPlayingOnConsole", "Return true if game is running on a console (such as Xbox 360)" )
	DEFINE_SCRIPTFUNC_NAMED( L4D1SurvivorGiveItem, "L4D1SurvivorGiveItem", "Let the L4D1 survivors know that now is a good time to give the players an item" )
	DEFINE_SCRIPTFUNC_NAMED( IsAnySurvivorInCombat, "IsAnySurvivorInCombat", "Returns true if any survivor recently dealt or took damage" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsTankInPlay, "IsTankInPlay", "Returns true if any tanks are aggro on survivors" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetHoldoutCooldownEndTime, "GetHoldoutCooldownEndTime", "Get the end time of the cooldown timer" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetHoldoutCooldownEndTime, "SetHoldoutCooldownEndTime", "Set the end time of the cooldown timer" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptForceNextStage, "ForceNextStage", "Push the ScriptedMode to next stage - i.e. request GNSS be called" )
	DEFINE_SCRIPTFUNC_NAMED( ClearCachedBotQueries, "ClearCachedBotQueries", "Mark all cached bot query results as invalid" )
	DEFINE_SCRIPTFUNC_NAMED( WarpAllSurvivorsToCheckpoint, "WarpAllSurvivorsToCheckpoint", "Warp the survivors to the next checkpoint" )
	DEFINE_SCRIPTFUNC_NAMED( WarpAllSurvivorsToBattlefield, "WarpAllSurvivorsToBattlefield", "Warp the survivors to the crescendo event" )
	DEFINE_SCRIPTFUNC_NAMED( WarpAllSurvivorsToFinale, "WarpAllSurvivorsToFinale", "Warp the survivors to the finale area" )
	DEFINE_SCRIPTFUNC_NAMED( AreAllSurvivorsInFinaleArea, "AreAllSurvivorsInFinaleArea", "True when all survivors are within the finale area" )
	DEFINE_SCRIPTFUNC_NAMED( AreTeamsFlipped, "AreTeamsFlipped", "True when teams are flipped in Versus modes" )
	DEFINE_SCRIPTFUNC_NAMED( GetChapterNumber, "GetMapNumber", "Return the current map number" )
	DEFINE_SCRIPTFUNC_NAMED( HasSurvivorsAdvancedBeyondTank, "IsAnySurvivorBypassingTank", "True when one or more survivors are bypassing the Tank" )
	DEFINE_SCRIPTFUNC_NAMED( IsTankFrustrationSuppressed, "IsAnySurvivorInExitCheckpoint", "True when one or more survivors are in the exit checkpoint" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsAnySurvivorInStartArea, "IsAnySurvivorInStartArea", "True when one or more survivors are in the starting safe area" )
	DEFINE_SCRIPTFUNC_NAMED( IsFinale, "IsFinale", "True when the finale has started" )
	DEFINE_SCRIPTFUNC_NAMED( IsFinaleVehicleLeaving, "IsFinaleEscapeInProgress", "True when the finale escape is in progress" )
	DEFINE_SCRIPTFUNC_NAMED( IsRescueVehicleReady, "IsFinaleVehicleReady", "True when the finale vehicle is ready" )
	DEFINE_SCRIPTFUNC_NAMED( ShouldResetTeamsOnMissionStart, "IsFirstMapInScenario", "True if the first map in a scenario" )
	DEFINE_SCRIPTFUNC_NAMED( IsPlayingIntro, "IsPlayingIntro", "True if the intro is currently playing" )
	DEFINE_SCRIPTFUNC_NAMED( IsFirstChapter, "IsSessionStartMap", "True if the map is the start of the session" )
	DEFINE_SCRIPTFUNC_NAMED( GetMissionDuration, "GetTotalElapsedMissionTime", "Returns the total elapsed mission time" )
	DEFINE_SCRIPTFUNC_NAMED( IsFinaleWon, "IsFinaleWon", "Returns true if the finale has been won" )
	DEFINE_SCRIPTFUNC_NAMED( GetMissionLossCount, "GetMissionWipes", "Returns the current number of mission wipes" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptAreAllSurvivorsInBattlefield, "AreAllSurvivorsInBattlefield", "Returns true if all Survivors are in the battlefield" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetClosestCharacterWhoIsIT, "GetClosestCharacterWhoIsIT", "Returns the closest character who is IT" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetClosestSurvivorOrigin, "GetClosestSurvivor", "Returns the closest Survivor to the passed origin" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetHighestFlowSurvivor, "GetHighestFlowSurvivor", "Returns the Survivor with the highest flow" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetLowestFlowSurvivor, "GetLowestFlowSurvivor", "Returns the Survivor with the lowest flow" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetRandomSurvivor, "GetRandomSurvivor", "Returns a random Survivor" )
	DEFINE_SCRIPTFUNC_NAMED( IsSpawnPositionInFog, "IsLocationFoggedToSurvivors", "Returns true if the passed location is fogged to Survivors" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetTimeSinceSpecialZombieKilled, "GetTimeSinceSpecialZombieKilled", "Returns the time since the passed zombie type was killed" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptRegisterForbiddenTarget, "RegisterForbiddenTarget", "Registers a target entity as forbidden" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptUnregisterForbiddenTarget, "UnregisterForbiddenTarget", "Unregisters a target entity as forbidden" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetClosestActivePipeBomb, "GetClosestActivePipeBomb", "Returns the closest active pipe bomb from this origin" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetPendingMobCount, "GetPendingMobCount", "Returns tnumber of infected waiting to spawn" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetCommonInfectedCount, "GetCommonInfectedCount", "Returns number fo infected currently spawned" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptUserDefinedEvent1, "UserDefinedEvent1", "generic user defined event fired from a squirrel script" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptUserDefinedEvent2, "UserDefinedEvent2", "generic user defined event fired from a squirrel script" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptUserDefinedEvent3, "UserDefinedEvent3", "generic user defined event fired from a squirrel script" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptUserDefinedEvent4, "UserDefinedEvent4", "generic user defined event fired from a squirrel script" )
END_SCRIPTDESC()

int CDirector::GetSurvivorSet() const { return TerrorGameRules()->GetSurvivorSet(); }
int CDirector::GetDifficulty() const { return TerrorGameRules()->GetDifficultyLevel(); }

#pragma float_control( precise, on, push )
int CDirectorScriptedMode::AllowBash( CBaseEntity *pusher, CBaseEntity *target )
{
	if ( !m_hAllowBash || m_hAllowBash == INVALID_HSCRIPT )
		return 0;
	HSCRIPT targetScript = target ? target->GetScriptInstance() : NULL;
	HSCRIPT pusherScript = pusher ? pusher->GetScriptInstance() : NULL;
	ScriptVariant_t result;
	ScriptStatus_t status = g_pScriptVM->Call( m_hAllowBash, TheDirector->GetScriptScope( 3 ), true,
		&result, pusherScript, targetScript );
	if ( status != SCRIPT_DONE )
	{
		DevWarning( "Failed to call AllowBash though we think we have one\n" );
		result.Free();
		return 0;
	}
	// S27A8B0: FIELD_INTEGER is copied as integer bits. EVERY other type's
	// same four payload bytes are CVTTSS2SI, not AssignTo(int), bool mapping,
	// nearest rounding or a validating type switch. Preserve native indefinite
	// INT_MIN for NaN/out-of-range, which remains neither disposition1 nor2.
	uint32 bits = result.GetRawPayload32();
	float raw;
	Q_memcpy( &raw, &bits, sizeof( raw ) );
	const int disposition = result.GetType() == FIELD_INTEGER ? static_cast< int32 >( bits ) :
		_mm_cvttss_si32( _mm_set_ss( raw ) );
	result.Free();
	return disposition;
}

bool CDirectorScriptedMode::AllowTakeDamage( CBaseEntity *victim, CTakeDamageInfo &info )
{
	if ( !m_hAllowTakeDamage || m_hAllowTakeDamage == INVALID_HSCRIPT )
		return true;

	// Native27A420 fills the existing MapScript table in this order. Only
	// DamageDone and DamageType are read back after the Challenge callback.
	ScriptVariant_t location( info.GetDamagePosition() );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "Inflictor", ToHScript( info.GetInflictor() ) );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "Attacker", ToHScript( info.GetAttacker() ) );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "Victim", ToHScript( victim ) );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "DamageDone", info.GetDamage() );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "DamageType", info.GetDamageType() );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "Location", location );
	g_pScriptVM->SetValue( m_hDamageInfoTable, "Weapon", ToHScript( info.GetWeapon() ) );
	ScriptVariant_t result;
	if ( g_pScriptVM->Call( m_hAllowTakeDamage, TheDirector->GetScriptScope( 3 ), true,
		&result, m_hDamageInfoTable ) != SCRIPT_DONE )
	{
		DevWarning( "Failed to call AllowTakeDamage though we think we have one!\n" );
	}
	else
	{
		if ( !( result.GetRawPayload32() & 0xFF ) )
		{
			result.Free();
			location.Free();
			return false;
		}
		ScriptVariant_t value;
		if ( g_pScriptVM->GetValue( m_hDamageInfoTable, "DamageDone", &value ) )
		{
			const uint32 bits = value.GetRawPayload32();
			float raw;
			Q_memcpy( &raw, &bits, sizeof( raw ) );
			info.SetDamage( value.GetType() == FIELD_FLOAT ? raw : (float)(int32)bits );
		}
		if ( g_pScriptVM->GetValue( m_hDamageInfoTable, "DamageType", &value ) )
		{
			const uint32 bits = value.GetRawPayload32();
			float raw;
			Q_memcpy( &raw, &bits, sizeof( raw ) );
			info.SetDamageType( value.GetType() == FIELD_INTEGER ? (int32)bits :
				_mm_cvttss_si32( _mm_set_ss( raw ) ) );
		}
		value.Free();
	}
	result.Free();
	location.Free();
	return true;
}

bool CDirectorScriptedMode::InterceptChat( CBaseEntity *speaker, const char *text )
{
	if ( !m_hInterceptChat || m_hInterceptChat == INVALID_HSCRIPT )
		return true;
	HSCRIPT instance = ToHScript( speaker );
	ScriptVariant_t result;
	if ( g_pScriptVM->Call( m_hInterceptChat, TheDirector->GetScriptScope( 3 ), true,
		&result, text, instance ) != SCRIPT_DONE )
		DevWarning( "Tried calling InterceptChat but failed\n" );
	const bool allow = result.GetType() != FIELD_BOOLEAN || ( result.GetRawPayload32() & 0xFF ) != 0;
	result.Free();
	return allow;
}
#pragma float_control( pop )
int CDirector::GetDirectorOptionInt( const char *name, int fallback ) const
{
	// S:262CD0. The options reference and VM are the only existence guards.
	if ( m_hDirectorOptions != INVALID_HSCRIPT && g_pScriptVM )
	{
		ScriptVariant_t value;
		if ( g_pScriptVM->GetValue( m_hDirectorOptions, name, &value ) && !value.AssignTo( &fallback ) )
			g_pScriptVM->ReleaseValue( value );
		value.Free();
	}
	return fallback;
}
float CDirector::GetDirectorOptionFloat( const char *name, float fallback ) const
{
	if ( m_hDirectorOptions != INVALID_HSCRIPT && g_pScriptVM )
	{
		ScriptVariant_t value;
		if ( g_pScriptVM->GetValue( m_hDirectorOptions, name, &value ) && !value.AssignTo( &fallback ) )
			g_pScriptVM->ReleaseValue( value );
		value.Free();
	}
	return fallback;
}

// S262E00. Non-CString pointer-bit values use Main's lookup-failure disposition.
const char *CDirector::GetDirectorOptionString( const char *name, const char *fallback,
	char *destination, size_t count ) const
{
	if ( m_hDirectorOptions != INVALID_HSCRIPT && g_pScriptVM )
	{
		ScriptVariant_t value;
		if ( g_pScriptVM->GetValue( m_hDirectorOptions, name, &value ) )
		{
			if ( value.GetType() == FIELD_CSTRING )
			{
				Q_strncpy( destination, (const char *)value, count );
				fallback = destination;
			}
			g_pScriptVM->ReleaseValue( value );
		}
		value.Free();
	}
	return fallback;
}

// S262D60/25D000. The argument retains the supplied fallback and receives the result.
void CDirector::GetDirectorOptionVector( const char *name, Vector *value ) const
{
	if ( m_hDirectorOptions == INVALID_HSCRIPT || !g_pScriptVM )
		return;
	ScriptVariant_t option;
	if ( g_pScriptVM->GetValue( m_hDirectorOptions, name, &option ) )
	{
		bool converted = false;
		switch ( option.GetType() )
		{
		case FIELD_VOID:
			*value = vec3_origin;
			break;
		case FIELD_VECTOR:
			*value = (const Vector &)option;
			converted = true;
			break;
		case FIELD_QANGLE:
		{
			const QAngle &angles = (const QAngle &)option;
			value->Init( angles.x, angles.y, angles.z );
			converted = true;
			break;
		}
		case FIELD_CSTRING:
			converted = sscanf( (const char *)option, "%f %f %f", &value->x, &value->y, &value->z ) == 3;
			if ( !converted )
				*value = vec3_origin;
			break;
		default:
			Warning( "No free conversion of %s variant to Vector right now\n", VariantFieldTypeName( option.GetType() ) );
			break;
		}
		if ( !converted )
			g_pScriptVM->ReleaseValue( option );
	}
	option.Free();
}

CDirector::CDirector() :
	m_nHumansNearCheckpoint( 0 ),
	m_nHumansAwayFromCheckpoint( 0 ),
	m_survivorSeparation(),
	m_playerAvatars( DefLessFunc( uint64 ) ),
	m_hScriptScope{ INVALID_HSCRIPT, INVALID_HSCRIPT, INVALID_HSCRIPT, INVALID_HSCRIPT },
	m_hDirectorOptions( INVALID_HSCRIPT ),
	m_bOfflineSession( false )
{
	// Native1576 stateless receiver is elided; its real queries retain their owners.
	m_pItemManager = new CDirectorItemManager;
	m_pMusicBanks = new DirectorMusicBanks;
	m_pSessionManager = new CDirectorSessionManager;
	m_pScenario = new CDirectorScenario;
	m_pVersusMode = new CDirectorVersusMode;
	m_pSurvivalMode = new CDirectorSurvivalMode;
	m_pScavengeMode = new CDirectorScavengeMode;
	m_pScriptedMode = new CDirectorScriptedMode;

	// S271A49 onward follows all eight child constructors.
	m_bEverHadHumanSurvivor = false;
	m_bEverHadHumanPlayer = false;
	m_bTransitionInProgress = false;
	m_bTransitionEntitiesRestored = false;
	m_bTransitionedToMap = false;
	m_bRestoreByCharacter = false;
	m_originalMapName[0] = '\0';
	m_flMissionElapsedTime = 0.0f;
	m_flSavedMobInterval = 0.0f;
	m_flSavedSpecialInterval = 0.0f;
	m_nSavedTankControllerUserID = 0;
	m_nMissionID = 0;
	m_nMissionLossCount = 0;
	m_nAllLeftStartAreaMissionID = -1;
	m_bSwappingTeams = false;
	m_bVersusVoteRestarting = false;
	m_nCurrentChapterIndex = 0;
	m_nCampaignStartChapterIndex = 0;
	m_bPlayingIntro = false;
	m_bSpeechDisabled = false;
	m_bTransitioningToNextMap = false;
	m_bMissionEndBlocked = false;
	m_bForceGauntletTank = false;
	m_nWitchCount = 0;
	m_bL4D1SurvivorItemRequest = false;

	const int preferredSurvivor = RandomInt( 0, 1 );
	m_bUpdatePreferredL4D1Survivor = false;
	m_nPreferredL4D1Survivor = ( preferredSurvivor != 1 ) + 5;
	m_hPendingTankOffer = NULL;
	m_flScriptUpdateInterval = 1.0f;
	m_bInSecondHalfOfRound = false;
	m_hScriptUpdate[0] = NULL;
	m_hScriptUpdate[1] = NULL;
	m_hScriptUpdate[2] = NULL;
	m_hScriptUpdate[3] = NULL;

	// Native PC engine+504 is constant false; +496 is the documented tool gap.
	if ( Engine_IsCreatingReslist() || CommandLine()->FindParm( "-bugbait" ) != 0 )
		director_min_start_players.SetValue( 0 );

	// S271B68..271B70 is the last constructor RNG call, not a -1 sentinel.
	m_nCampaignRandomNum = RandomInt( 0, 100 );
}
CDirector::~CDirector()
{
	ShutdownInfectedPopulation(); // Native2752D0: population data is the first release.
	ReleaseScriptHandles();
	delete m_pItemManager;
	delete m_pMusicBanks;
	delete m_pSessionManager;
	delete m_pScenario;
	delete m_pVersusMode;
	delete m_pSurvivalMode;
	delete m_pScavengeMode;
	// S:2752D0 does not delete the +1608 scripted context. Preserve that
	// original shutdown ownership rather than silently repairing the leak.
	FOR_EACH_MAP( m_playerAvatars, i ) m_playerAvatars[i]->deleteThis();
	TheDirector = NULL;
}

const CUtlVector< EHANDLE > &CDirector::GetCarAlarms() const
{
	return m_carAlarms;
}

void CDirector::RegisterCarAlarm( CBaseEntity *alarm )
{
	EHANDLE handle = alarm;
	if ( m_carAlarms.Find( handle ) < 0 )
	{
		handle = alarm; // Native26E120 resolves again before appending.
		m_carAlarms.AddToTail( handle );
	}
}

void CDirector::UnregisterCarAlarm( CBaseEntity *alarm )
{
	EHANDLE handle = alarm;
	if ( m_carAlarms.Find( handle ) >= 0 )
	{
		handle = alarm;
		int index = m_carAlarms.Find( handle );
		if ( index != -1 )
			m_carAlarms.FastRemove( index );
	}
}

void CDirector::ScriptRegisterForbiddenTarget( HSCRIPT target )
{
	if ( target )
	{
		CBaseEntity *entity = ToEnt( target );
		if ( entity )
			RegisterCarAlarm( entity );
	}
}

void CDirector::ScriptUnregisterForbiddenTarget( HSCRIPT target )
{
	if ( target )
	{
		CBaseEntity *entity = ToEnt( target );
		if ( entity )
			UnregisterCarAlarm( entity );
	}
}

HSCRIPT CDirector::ScriptGetHighestFlowSurvivor() const
{
	CTerrorPlayer *player = FindHighestFlowSurvivor( 0 ); // S25C020 ->28FD50(0).
	return player ? player->GetScriptInstance() : NULL;
}

HSCRIPT CDirector::ScriptGetLowestFlowSurvivor() const
{
	CTerrorPlayer *player = FindLowestFlowSurvivor( 4 ); // S25C040 ->28FD90.
	return player ? player->GetScriptInstance() : NULL;
}

HSCRIPT CDirector::ScriptGetClosestSurvivorOrigin( const Vector &position, bool includeImmobile, bool excludeRescueVehicle ) const
{
	CTerrorPlayer *player = FindClosestSurvivorPlayer( position, includeImmobile, excludeRescueVehicle );
	return player ? player->GetScriptInstance() : NULL;
}

HSCRIPT CDirector::ScriptGetRandomSurvivor() const
{
	CTerrorPlayer *player = PickLivingSurvivorForForceStart(); // Existing sole28F340.
	return player ? player->GetScriptInstance() : NULL;
}

int CDirector::ScriptGetCommonInfectedCount() const
{
	return ::GetCommonInfectedCount( 0 ); // S25BF60 ->2901B0(0).
}
void CDirector::OnClientDisconnected( CTerrorPlayer *player )
{
	m_pSessionManager->OnClientDisconnected( player );
	if ( player->IsAlive() && player->GetTeamNumber() == TEAM_INFECTED )
		OnInfectedKilled( player );

	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, -1, 7, 3, 9 );
	FOR_EACH_VEC( players, i )
		players[i]->OnPlayerDisconnected( player );
}


void CDirector::Reset()
{
	// S26BDC0: children and storage follow native order, not round-event aliases.
	m_pScriptedMode->Reset();
	m_pItemManager->Reset();
	m_pSessionManager->Reset();
	m_pScavengeMode->Reset();
	m_pScenario->Reset();
	m_pVersusMode->Reset();
	m_pSurvivalMode->Reset();
	m_bHasAnySurvivorLeftSafeArea = m_bHasAllSurvivorsLeftSafeArea = false;
	m_bGamePlaying = m_bEverHadHumanSurvivor = m_bEverHadHumanPlayer = false;
	m_bPlayingIntro = m_bSpeechDisabled = false;
	m_bDelayMissionEndScoreboard = m_bMissionEndBlocked = false;
	m_hLastGasCanPourer = NULL;
	m_nIntensityState = 0;
	m_IntensityStateTimer.Invalidate();
	m_nGameState = 0;
	m_navAreaChangeTimer.Invalidate();
	m_MissionStatusTimer.Invalidate();
	m_MissionEndTimer.Invalidate();
	m_ScenarioExitTimer.Invalidate();
	ResetThreatPriorities( 9 );
	m_nActiveCommonCount = 0;
	m_nHumanSurvivorCount = m_nTankCount = m_nWitchCount = 0;
	m_nSpecialCount = m_nHumanInfectedCount = 0;
	m_bTankFrustrationEnabled = true; // Native26BF84, same field used by real InfoDirector inputs.
	m_bAllowTankThreat = true;
	m_SpecialSpawnTimer.Invalidate();
	m_MobSpawnTimer.Invalidate();
	m_InitialSpawnDelay.Invalidate();
	m_flMobSize = GetDirectorOptionFloat( "MobMaxSize", z_mob_spawn_max_size.GetFloat() );
	m_flMobSizeIncrement = 0.0f;
	m_nTankOfferWinner = 0; // Native26BDC0 Reset; timers376/388, handle400 and pass count280 intentionally retained.
	m_SpecialZombieOfferPlayers.RemoveAll(); // Native26BDC0 actual1036 count0; retain capacity/timer1012/userID916.
	m_RescueUpdateTimer.Start( rescue_interval.GetFloat() );
	ResetInfectedPopulation();
	m_nHordeEscapeCommonLimit = -1;
	m_nSurvivorFlowIndex = 0;
	Q_memset( m_minSurvivorFlow, 0, sizeof( m_minSurvivorFlow ) );
	Q_memset( m_maxSurvivorFlow, 0, sizeof( m_maxSurvivorFlow ) );
	m_SurvivorMovementTimer.Invalidate();
	m_flSurvivorFlowSpan = m_flSurvivorMovement = m_flMaximumSurvivorFlow = 0.0f;
	for ( int i = 0; i < ARRAYSIZE( m_ClassSpawnIntervals ); ++i ) m_ClassSpawnIntervals[i].Invalidate();
	FOR_EACH_VEC( m_SpecialSpawnTimers, i ) m_SpecialSpawnTimers[i].Invalidate();
	ClearWitchList();
	if ( !IsBackgroundMap() )
	{
		RemoveInfectedForRoundReset remove;
		TheNextBots().ForEachBot( remove );
	}
	// Native retains base timers756, rescue/pipe collections and timers1128/1256,
	// saved intervals908/912, reason644, epochs and chapter origins.
	InitScripts();
	UpdateWorldMusic();
}

bool CDirector::CanChangeLevel() const
{
	// S25CD60 dispatches through the actual Director+1596 child.
	return m_pVersusMode->CanChangeLevel();
}
void CDirector::ResetThreatPriorities( int previousThreat )
{
	// Native virtual96=S25E000. It never removes an actor.
	if ( m_pVersusMode->IsActive() ) return;
	m_ThreatOrder[0] = 7; m_ThreatOrder[1] = 8; m_ThreatOrder[2] = 9;
	for ( int i = 2; i > 0; --i )
	{
		int other = RandomInt( 0, i );
		V_swap( m_ThreatOrder[i], m_ThreatOrder[other] );
	}
	if ( m_ThreatOrder[0] == 8 && previousThreat == 8 )
	{
		int other = RandomInt( 1, 2 );
		V_swap( m_ThreatOrder[0], m_ThreatOrder[other] );
	}
	m_nThreatOrderIndex = 0;
	if ( director_debug_threat_placement.GetBool() )
		for ( int i = 0; i < 3; ++i ) DevMsg( "Threat[%d] = %s\n", i, GetZombieClassName( m_ThreatOrder[i] ) );
}
void CDirector::OnRoundStart()
{
	if ( GetModeDescription()->IsBaseMode( "scavenge" ) && !m_bInSecondHalfOfRound )
		TerrorGameRules()->AdvanceScavengeRoundNumber();
	Reset();
	// Native2659A0 epoch follows virtual Reset; retained by Reset itself.
	if ( !m_bTransitionInProgress || g_bTerrorRestartAfterDeath )
	{
		m_nMissionID = (int)( (unsigned int)m_nMissionID + 1u );
		m_nMissionLossCount = 0;
	}
	struct RespawnRoundTeam
	{
		int team;
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
			if ( player->GetTeamNumber() != team ) return true;
			// Native25BAC0: original2056/2060 CS virtuals are distinct real
			// RoundRespawn/ObserverRoundRespawn APIs, NOT generic Spawn.
			switch ( player->GetTeamNumber() )
			{
			case TEAM_SURVIVOR: case TEAM_L4D1_SURVIVOR: player->RoundRespawn(); break;
			case TEAM_INFECTED: player->AttemptBecomeGhost( true ); break;
			default: player->ObserverRoundRespawn(); break;
			}
			return true;
		}
	};
	RespawnRoundTeam survivors = { TEAM_SURVIVOR }, infected = { TEAM_INFECTED };
	ForEachPlayer( survivors ); // Native25ED20.
	ForEachPlayer( infected ); // Native25EDB0, after ALL survivors.
	struct ResetScrimmage
	{
		bool operator()( CBasePlayer *base )
		{
			static_cast< CTerrorPlayer * >( base )->UpdateLineOfScrimmage( -1.0f, -1.0f );
			return true;
		}
	} scrimmage;
	ForEachPlayer( scrimmage ); // Native25EE40 actual connected-player admission.
	ResetRoundSpeech();
	const float initialRelax = RandomFloat( -30.0f, 30.0f );
	if ( initialRelax <= 0.0f ) { m_nIntensityState = 0; m_IntensityStateTimer.Invalidate(); }
	else { m_nIntensityState = 3; m_IntensityStateTimer.Start( initialRelax ); }
	m_nTankCount = m_nSpecialCount = m_nHumanInfectedCount = 0;
	sv_maxspeed.SetValue( 1000.0f ); // Actual existing movement ConVar, not a new cvar.
	for ( int i = 0; i < ARRAYSIZE( m_BaseSpecialSpawnTimers ); ++i ) m_BaseSpecialSpawnTimers[i].Invalidate();
	m_pMusicBanks->OnRoundStart( IsRestartingScenario() );
	// Native scripted child call here is PROVEN nullsub_22, no invented phase.
	m_pScenario->OnMapStart();
	m_pVersusMode->OnGameplayStart(); // Actual291220, NOT its2905C0 start-area callback.
	ResetTerrorIdleSpeechState(); // Actual2FD3C0 resets SAME producer globals.
	if ( !m_pSessionManager->IsTeamFrozen() ) m_pSessionManager->OnAllPlayersConnected();
}
bool CDirector::AreTeamsFlipped() const
{
	if ( GetModeDescription()->IsBaseMode( "versus" ) || !Q_stricmp( GetGameMode(), "mutation15" ) )
		return m_pVersusMode->AreTeamsFlipped();
	if ( GetModeDescription()->IsBaseMode( "scavenge" ) )
		return m_pScavengeMode->AreTeamsFlipped();
	return false;
}
void CDirector::SwapRoleProviders()
{
	// S:26D830 does BOTH before transferring actual player/bot ownership.
	m_pVersusMode->SwapRoles();
	m_pScavengeMode->SwapRoles();
}
bool CDirector::ShouldResetTeamsOnMissionStart() const
{
	KeyValues *first = TerrorGameRules()->GetFirstChapterInfo();
	if ( !first ) return false;
	const char *map = STRING( gpGlobals->mapname );
	if ( !map ) map = "";
	const char *firstMap = first->GetString( "map", "" );
	return map == firstMap || !Q_stricmp( map, firstMap );
}
int CDirector::GetCampaignMapType() const
{
	if ( m_nCurrentChapterIndex == 0 ) return 0;
	int count = TerrorGameRules()->GetMissionChapterCount();
	if ( m_nCurrentChapterIndex + 1 == count ) return 4;
	if ( count < 5 ) return m_nCurrentChapterIndex + 1;
	return m_nCurrentChapterIndex >= 4 ? 4 : m_nCurrentChapterIndex;
}
void CDirector::ReleaseScriptHandles()
{
	if ( g_pScriptVM )
	{
		ClearScriptGameEventListeners();
		// Native266640 gates the scope family on DirectorScript and releases
		// the options reference last, while this VM still owns all handles.
		if ( m_hScriptScope[0] != INVALID_HSCRIPT )
		{
			for ( int i = 0; i < 4; ++i )
			{
				if ( i == 3 ) m_pScriptedMode->ReleaseScriptFunctions();
				if ( m_hScriptScope[i] != INVALID_HSCRIPT ) g_pScriptVM->ReleaseScope( m_hScriptScope[i] );
				m_hScriptScope[i] = INVALID_HSCRIPT;
				if ( m_hScriptUpdate[i] ) g_pScriptVM->ReleaseFunction( m_hScriptUpdate[i] );
				m_hScriptUpdate[i] = NULL;
			}
			if ( m_hDirectorOptions != INVALID_HSCRIPT )
				g_pScriptVM->ReleaseScope( m_hDirectorOptions );
		}
	}
	m_hDirectorOptions = INVALID_HSCRIPT;
	for ( int i = 0; i < 4; ++i )
	{
		m_hScriptUpdate[i] = NULL;
		m_hScriptScope[i] = INVALID_HSCRIPT;
	}
}
void CDirector::ResetChallengeScope()
{
	// S:26ABA0. ChallengeScript is recreated under MapScript, not LocalScript.
	if ( g_pScriptVM )
	{
		m_pScriptedMode->ReleaseScriptFunctions();
		if ( m_hScriptScope[3] != INVALID_HSCRIPT )
			g_pScriptVM->ReleaseScope( m_hScriptScope[3] );
		if ( m_hScriptUpdate[3] )
			g_pScriptVM->ReleaseFunction( m_hScriptUpdate[3] );
		m_hScriptUpdate[3] = NULL;
		m_hScriptScope[3] = g_pScriptVM->CreateScope( "ChallengeScript", m_hScriptScope[1] );
	}
	else
	{
		m_hScriptScope[3] = INVALID_HSCRIPT;
		m_hScriptUpdate[3] = NULL;
	}
}
void CDirector::BindDirectorOptions()
{
	if ( m_hDirectorOptions != INVALID_HSCRIPT )
		g_pScriptVM->ReleaseScope( m_hDirectorOptions );
	m_hDirectorOptions = INVALID_HSCRIPT;
	HSCRIPT function = g_pScriptVM->LookupFunction( "GetDirectorOptions", m_hScriptScope[0] );
	if ( function )
	{
		ScriptVariant_t value;
		ScriptStatus_t status = g_pScriptVM->Call( function, m_hScriptScope[0], true, &value );
		g_pScriptVM->ReleaseFunction( function );
		if ( status == SCRIPT_DONE && value.GetType() == FIELD_HSCRIPT )
		{
			HSCRIPT scope = (HSCRIPT)value;
			if ( scope && scope != INVALID_HSCRIPT )
				m_hDirectorOptions = g_pScriptVM->ReferenceScope( scope );
		}
		else if ( status == SCRIPT_DONE )
			Warning( "No free conversion of %s variant to HSCRIPT right now\n", VariantFieldTypeName( value.GetType() ) );
		value.Free();
	}
	for ( int i = 0; i < 4; ++i )
	{
		if ( m_hScriptUpdate[i] ) g_pScriptVM->ReleaseFunction( m_hScriptUpdate[i] );
		m_hScriptUpdate[i] = g_pScriptVM->LookupFunction( "Update", m_hScriptScope[i], true );
		g_VScriptGameEventListener.CollectGameEventCallbacksInScope(
			m_hScriptScope[i] == INVALID_HSCRIPT ? NULL : m_hScriptScope[i] );
	}
}
// S:22E240 requires every absolute addon match in native search-path order.
// terror_provider_files.cpp re-expands real mounted absolute matches; the old
// effective-file-only draft has been removed.
extern void VScriptRunTerrorAddonScripts( const char *name, HSCRIPT scope, bool warnMissing );
void CDirector::RunScript( const char *scriptList, int level, bool includeAddonScripts )
{
	if ( m_hScriptScope[0] == INVALID_HSCRIPT )
		InitScripts();
	char list[1024];
	Q_strncpy( list, scriptList, sizeof( list ) );
	char *next = list;
	while ( *next )
	{
		while ( *next && ( ( *next >= 9 && *next <= 13 ) || *next == ' ' ) ) ++next;
		if ( !*next ) break;
		char *name = next;
		while ( *next && ( isalnum( (unsigned char)*next ) || *next == '_' ) ) ++next;
		if ( name == next )
		{
			DevWarning( "Bad character in script list (%s)\n", scriptList );
			BindDirectorOptions();
			return;
		}
		char delimiter = *next;
		*next = 0;
		VScriptRunScript( name, m_hScriptScope[level], true );
		if ( includeAddonScripts )
			VScriptRunTerrorAddonScripts( name, m_hScriptScope[level], false );
		if ( delimiter == '.' )
		{
			DevWarning( "File extension found in script list (%s)\nScript names should not contain extensions.\n", scriptList );
			delimiter = ' ';
		}
		*next = delimiter;
	}
	BindDirectorOptions();
}

static void RegisterDirectorConstants( HSCRIPT scope );
void CDirector::InitScripts()
{
	m_ScriptUpdateTimer.Invalidate();
	ReleaseScriptHandles();
	if ( !g_pScriptVM )
		return;
	static const char *names[] = { "DirectorScript", "MapScript", "LocalScript", "ChallengeScript" };
	m_hScriptScope[0] = g_pScriptVM->CreateScope( names[0] );
	m_hScriptScope[1] = g_pScriptVM->CreateScope( names[1], m_hScriptScope[0] );
	m_hScriptScope[2] = g_pScriptVM->CreateScope( names[2], m_hScriptScope[1] );
	RegisterDirectorConstants( m_hScriptScope[0] );
	m_pScriptedMode->Init();
	RunScript( "director_base", 0, true );
	for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
	{
		HSCRIPT entityScope = entity->GetScriptScope();
		if ( entityScope != NULL && entityScope != INVALID_HSCRIPT )
			g_VScriptGameEventListener.CollectGameEventCallbacksInScope( entityScope ); // Native26EF40 ->220D50.
	}
}
void CDirector::ShutdownScripts( int reason, const char *nextMap )
{
	if ( g_pScriptVM ) m_pScriptedMode->Shutdown( reason, nextMap );
}
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
void CDirector::UpdateScripts()
{
	// Native2748BD/2748CA treats an unordered deadline as unstarted.
	if ( !( m_ScriptUpdateTimer.m_timestamp > 0.0f ) )
		m_ScriptUpdateTimer.Start( m_flScriptUpdateInterval );
	else if ( gpGlobals->curtime >= m_ScriptUpdateTimer.m_timestamp )
	{
		for ( int i = 3; i >= 0; --i )
			if ( m_hScriptUpdate[i] ) g_pScriptVM->Call( m_hScriptUpdate[i], m_hScriptScope[i], true );
		m_ScriptUpdateTimer.Start( m_ScriptUpdateTimer.GetCountdownDuration() );
	}
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

CDirectorScriptedMode::CDirectorScriptedMode()
{
	m_hGetNextScriptedStage = NULL;
	m_hDebugTick = m_hAllowTakeDamage = m_hDamageInfoTable = m_hAllowBash = NULL;
	m_hInterceptChat = m_hUserConsoleCommand = NULL;
	m_hRescueTrigger = NULL;
	m_nBotQueryEpoch = 0;
	Q_memset( m_szScoreboardFilename, 0, sizeof( m_szScoreboardFilename ) );
	Reset();
	m_pSavedTables = NULL; // S279894 explicit constructor writer, after Reset()
}
void CDirectorScriptedMode::Reset()
{
	m_bEnabled = false;
	m_bActive = false;
	m_nSpawnSetRule = 1;
	m_nEndScriptReason = 0;
	m_szScoreboardFilename[0] = 0;
	m_bDebugMode = false;
	if ( g_pGameRules ) TerrorGameRules()->ResetScriptedHUD();
	m_Journal.Reset();
	m_HUD.Reset();
}
void CDirectorScriptedMode::UpdateForFrame()
{
	// S:287840: one call at the frame owner's native position.
	if ( !m_bActive ) return;
	m_HUD.Update();
	m_Journal.Update();
	if ( m_bDebugMode && m_hDebugTick )
		g_pScriptVM->Call( m_hDebugTick, TheDirector->GetScriptScope( 3 ), true, NULL );
	TheDirector->GetScenario()->UpdateFinale();
}
ScriptStatus_t CDirectorScriptedMode::GetNextScriptedStage()
{
	ScriptVariant_t value;
	ScriptStatus_t status = g_pScriptVM->Call( m_hGetNextScriptedStage, TheDirector->GetScriptScope( 3 ), true, &value );
	value.Free();
	return status;
}
void CDirectorScriptedMode::SetSpawnSetRuleFromOptions()
{
	m_nSpawnSetRule = TheDirector->GetDirectorOptionInt( "SpawnSetRule", 0 );
}
void CDirectorScriptedMode::ReleaseScriptFunctions()
{
	// S:276270 probes/releases Reset, then invokes it through a fresh lookup.
	HSCRIPT options = TheDirector->GetOptionsScope();
	if ( options != INVALID_HSCRIPT )
	{
		HSCRIPT function = g_pScriptVM->LookupFunction( "Reset", options );
		g_pScriptVM->ReleaseFunction( function );
		if ( function )
		{
			function = g_pScriptVM->LookupFunction( "Reset", options );
			if ( function )
			{
				g_pScriptVM->Call( function, options, true, NULL );
				g_pScriptVM->ReleaseFunction( function );
			}
		}
	}
	m_HUD.Release();
	if ( g_pScriptVM )
	{
		HSCRIPT *functions[] = { &m_hGetNextScriptedStage, &m_hDebugTick,
			&m_hAllowTakeDamage, &m_hAllowBash, &m_hInterceptChat, &m_hUserConsoleCommand };
		for ( int i = 0; i < ARRAYSIZE( functions ); ++i )
		{
			if ( *functions[i] && *functions[i] != INVALID_HSCRIPT )
			{
				g_pScriptVM->ReleaseFunction( *functions[i] );
				*functions[i] = INVALID_HSCRIPT;
			}
		}
		if ( m_hDamageInfoTable && m_hDamageInfoTable != INVALID_HSCRIPT )
		{
			g_pScriptVM->ReleaseScope( m_hDamageInfoTable );
			m_hDamageInfoTable = INVALID_HSCRIPT;
		}
	}
}
void CDirectorScriptedMode::Init()
{
	TheDirector->ResetChallengeScope();
	m_HUD.SetScope( 1 );
	m_nBotQueryEpoch = 1;
	m_Journal.SetScope( 3 );
	m_ScriptSpawnedEntities.RemoveAll(); // Native2798D9, count only; Reset retains.
	TheDirector->RunScript( "scriptedmode", 1, true );
	m_bEnabled = false;
	HSCRIPT scope = TheDirector->GetScriptScope( 1 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "ScriptMode_Init", scope );
	ScriptVariant_t value;
	if ( function )
	{
		if ( g_pScriptVM->Call( function, scope, true, &value,
			TheDirector->GetModeDescription()->GetName(), STRING( gpGlobals->mapname ) ) != SCRIPT_ERROR )
		{
			// S:2798A0 stores the returned variant's low byte, not AssignTo(bool).
			m_bEnabled = *reinterpret_cast< const unsigned char * >( &value ) != 0;
			gEntList.CleanupDeleteList();
			engine->AllowImmediateEdictReuse();
		}
		g_pScriptVM->ReleaseFunction( function );
	}
	TheDirector->BindDirectorOptions();
	value.Free();
}
void CDirectorScriptedMode::Activate()
{
	if ( !m_bEnabled ) return;
	HSCRIPT scope = TheDirector->GetScriptScope( 1 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "ScriptMode_OnActivate", scope );
	if ( function )
	{
		ScriptVariant_t value;
		ScriptStatus_t status = g_pScriptVM->Call( function, scope, true, &value, TheDirector->GetModeDescription()->GetName(), STRING( gpGlobals->mapname ) );
		g_pScriptVM->ReleaseFunction( function );
		if ( status != SCRIPT_ERROR )
			TheDirector->BindDirectorOptions();
		value.Free();
	}
	m_bActive = true;
	if ( g_pGameRules ) TerrorGameRules()->SetScriptedModeActive( true );
	m_hGetNextScriptedStage = g_pScriptVM->LookupFunction( "GetNextStage", TheDirector->GetScriptScope( 3 ) );
	m_hUserConsoleCommand = g_pScriptVM->LookupFunction( "UserConsoleCommand", TheDirector->GetScriptScope( 3 ) );
	m_hInterceptChat = g_pScriptVM->LookupFunction( "InterceptChat", TheDirector->GetScriptScope( 3 ) );
	m_hAllowTakeDamage = g_pScriptVM->LookupFunction( "AllowTakeDamage", TheDirector->GetScriptScope( 3 ) );
	m_hAllowBash = g_pScriptVM->LookupFunction( "AllowBash", TheDirector->GetScriptScope( 3 ) );
	if ( m_hAllowTakeDamage )
	{
		ScriptVariant_t table;
		m_hDamageInfoTable = NULL;
		if ( g_pScriptVM->GetValue( TheDirector->GetScriptScope( 1 ) == INVALID_HSCRIPT ? NULL : TheDirector->GetScriptScope( 1 ), "ScriptedDamageInfo", &table ) )
			Q_memcpy( &m_hDamageInfoTable, &table, sizeof( m_hDamageInfoTable ) );
		if ( !m_hDamageInfoTable || !VScriptIsTable( m_hDamageInfoTable ) ) m_hAllowTakeDamage = NULL;
		table.Free();
	}
	m_bDebugMode = !Q_strnicmp( TheDirector->GetModeDescription()->GetName(), "debug_", 6 );
	if ( m_bDebugMode )
	{
		DevMsg( "ScriptedMode: IsADebugMode is True!\n" );
		m_hDebugTick = g_pScriptVM->LookupFunction( "DebugTick", TheDirector->GetScriptScope( 3 ) );
	}
	TheDirector->GetScenario()->AdvanceStage();
}
void CDirectorScriptedMode::Shutdown( int reason, const char *nextMap )
{
	if ( !m_bActive ) return;
	m_bActive = false;
	HSCRIPT scope = TheDirector->GetScriptScope( 1 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "ScriptMode_OnShutdown", scope );
	if ( function )
	{
		ScriptVariant_t value;
		g_pScriptVM->Call( function, scope, true, &value, reason, nextMap );
		value.Free();
		g_pScriptVM->ReleaseFunction( function );
	}
	TheDirector->ResetChallengeScope();
	TheDirector->BindDirectorOptions();
	if ( g_pGameRules ) TerrorGameRules()->SetScriptedModeActive( false );
}
bool CDirectorScriptedMode::QueryOptionBool( const char *name, bool fallback ) const
{
	return m_bEnabled ? TheDirector->GetDirectorOptionInt( name, fallback ) != 0 : fallback;
}
bool CDirectorScriptedMode::CanPickupObject( CBaseEntity *object ) const
{
	if ( !m_bEnabled ) return false;
	HSCRIPT scope = TheDirector->GetScriptScope( 3 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "CanPickupObject", scope );
	g_pScriptVM->ReleaseFunction( function );
	if ( !function ) return false;
	// Native27A250 probes/releases, then performs a fresh scoped lookup.
	ScriptVariant_t value;
	scope = TheDirector->GetScriptScope( 3 );
	function = g_pScriptVM->LookupFunction( "CanPickupObject", scope );
	if ( function )
	{
		g_pScriptVM->Call( function, scope, true, &value, ToHScript( object ) );
		g_pScriptVM->ReleaseFunction( function );
	}
	const bool allow = ( value.GetRawPayload32() & 0xFF ) != 0;
	value.Free();
	return allow;
}
bool CDirectorScriptedMode::ShouldPlayBossMusic( bool *result ) const
{
	if ( !m_bEnabled ) return false;
	HSCRIPT scope = TheDirector->GetOptionsScope();
	HSCRIPT function = g_pScriptVM->LookupFunction( "ShouldPlayBossMusic", scope );
	g_pScriptVM->ReleaseFunction( function );
	if ( !function ) return false;
	ScriptVariant_t value;
	scope = TheDirector->GetOptionsScope();
	function = g_pScriptVM->LookupFunction( "ShouldPlayBossMusic", scope );
	if ( function )
	{
		g_pScriptVM->Call( function, scope, true, &value, TheDirector->GetScenario()->GetCustomStage() );
		g_pScriptVM->ReleaseFunction( function );
	}
	*result = ( value.GetRawPayload32() & 0xFF ) != 0; // Native27A0F0, not AssignTo(bool).
	value.Free();
	return true;
}

bool CDirector::IsBleedoutSuppressed() const { return m_pScenario->GetFinaleType() != 5 && m_nGameState == 3; }
bool CDirector::IsRestartingScenario() const { return m_bTransitionInProgress && !g_bTerrorRestartAfterDeath; }
bool CDirector::IsMissionLostOrWon() const { return m_MissionEndTimer.HasStarted(); }
bool CDirector::ShouldPlayBossMusic() const
{
	bool result = false;
	if ( m_pScriptedMode->ShouldPlayBossMusic( &result ) ) return result;
	return m_pScenario->GetFinaleType() == 5;
}
bool CDirector::AllowSurvivorRescue( bool fallback ) const { return m_pScriptedMode->QueryOptionBool( "cm_AllowSurvivorRescue", fallback ); }
int CDirector::GetInfectedFlags() const
{
	// Native25CAF0: only enabled scripted mode contributes these flags.
	if ( !m_pScriptedMode->IsEnabled() ) return 0;
	int flags = GetDirectorOptionInt( "InfectedFlags", 0 );
	if ( GetDirectorOptionInt( "cm_HeadshotOnly", 0 ) != 0 ) flags |= 0x800;
	return flags;
}
bool CDirector::AreWitchesAllowedInCheckpoints() const
{
	// Native25C830 ->275A90, not the scripted activation byte.
	return TerrorGameRules()->IsSurvivalMode() || TerrorGameRules()->IsScavengeMode() ||
		m_pScriptedMode->QueryOptionBool( "AllowWitchesInCheckpoints", false );
}
void CDirector::OnLevelShutdown()
{
	// Native25C5A0, actual PrepareForChangeLevel2B8140 virtual+12 tail.
	// No extra script shutdown, world clear, player save or NextBot reset.
	m_pSessionManager->OnLevelShutdown();
	m_pItemManager->OnLevelEnd();
}
void CDirector::OnNavAreaChanged( CTerrorPlayer *player, CNavArea *, CNavArea * )
{
	// Native262760: only an existing living player enters the shared 2s gate.
	if ( !player || !player->IsAlive() ) return;
	if ( (double)gpGlobals->curtime >= (double)m_navAreaChangeTimer.m_timestamp )
	{
		m_navAreaChangeTimer.Start( 2.0f );
		UpdateInfectedCulling(); // Existing native virtual108/26AB60 owner.
	}
	// The native manager tail is nullsub_30; new/old NAV areas have no effect.
}
void CDirector::OnLevelChangeComplete()
{
	// Native261DC0 complete ordered level boundary, not a grace-only facade.
	m_pSessionManager->OnRoundRestart();
	m_pItemManager->OnMapStart();
	TheTerrorNavMesh()->OnServerActivate(); // Native261DC0 NAV virtual17, not round virtual18.
	TheNextBots().OnMapLoaded(); // Native104ABB00 -> same real104AB7B0 Reset.
	TheZombieManager->OnMapStart(); // Native35C760, not round35A...
	m_bTransitionedToMap = g_bTerrorTransitionRestore;
	g_bTerrorTransitionRestore = false;
	m_bVersusVoteRestarting = false;
	ResetThreatPriorities( 9 );
	for ( int i = 0; i < ARRAYSIZE( m_ClassSpawnIntervals ); ++i ) m_ClassSpawnIntervals[i].Invalidate();
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	m_nCurrentChapterIndex = chapter ? chapter->GetInt( "chapter", 1 ) - 1 : 0;
	if ( !m_bTransitionedToMap )
	{
		const char *map = STRING( gpGlobals->mapname );
		Q_strncpy( m_originalMapName, map ? map : "", sizeof( m_originalMapName ) );
	}
	if ( !m_bTransitionedToMap || g_bTerrorRestartAfterDeath ) m_nCampaignStartChapterIndex = m_nCurrentChapterIndex;
	m_MissionTimer.Start();
	m_bTransitionEntitiesRestored = false;
	if ( !m_bTransitionedToMap && !g_bTerrorTransitionRestore )
	{
		m_flMissionElapsedTime = 0.0f;
		m_bTransitionInProgress = false;
	}
	else m_bTransitionInProgress = true;
	m_bInSecondHalfOfRound = false;
	m_bTransitioningToNextMap = false;
	TerrorGameRules()->RestartRound(); // Actual full native2E0650, not an inherited SDK/clock-only substitute.
	const bool clearCampaign = !m_bTransitionedToMap;
	if ( TerrorGameRules() )
	{
		TerrorGameRules()->ResetVersusRoundScores( clearCampaign );
		if ( TerrorGameRules() )
		{
			TerrorGameRules()->ResetScavengeScores( clearCampaign );
			TerrorGameRules()->ResetRoundDurations();
		}
	}
	m_pVersusMode->ResetCampaignScores( clearCampaign );
	m_HumanSurvivorGraceTimer.Start( director_transition_timeout.GetFloat() );
	m_pVersusMode->OnLevelChangeComplete();
	m_pSurvivalMode->OnLevelChangeComplete();
	m_pScavengeMode->OnMissionStart(); // Actual285670 writer, no duplicate callback alias.
	m_nSpecialClassRotationSeed = RandomInt( 1, 6 );
	if ( ( GetModeDescription()->IsBaseMode( "coop" ) || GetModeDescription()->IsBaseMode( "realism" ) )
		&& !m_bTransitionedToMap && !g_bTerrorTransitionRestore )
		m_nCampaignRandomNum = RandomInt( 0, 100 );
}
void CDirector::UpdateWorldMusic()
{
	// S:26BDC0 ->2DA010: the *mission* descriptor, not the current mode.
	KeyValues *mission = TerrorGameRules()->GetMissionInfo();
	if ( !mission ) return;
	const char *name = mission->GetString( "name", "" );
	const char *legacy = StringAfterPrefix( name, "L4D2C" );
	if ( !legacy ) return;
	int campaign = atoi( legacy );
	if ( campaign < 8 || campaign > 12 ) return;
	CWorld *world = GetWorldEntity();
	if ( !world ) return;
	world->SetStartMusicType( clamp( m_nCurrentChapterIndex, 0, 1 ) );
	const char *postfix = "NoMercy";
	switch ( campaign )
	{
	case 9: postfix = "Garage"; break;
	case 10: postfix = "SmallTown"; break;
	case 11: postfix = "Airport"; break;
	case 12: postfix = "Farm"; break;
	}
	world->SetMusicPostfix( postfix );
}
void CDirector::FireGameEvent( IGameEvent *event )
{
	m_pVersusMode->FireGameEvent( event );
	m_pSessionManager->FireGameEvent( event );
	m_pItemManager->FireGameEvent( event );
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_death" ) )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( player && player->IsPlayer() ) OnPlayerDeath( player );
	}
	else if ( !Q_stricmp( name, "player_disconnect" ) ) UpdatePlayerAvatarInfo( event->GetUint64( "xuid" ), NULL );
	else if ( !Q_stricmp( name, "player_first_spawn" ) )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( player && player->IsPlayer() && !player->IsBot() ) engine->ClientCommand( player->edict(), "cl_consistencycheck" );
	}
}

void CDirector::IncrementTeamScore( int score, CBaseEntity *scorer )
{
	// Native25D960: score is forwarded unchanged to the real rules/child.
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) && !m_pScavengeMode->HasStarted() ) return;
	if ( TerrorGameRules() ) TerrorGameRules()->IncrementScavengeTeamScore( score );
	CInfoDirector *director = dynamic_cast< CInfoDirector * >( gEntList.FindEntityByClassname( NULL, "info_director" ) );
	if ( director ) director->m_OnTeamScored.FireOutput( NULL, director, 0.0f );
	m_pScavengeMode->OnGasCanPoured( score ); // Sole2858D0 remaining-item decrement.
	m_hLastGasCanPourer = scorer; // Same actual932 EHANDLE; nonplayer/NULL is valid here.
}
void CDirector::SpawnScavengeItems()
{
	m_nScavengeItemsRemaining = m_pScavengeMode->SpawnScavengeItems();
	TerrorGameRules()->SetScavengeItemsRemaining( m_nScavengeItemsRemaining );
	if ( GetModeDescription()->IsBaseMode( "scavenge" ) )
		TerrorGameRules()->SetScavengeItemsGoal( m_nScavengeItemsRemaining );
}

#include "l4d2/director/terror_director_constants.inc"

#pragma float_control( precise, on, push )
float CDirector::GetResponseSurvivorDistance( int from, int to, bool mapCharacters ) const
{
	if ( mapCharacters )
	{
		from = MapTerrorSurvivorCharacter( from );
		to = MapTerrorSurvivorCharacter( to );
	}
	// S345947..A2A: unsigned bounds, and COMISS/JBE accepts unordered NaN.
	if ( static_cast< unsigned int >( from ) > 7 || static_cast< unsigned int >( to ) > 7 )
		return 1000000000.0f;
	float distance = m_survivorSeparation[from][to];
	return distance > 1000000000.0f ? 1000000000.0f : distance;
}
int CDirector::GetClosestResponseSurvivor( int character ) const
{
	// S25CA70 uses SUB/JS on FLOAT BITS, NOT floating comparison, not all8,
	// not a living-player scan. Ties choose the later slot. Preserve wrap.
	uint32 best = 0x7FFFFFFF;
	int selected = character;
	for ( int other = 0; other < 4; ++other )
	{
		if ( other == character ) continue;
		uint32 candidate;
		memcpy( &candidate, &m_survivorSeparation[character][other], sizeof( candidate ) );
		if ( ( ( best - candidate ) & 0x80000000U ) == 0 )
		{
			best = candidate;
			selected = other;
		}
	}
	return selected;
}
int CDirector::GetNumberInSafeSpot() const { return m_nHumansNearCheckpoint; }
int CDirector::GetNumberOutsideSafeSpot() const { return m_nHumansAwayFromCheckpoint; }
int CDirector::GetCampaignRandomNumber() const { return m_nCampaignRandomNum; }
const char *CDirector::GetIntroActorName() const { return GetSurvivorCharacterDisplayName( m_nPreferredL4D1Survivor ); }
const char *CDirector::GetScenarioName() const { return m_originalMapName; }

#pragma float_control( pop )

#include "l4d2/director/terror_director_console_server_callbacks.inc"
