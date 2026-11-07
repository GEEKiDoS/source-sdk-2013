//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Terror rules and ordinary cooperative spawn placement.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "tier1/utlstring.h"
#ifndef CLIENT_DLL
#include "vote_controller.h"
#include "l4d2/gamerules/cs_team.h"
#include "l4d2/gamerules/terror_team_size.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/stats/player_reporter.h"
#include "l4d2/entities/survivor_position.h"
#include "l4d2/player/terror_player_resource.h"
#include "l4d2/content/terror_model_precache.h"
#include "entitylist.h"
#include "nav_mesh.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/gamerules/terror_checkpoint_respawn.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/director/zombiemanager.h"
#include "tier1/netadr.h"
extern ConVar mp_roundlimit;
extern ConVar sv_gametypes;
#endif

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "KeyValues.h"
#endif
#include "tier0/memdbgon.h"


REGISTER_GAMERULES_CLASS( CTerrorGameRules );
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar survival_generate_fake_times( "survival_generate_fake_times", "1", 0x600a, "" );
#elif defined( L4D2 )
extern ConVar survival_generate_fake_times;
#else
static ConVar survival_generate_fake_times( "survival_generate_fake_times", "1", FCVAR_CHEAT | FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY );
#endif

#ifndef CLIENT_DLL
bool CTerrorGameRules::FShouldSwitchWeapon( CBasePlayer *player, CBaseCombatWeapon *weapon )
{
	// S2D9350: carry slot 5 switches before the CS autoswitch policy.
	if ( weapon->GetSlot() == 5 )
		return true;
	return BaseClass::FShouldSwitchWeapon( player, weapon );
}

bool CTerrorGameRules::IsInfiniteChainsawFuel() const { return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_InfiniteFuel", false ); }
bool CTerrorGameRules::IsVIPGnomeMutation() const { return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_VIPTarget", false ); }
bool CTerrorGameRules::IsHealingGnomeMutation() const { return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_HealingGnome", false ); }
#endif

// The original Terror table repeats these properties; it has no baseclass table.
BEGIN_NETWORK_TABLE_NOBASE( CTerrorGameRules, DT_TerrorGameRules )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bFreezePeriod ) ),
	RecvPropInt( RECVINFO( m_iRoundTime ) ),
	RecvPropFloat( RECVINFO( m_fLevelStartTime ) ),
	RecvPropFloat( RECVINFO( m_flGameStartTime ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iSurvivorScore ), RecvPropInt( RECVINFO( m_iSurvivorScore[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iCampaignScore ), RecvPropInt( RECVINFO( m_iCampaignScore[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iChapterScore ), RecvPropInt( RECVINFO( m_iChapterScore[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iChapterDamage ), RecvPropInt( RECVINFO( m_iChapterDamage[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iScavengeTeamScore ), RecvPropInt( RECVINFO( m_iScavengeTeamScore[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iScavengeMatchScore ), RecvPropInt( RECVINFO( m_iScavengeMatchScore[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_flTeamRoundTime ), RecvPropFloat( RECVINFO( m_flTeamRoundTime[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iVersusDistancePerSurvivor ), RecvPropInt( RECVINFO( m_iVersusDistancePerSurvivor[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iVersusSurvivorDeathDistance ), RecvPropInt( RECVINFO( m_iVersusSurvivorDeathDistance[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iVersusSurvivalMultiplier ), RecvPropInt( RECVINFO( m_iVersusSurvivalMultiplier[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iVersusDefibsUsed ), RecvPropInt( RECVINFO( m_iVersusDefibsUsed[0] ) ) ),
	RecvPropBool( RECVINFO( m_bInIntro ) ),
	RecvPropInt( RECVINFO( m_iServerRank ) ),
	RecvPropInt( RECVINFO( m_iServerPlayerCount ) ),
	RecvPropBool( RECVINFO( m_bIsDedicatedServer ) ),
	RecvPropInt( RECVINFO( m_iServerSteamGroupID ) ),
	RecvPropFloat( RECVINFO( m_flRoundStartTime ) ),
	RecvPropFloat( RECVINFO( m_flRoundEndTime ) ),
	RecvPropFloat( RECVINFO( m_flAccumulatedTime ) ),
	RecvPropInt( RECVINFO( m_nRoundNumber ) ),
	RecvPropInt( RECVINFO( m_nRoundLimit ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_iWinningTeamNumber ), RecvPropInt( RECVINFO( m_iWinningTeamNumber[0] ) ) ),
	RecvPropFloat( RECVINFO( m_flTeamBestRoundTime ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_flRoundDuration ), RecvPropFloat( RECVINFO( m_flRoundDuration[0] ) ) ),
	RecvPropInt( RECVINFO( m_nScavengeItemsRemaining ) ),
	RecvPropInt( RECVINFO( m_nScavengeItemsGoal ) ),
	RecvPropBool( RECVINFO( m_bAreTeamsFlipped ) ),
	RecvPropBool( RECVINFO( m_bInSecondHalfOfRound ) ),
	RecvPropBool( RECVINFO( m_bIsTransitioningToNextMap ) ),
	RecvPropBool( RECVINFO( m_bIsVersusVoteRestarting ) ),
	RecvPropBool( RECVINFO( m_bChallengeModeActive ) ),
	RecvPropBool( RECVINFO( m_bWaterSlowdownEnabled ) ),
	RecvPropInt( RECVINFO( m_iSacrificeEscapees ) ),
	RecvPropFloat(RECVINFO(m_flHoldoutCooldownEndTime)),
	RecvPropArray3(RECVINFO_ARRAY(m_iScriptedHUDInts),RecvPropInt(RECVINFO(m_iScriptedHUDInts[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDFloats),RecvPropFloat(RECVINFO(m_fScriptedHUDFloats[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_iScriptedHUDFlags),RecvPropInt(RECVINFO(m_iScriptedHUDFlags[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDTimerBase),RecvPropFloat(RECVINFO(m_fScriptedHUDTimerBase[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDTimerAdd),RecvPropFloat(RECVINFO(m_fScriptedHUDTimerAdd[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_iScriptedHUDTimerMode),RecvPropInt(RECVINFO(m_iScriptedHUDTimerMode[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDPosX),RecvPropFloat(RECVINFO(m_fScriptedHUDPosX[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDPosY),RecvPropFloat(RECVINFO(m_fScriptedHUDPosY[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDWidth),RecvPropFloat(RECVINFO(m_fScriptedHUDWidth[0]))),
	RecvPropArray3(RECVINFO_ARRAY(m_fScriptedHUDHeight),RecvPropFloat(RECVINFO(m_fScriptedHUDHeight[0]))),
	RecvPropArray(RecvPropString(RECVINFO(m_szScriptedHUDStringSet[0])),m_szScriptedHUDStringSet),
#else
	SendPropBool( SENDINFO( m_bFreezePeriod ) ),
	SendPropInt( SENDINFO( m_iRoundTime ), 16 ),
	SendPropFloat( SENDINFO( m_fLevelStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flGameStartTime ), 0, SPROP_NOSCALE ),
	SendPropArray3( SENDINFO_ARRAY3( m_iSurvivorScore ), SendPropInt( SENDINFO_ARRAY( m_iSurvivorScore ), 16, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iCampaignScore ), SendPropInt( SENDINFO_ARRAY( m_iCampaignScore ), 16, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iChapterScore ), SendPropInt( SENDINFO_ARRAY( m_iChapterScore ), 16, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iChapterDamage ), SendPropInt( SENDINFO_ARRAY( m_iChapterDamage ), 16, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iScavengeTeamScore ), SendPropInt( SENDINFO_ARRAY( m_iScavengeTeamScore ), 6, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iScavengeMatchScore ), SendPropInt( SENDINFO_ARRAY( m_iScavengeMatchScore ), 4, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_flTeamRoundTime ), SendPropFloat( SENDINFO_ARRAY( m_flTeamRoundTime ), 16, 0, 0.0f, 65536.0f ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iVersusDistancePerSurvivor ), SendPropInt( SENDINFO_ARRAY( m_iVersusDistancePerSurvivor ), 8, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iVersusSurvivorDeathDistance ), SendPropInt( SENDINFO_ARRAY( m_iVersusSurvivorDeathDistance ), 9, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iVersusSurvivalMultiplier ), SendPropInt( SENDINFO_ARRAY( m_iVersusSurvivalMultiplier ), 4, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_iVersusDefibsUsed ), SendPropInt( SENDINFO_ARRAY( m_iVersusDefibsUsed ), 4, SPROP_UNSIGNED ) ),
	SendPropBool( SENDINFO( m_bInIntro ) ),
	SendPropInt( SENDINFO( m_iServerRank ), 16, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iServerPlayerCount ), 16, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bIsDedicatedServer ) ),
	SendPropInt( SENDINFO( m_iServerSteamGroupID ), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flRoundStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flRoundEndTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flAccumulatedTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_nRoundNumber ), 32, 0 ),
	SendPropInt( SENDINFO( m_nRoundLimit ), 32, 0 ),
	SendPropArray3( SENDINFO_ARRAY3( m_iWinningTeamNumber ), SendPropInt( SENDINFO_ARRAY( m_iWinningTeamNumber ), 2, SPROP_UNSIGNED ) ),
	SendPropFloat( SENDINFO( m_flTeamBestRoundTime ), 0, SPROP_NOSCALE ),
	SendPropArray3( SENDINFO_ARRAY3( m_flRoundDuration ), SendPropFloat( SENDINFO_ARRAY( m_flRoundDuration ), 0, SPROP_NOSCALE ) ),
	SendPropInt( SENDINFO( m_nScavengeItemsRemaining ), 32, 0 ),
	SendPropInt( SENDINFO( m_nScavengeItemsGoal ), 32, 0 ),
	SendPropBool( SENDINFO( m_bAreTeamsFlipped ) ),
	SendPropBool( SENDINFO( m_bInSecondHalfOfRound ) ),
	SendPropBool( SENDINFO( m_bIsTransitioningToNextMap ) ),
	SendPropBool( SENDINFO( m_bIsVersusVoteRestarting ) ),
	SendPropBool( SENDINFO( m_bChallengeModeActive ) ),
	SendPropBool( SENDINFO( m_bWaterSlowdownEnabled ) ),
	SendPropInt( SENDINFO( m_iSacrificeEscapees ), 32, SPROP_UNSIGNED ),
	SendPropFloat(SENDINFO(m_flHoldoutCooldownEndTime),0,SPROP_NOSCALE),
	SendPropArray3(SENDINFO_ARRAY3(m_iScriptedHUDInts),SendPropInt(SENDINFO_ARRAY(m_iScriptedHUDInts),16,0)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDFloats),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDFloats),0,SPROP_NOSCALE)),
	SendPropArray3(SENDINFO_ARRAY3(m_iScriptedHUDFlags),SendPropInt(SENDINFO_ARRAY(m_iScriptedHUDFlags),16,SPROP_UNSIGNED)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDTimerBase),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDTimerBase),16,0,0.0f,65536.0f)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDTimerAdd),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDTimerAdd),16,0,0.0f,65536.0f)),
	SendPropArray3(SENDINFO_ARRAY3(m_iScriptedHUDTimerMode),SendPropInt(SENDINFO_ARRAY(m_iScriptedHUDTimerMode),4,0)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDPosX),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDPosX),0,SPROP_NOSCALE)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDPosY),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDPosY),0,SPROP_NOSCALE)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDWidth),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDWidth),0,SPROP_NOSCALE)),
	SendPropArray3(SENDINFO_ARRAY3(m_fScriptedHUDHeight),SendPropFloat(SENDINFO_ARRAY(m_fScriptedHUDHeight),0,SPROP_NOSCALE)),
	SendPropArray(SendPropString(SENDINFO_STRING_NOCHECK(m_szScriptedHUDStringSet),sizeof(((currentSendDTClass *)0)->m_szScriptedHUDStringSet[0])),m_szScriptedHUDStringSet),
#endif
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS( terror_gamerules, CTerrorGameRulesProxy );
IMPLEMENT_NETWORKCLASS_ALIASED( TerrorGameRulesProxy, DT_TerrorGameRulesProxy )

#ifdef CLIENT_DLL
static void RecvProxy_TerrorGameRules( const RecvProp *pProp, void **pOut, void *pData, int objectID )
{
	*pOut = TerrorGameRules();
	Assert( *pOut );
}

BEGIN_RECV_TABLE( CTerrorGameRulesProxy, DT_TerrorGameRulesProxy )
	RecvPropDataTable( "terror_gamerules_data", 0, 0, &REFERENCE_RECV_TABLE( DT_TerrorGameRules ), RecvProxy_TerrorGameRules ),
END_RECV_TABLE()
#else
static void *SendProxy_TerrorGameRules( const SendProp *pProp, const void *pStructBase, const void *pData, CSendProxyRecipients *pRecipients, int objectID )
{
	pRecipients->SetAllRecipients();
	return TerrorGameRules();
}

BEGIN_SEND_TABLE( CTerrorGameRulesProxy, DT_TerrorGameRulesProxy )
	SendPropDataTable( "terror_gamerules_data", 0, &REFERENCE_SEND_TABLE( DT_TerrorGameRules ), SendProxy_TerrorGameRules ),
END_SEND_TABLE()

ConVar survivor_limit( "survivor_limit", "4", 0x00000106, "Max # of survivors", true, 1.0f, true, 4.0f, OnSurvivorLimitChanged );
#endif
// S59E270/C513DE0: native release+replicated flags; only the server owns the callback.
ConVar z_difficulty( "z_difficulty", "Normal", 
#if defined( CLIENT_DLL )
	0x00082008
#elif defined( GAME_DLL )
	0x00082004
#else
	FCVAR_REPLICATED | 0x00080000
#endif
,
	"Difficulty of the current game (Easy, Normal, Hard, Impossible)", false, 0.0f, false, 0.0f
#ifndef CLIENT_DLL
	, TerrorDifficultyChanged
#endif
	);

// S2DA240/250/260 and C27B6B0/C0/D0 reinterpret the shared damage bits.
int CTerrorGameRules::Damage_GetTimeBased()
{
	return BaseClass::Damage_GetTimeBased() & ~DMG_MELEE;
}

int CTerrorGameRules::Damage_GetShowOnHud()
{
	return BaseClass::Damage_GetShowOnHud() & ~DMG_MELEE;
}

int CTerrorGameRules::Damage_GetNoPhysicsForce()
{
	return BaseClass::Damage_GetNoPhysicsForce() & 0xF85B7FFFu;
}

// Native inherited queries dispatch through these virtual mask getters;
// the SDK's query implementations instead hardcode their original masks.
bool CTerrorGameRules::Damage_IsTimeBased( int damageType )
{
	return ( damageType & Damage_GetTimeBased() ) != 0;
}

bool CTerrorGameRules::Damage_ShowOnHUD( int damageType )
{
	return ( damageType & Damage_GetShowOnHud() ) != 0;
}

bool CTerrorGameRules::Damage_NoPhysicsForce( int damageType )
{
	return ( damageType & Damage_GetNoPhysicsForce() ) != 0;
}

CTerrorGameRules::CTerrorGameRules()
#ifndef CLIENT_DLL
	: m_GSGameplayStatsCallback( this, &CTerrorGameRules::OnGSGameplayStats )
#endif
{
#ifndef CLIENT_DLL
	// S2E1DC5..2E1DFD: actual inherited restart and same live physics state.
	m_flRestartRoundTime = 0.0f;
	m_hSurvivalExploitEntity = NULL;
	m_flSurvivalExploitAccumulator = 0.0f;
	m_flPreviousSurvivalExploitAccumulator = 0.0f;
	m_flSurvivalExploitDecayTime = 0.0f;
	m_flSurvivalExploitDamageTime = 0.0f;
	m_flSurvivalExploitSpitTime = 0.0f;
	m_nSurvivalExploitSpitCount = 0;
	ResetSurvivorDamageByClass(); // S2E1E03, exact alternating live arrays.
	ResetVersusRoundScores( true ); // S2E1E0A, not a blanket constructor reset.
	m_flRoundStartTime = 0.0f;
	m_flRoundEndTime = 0.0f;
	m_flAccumulatedTime = 0.0f;
	m_flTeamBestRoundTime = 0.0f;
	m_nScavengeItemsRemaining = 0;
	m_nScavengeItemsGoal = 0;
	m_nRoundNumber = 0;
	m_nRoundLimit = mp_roundlimit.GetInt();
	m_iServerRank = 0;
	m_iServerPlayerCount = 0;
	m_iServerSteamGroupID = 0;
	m_bAreTeamsFlipped = false;
	m_bInSecondHalfOfRound = false;
	m_bIsTransitioningToNextMap = false;
	m_bIsDedicatedServer = engine->IsDedicatedServer();
	// Native compares the two duration FLOAT representations as dwords.
	for ( int index = 0; index < 2; ++index )
	{
		if ( FloatBits( m_flRoundDuration.Get( index ) ) != 0u )
			m_flRoundDuration.GetForModify( index ) = 0.0f;
	}
	// S2E1FCD..2E223E: GetString's NEVER_AS_STRING case stays nonempty.
	// Native 501680 equality is byte-exact, not case-folding.
	if ( sv_gametypes.GetString()[0] == '\0' )
	{
		CUtlVector< CUtlString > baseModes;
		KeyValues *dictionary = g_TerrorModes.GetModeDictionary();
		for ( KeyValues *mode = dictionary ? dictionary->GetFirstTrueSubKey() : NULL;
			mode; mode = mode->GetNextTrueSubKey() )
		{
			const char *base = mode->GetString( "base", "" );
			if ( !base || !base[0] )
				continue;
			bool found = false;
			FOR_EACH_VEC( baseModes, index )
			{
				if ( V_strcmp( baseModes[index].Get(), base ) == 0 )
				{
					found = true;
					break;
				}
			}
			if ( !found )
				baseModes[baseModes.AddToTail()] = base;
		}
		if ( baseModes.Count() )
		{
			CUtlString text;
			FOR_EACH_VEC( baseModes, index )
			{
				if ( text.Length() )
					text += ",";
				text += baseModes[index];
			}
			sv_gametypes.SetValue( text.Get() );
		}
	}
	m_bChallengeModeActive = false;
	int day, month, year;
	GetCurrentDate( &day, &month, &year );
	m_bHolidayGiftSeason = ( month == 12 && day >= 1 ) || ( month == 1 && day <= 2 );
#endif
	// Server S2E22A3 / client C27DAA8: the actual final paired constructor stores.
	m_bWaterSlowdownEnabled = true;
	m_nVersusMaxCompletionScore = 400;
}

#ifndef CLIENT_DLL
void CTerrorGameRules::OnGSGameplayStats( GSGameplayStats_t *stats )
{
	if ( stats->m_eResult == k_EResultOK )
	{
		m_iServerRank = static_cast< int >( static_cast< unsigned int >( stats->m_nRank ) + 1u );
		m_iServerPlayerCount = static_cast< int >( stats->m_unTotalConnects );
	}
	else
	{
		m_iServerRank = 0;
		m_iServerPlayerCount = 0;
	}
}
#endif

const CViewVectors *CTerrorGameRules::GetViewVectors() const
{
	static const CViewVectors s_ViewVectors(
		TERROR_VEC_VIEW,
		TERROR_VEC_HULL_MIN,
		TERROR_VEC_HULL_MAX,
		TERROR_VEC_DUCK_HULL_MIN,
		TERROR_VEC_DUCK_HULL_MAX,
		TERROR_VEC_DUCK_VIEW,
		TERROR_VEC_OBS_HULL_MIN,
		TERROR_VEC_OBS_HULL_MAX,
		TERROR_VEC_DEAD_VIEWHEIGHT );
	return &s_ViewVectors;
}

#ifndef CLIENT_DLL
void CTerrorGameRules::LevelInitPreEntity()
{
	BaseClass::LevelInitPreEntity();
	ResetSurvivorSetCache();
}
void CTerrorGameRules::LevelInitPostEntity()
{
	BaseClass::LevelInitPostEntity();
	m_iServerSteamGroupID = 0;
	ConVarRef steamGroup( "sv_steamgroup" );
	if ( steamGroup.IsValid() ) m_iServerSteamGroupID = steamGroup.GetInt();
}

const char *CTerrorGameRules::GetGameDescription()
{
	return "Left 4 Dead 2";
}

void CTerrorGameRules::CreateStandardEntities()
{
	g_pPlayerResource = static_cast<CPlayerResource *>( CBaseEntity::Create( "terror_player_manager", vec3_origin, vec3_angle ) );
	CBaseEntity::Create( "vote_controller", vec3_origin, vec3_angle ); // S2D94D0: Spawn installs the actual global before issue construction.
	TerrorCreateVoteIssues(); // The seven native issues register with that controller in their original order.

	if ( gpGlobals->eLoadType == MapLoad_NewGame ) // S2D9630 admits only native load type 0.
	{
		CBaseEntity *pOldProxy = gEntList.FindEntityByClassname( NULL, "terror_gamerules" );
		if ( pOldProxy )
		{
			UTIL_Remove( pOldProxy );
		}
		CBaseEntity::Create( "terror_gamerules", vec3_origin, vec3_angle );
	}
}

int CTerrorGameRules::GetHumanPlayerLimit() const
{
	// S2DB060..2DB0B2: only positive mounted metadata enables the eight-player
	// PZ limit. The shared native hard-cap caller2D9390 queries this once.
	return g_TerrorModes.IsPlayerControlledZombies() ? 8 : 4;
}

bool CTerrorGameRules::ClientConnected( edict_t *pEntity, const char *pszName, const char *pszAddress, char *pRejectReason, int nMaxRejectReason )
{
	// Native E1392F0 rejects remote connections to an offline MatchSession
	// with this reason. E2905E0/E290600 exempt 127.0.0.1 and engine loopback;
	// E2904F0 renders the latter as "loopback". Keep the same local exemption.
	if ( TheDirector->IsOfflineSession() && Q_strcmp( pszAddress, "loopback" ) )
	{
		netadr_t address;
		address.SetFromString( pszAddress );
		if ( !address.IsLocalhost() )
		{
			Q_strncpy( pRejectReason, "#Valve_Reject_Single_Player", nMaxRejectReason );
			return false;
		}
	}

	int nHumans = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer && !pPlayer->IsBot() )
		{
			++nHumans;
		}
	}

	const int nLimit = GetHumanPlayerLimit();
	if ( nHumans >= nLimit )
	{
		Q_snprintf( pRejectReason, nMaxRejectReason, "Human player limit reached (%d/%d)", nHumans, nLimit );
		return false;
	}
	return BaseClass::ClientConnected( pEntity, pszName, pszAddress, pRejectReason, nMaxRejectReason );
}

void CTerrorGameRules::SetInIntro( bool bInIntro )
{
	m_bInIntro = bInIntro;
}

void CTerrorGameRules::ClientDisconnected( edict_t *pClient )
{
	// S2DB0C0 preserves this raw entity across CS teardown and repeats IsPlayer.
	CBaseEntity *entity = CBaseEntity::Instance( pClient );
	TheDirector->OnClientDisconnected( ToTerrorPlayer( entity ) );
	BaseClass::ClientDisconnected( pClient );
	GetPlayerReporter().RemovePlayerReport( ToTerrorPlayer( entity ) );
}


namespace
{
bool HasOutgoingNonCheckpointArea( TerrorNavArea *area )
{
	for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
	{
		const NavConnectVector *connections = area->GetAdjacentAreas( static_cast< NavDirType >( direction ) );
		FOR_EACH_VEC( ( *connections ), i )
		{
			TerrorNavArea *other = ToTerrorNavArea( ( *connections )[i].area );
			if ( other && !other->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return true;
		}
	}
	return false;
}

// S2DB3C0: the second bucket also accepts candidates no closer than visible A.
class CSpawnFacingPlayerScan
{
public:
	CSpawnFacingPlayerScan( CBasePlayer *ignore, int team, const Vector &position ) :
		m_ignore( ignore ), m_team( team ), m_position( position ),
		m_visible( NULL ), m_visibleRange( 9999999.0f ),
		m_other( NULL ), m_otherRange( 9999999.0f )
	{
	}

	bool operator()( CBasePlayer *player )
	{
		if ( player == m_ignore || !player->IsAlive() ||
			( m_team != TEAM_ANY && player->GetTeamNumber() != m_team ) )
			return true;

		Vector delta = player->GetAbsOrigin() - m_position;
		const double rangeWide = sqrt( ( (double)delta.x * delta.x +
			(double)delta.y * delta.y ) + (double)delta.z * delta.z );
		const float range = (float)rangeWide; // S2DB45F FST, not FSTP.
		if ( rangeWide > (double)m_otherRange )
			return true;

		if ( m_visibleRange > range )
		{
			CTraceFilterNoNPCsOrPlayer filter( m_ignore, COLLISION_GROUP_NONE );
			trace_t trace;
			Vector eye = TerrorGameRules()->GetViewVectors()->m_vView + m_position;
			UTIL_TraceLine( eye, player->EyePosition(), 0x02006041, &filter, &trace );
			bool visible = !trace.DidHit();
			if ( !visible )
			{
				eye = TerrorGameRules()->GetViewVectors()->m_vView + m_position;
				UTIL_TraceLine( eye, player->WorldSpaceCenter(), 0x02006041, &filter, &trace );
				visible = !trace.DidHit();
				if ( !visible )
				{
					eye = TerrorGameRules()->GetViewVectors()->m_vView + m_position;
					UTIL_TraceLine( eye, player->GetAbsOrigin(), 0x02006041, &filter, &trace );
					visible = !trace.DidHit();
				}
			}
			if ( visible )
			{
				if ( m_visibleRange > range )
				{
					m_visible = player;
					m_visibleRange = range;
				}
				return true;
			}
		}
		m_otherRange = range;
		m_other = player;
		return true;
	}

	CBasePlayer *GetPlayer() const { return m_visible ? m_visible : m_other; }

private:
	CBasePlayer *m_ignore;
	int m_team;
	Vector m_position;
	CBasePlayer *m_visible;
	float m_visibleRange;
	CBasePlayer *m_other;
	float m_otherRange;
};
}

bool CTerrorGameRules::GetPlayerSpawnPosition( int nCharacter, Vector *pOrigin, QAngle *pAngles, TerrorNavArea **pArea )
{
	CUtlVector< CSurvivorPosition * > modePositions;
	CUtlVector< CSurvivorPosition * > defaultPositions;
	bool checkpointSpawn = !IsSurvivalMode();
	int survivorSet = GetSurvivorSet();
	const char *baseMode = TheDirector->GetBaseGameMode();
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_position" ) ) != NULL )
	{
		CSurvivorPosition *position = static_cast< CSurvivorPosition * >( entity );
		const char *positionName = position->GetSurvivorName();
		SurvivorCharacterType character = positionName ?
			static_cast< SurvivorCharacterType >( GetTerrorSurvivorCharacterForName( positionName ) ) : SurvivorCharacter_Unknown;
		if ( survivorSet == 2 && character >= SurvivorCharacter_NamVet && character < SurvivorCharacter_Unknown ) continue;
		if ( survivorSet == 1 && character < SurvivorCharacter_NamVet ) continue;
		const char *mode = position->GetGameMode();
		if ( mode )
		{
			if ( mode == baseMode || !Q_stricmp( mode, baseMode ) ) modePositions.AddToTail( position );
		}
		else
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( position, 0, 10000.0f ) );
			bool inCheckpoint = area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT );
			if ( inCheckpoint == checkpointSpawn ) defaultPositions.AddToTail( position );
		}
	}
	CUtlVector< CSurvivorPosition * > &positions = modePositions.Count() ? modePositions : defaultPositions;
	CSurvivorPosition *selected = NULL;
	FOR_EACH_VEC( positions, i )
	{
		const char *positionName = positions[i]->GetSurvivorName();
		SurvivorCharacterType character = positionName ?
			static_cast< SurvivorCharacterType >( GetTerrorSurvivorCharacterForName( positionName ) ) : SurvivorCharacter_Unknown;
		if ( character != SurvivorCharacter_Unknown && character == nCharacter ) { selected = positions[i]; break; }
	}
	if ( !selected && positions.Count() )
	{
		static int s_position = -1; // Native process-lifetime7AB2B8, never round/map reset.
		if ( ++s_position >= positions.Count() ) s_position = 0;
		selected = positions[s_position];
	}
	if ( selected )
	{
		if ( pOrigin ) *pOrigin = selected->GetAbsOrigin();
		if ( pAngles ) *pAngles = selected->GetAbsAngles();
		if ( pArea ) *pArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( selected, GETNAVAREA_CHECK_GROUND, 10000.0f ) );
		return true;
	}
	if ( IsSurvivalMode() )
	{
		FOR_EACH_VEC( TheNavAreas, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
			if ( !area->HasSpawnAttributes( TERROR_NAV_BATTLESTATION ) ) continue;
			Vector origin = area->GetCenter() + Vector( 0, 0, 18 );
			if ( pOrigin ) *pOrigin = origin;
			if ( pAngles ) { *pAngles = vec3_angle; OrientTerrorSpawnToNearbyItem( origin, pAngles ); }
			if ( pArea ) *pArea = area;
			return true;
		}
	}
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) )
		{
			Vector origin = area->GetCenter() + Vector( 0, 0, 18 );
			if ( pOrigin ) *pOrigin = origin;
			if ( pAngles ) { *pAngles = vec3_angle; OrientTerrorSpawnToNearbyItem( origin, pAngles ); }
			if ( pArea ) *pArea = area;
			return true;
		}
		if ( !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) || HasOutgoingNonCheckpointArea( area ) ) continue;
		Vector origin = area->GetCenter() + Vector( 0, 0, 35.5f );
		CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetCheckpoint( origin );
		if ( !checkpoint || checkpoint->IsOutward() ) continue;
		QAngle angles;
		if ( !checkpoint->GetSpawnPosition( &origin, &angles, &area ) ) continue;
		if ( pOrigin ) *pOrigin = origin;
		if ( pAngles ) *pAngles = angles;
		if ( pArea ) *pArea = area;
		return true;
	}
	entity = gEntList.FindEntityByClassname( NULL, "info_player_start" );
	if ( entity )
	{
		Vector origin = entity->GetAbsOrigin();
		if ( pOrigin ) *pOrigin = origin;
		if ( pAngles ) { *pAngles = vec3_angle; OrientTerrorSpawnToNearbyItem( origin, pAngles ); }
		if ( pArea ) *pArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( origin, false, 10000.0f, false, true, TEAM_ANY ) );
		return true;
	}
	// S2BE49D: Main retained this original first-area quirk, not a flow fallback.
	if ( TheNavAreas.Count() )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[0] );
		Vector origin = area->GetCenter() + Vector( 0, 0, 35.5f );
		if ( pOrigin ) *pOrigin = origin;
		if ( pAngles ) { *pAngles = vec3_angle; OrientTerrorSpawnToNearbyItem( origin, pAngles ); }
		if ( pArea ) *pArea = area;
		return true;
	}
	Warning( "GetPlayerSpawnPosition: No spawn zones\n" );
	return false;
}

CBaseEntity *CTerrorGameRules::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	QAngle angles = vec3_angle;
	Vector origin = vec3_origin;
	if ( pPlayer->GetTeamNumber() == TEAM_INFECTED )
	{
		Assert( TheZombieManager ); // Native35C700 startup invariant; no release fallback.
		if ( TheZombieManager->IsUsingSpecialSpawnPosition() )
			origin = TheZombieManager->GetSpecialSpawnPosition();
	}
	else
	{
		CTerrorCheckpoint *checkpoint = GetTerrorCheckpointSpawnOverride();
		if ( !checkpoint )
			checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
		CSurvivorRescue *rescue = GetTerrorRescueSpawnOverride();
		if ( rescue )
		{
			origin = rescue->GetAbsOrigin();
			origin.z += 1.0f;
			angles = GetTerrorRescueSpawnOverride()->GetAbsAngles();
		}
		else if ( checkpoint )
			checkpoint->GetSpawnPosition( &origin, &angles, NULL );
		else
			GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &origin, &angles, NULL );
	}
	const Vector &hullMax = GetViewVectors()->m_vHullMax;
	const Vector &hullMin = GetViewVectors()->m_vHullMin;
	Vector position = DropSpawnToGround( pPlayer, origin, hullMin, hullMax );
	if ( pPlayer->GetTeamNumber() == TEAM_INFECTED && !pPlayer->IsBot() )
	{
		CSpawnFacingPlayerScan scan( pPlayer, TEAM_SURVIVOR, position );
		ForEachPlayer( scan );
		CBasePlayer *target = scan.GetPlayer();
		if ( target )
			VectorAngles( target->GetAbsOrigin() - position, angles );
	}
	pPlayer->Teleport( &position, &angles, &vec3_origin );
	pPlayer->SetPunchAngle( vec3_angle );
	AddRecentlySpawnedPlayer( pPlayer );
	return NULL;
}
#endif

#include "l4d2/gamerules/terror_gamerules_console_server_callbacks.inc"
