//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Terror's game-specific engine client hooks.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "voice_gamemgr.h"
#include "entitylist.h"
#include "l4d2/director/terror_director.h"
#include "recipientfilter.h"
#include "usermessages.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CVoiceGameMgrHelper : public IVoiceGameMgrHelper
{
public:
	virtual bool CanPlayerHearPlayer( CBasePlayer *pListener, CBasePlayer *pTalker, bool &bProximity ) OVERRIDE
	{
		bProximity = false;
		return pListener->InSameTeam( pTalker );
	}
};

static CVoiceGameMgrHelper s_VoiceGameMgrHelper;
IVoiceGameMgrHelper *g_pVoiceGameMgrHelper = &s_VoiceGameMgrHelper;

void InstallGameRules()
{
	CreateGameRulesObject( "CTerrorGameRules" );
}

void ClientPutInServer( edict_t *pEdict, const char *pszPlayerName )
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( CBaseEntity::Instance( pEdict ) );
	if ( pPlayer )
	{
		Warning( "Second ClientPutInServer for %s\n", pszPlayerName );
	}
	else
	{
		pPlayer = CTerrorPlayer::CreatePlayer( "player", pEdict );
	}
	pPlayer->SetPlayerName( pszPlayerName );
}

void FinishClientPutInServer( CTerrorPlayer *pPlayer )
{
	// S2D03B0 invokes the real restoration virtual before ordinary admission.
	if ( !TheDirector->IsChangingMapOrMission() || !pPlayer->OnEndChangeLevel() )
	{
		pPlayer->InitialSpawn();
		pPlayer->m_takedamage = DAMAGE_NO;
		pPlayer->pl.deadflag = true;
		pPlayer->m_lifeState = LIFE_DEAD;
		pPlayer->AddEffects( EF_NODRAW );
		pPlayer->ChangeTeam( TEAM_UNASSIGNED );
		pPlayer->SetThink( NULL );
		pPlayer->SetNextThink( 0.0f );

		Vector origin = vec3_origin;
		QAngle angles = vec3_angle;
		if ( gpGlobals->eLoadType == MapLoad_Background )
		{
			CBaseEntity *pViewControl = gEntList.FindEntityByClassname( NULL, "point_viewcontrol" );
			if ( pViewControl )
			{
				origin = pViewControl->GetAbsOrigin();
				angles = pViewControl->GetAbsAngles();
			}
			else TerrorGameRules()->GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &origin, &angles, NULL );
		}
		else TerrorGameRules()->GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &origin, &angles, NULL );
		pPlayer->Teleport( &origin, &angles, NULL );
		pPlayer->SetMoveType( MOVETYPE_NONE );
	}
	pPlayer->ResetNumSpawns();
	pPlayer->SetViewEntity( NULL );
	if ( !TheDirector->IsChangingMapOrMission() )
	{
		DevMsg( "FinishClientPutInServer %s(%d): looking for bots to take over\n", pPlayer->GetPlayerName(), pPlayer->GetUserID() );
		if ( !pPlayer->TakeOverBot( true ) && TheDirector->IsSpeechDisabled() &&
			( TerrorGameRules()->IsCoopMode() || TerrorGameRules()->IsRealismMode() ) )
		{
			CSingleUserRecipientFilter filter( pPlayer );
			UserMessageBegin( filter, "JoinLateMsg" );
			MessageEnd();
		}
	}

	char szName[128];
	Q_strncpy( szName, pPlayer->GetPlayerName(), sizeof( szName ) );
	if ( !szName[0] )
	{
		Q_strncpy( szName, engine->GetClientConVarValue( pPlayer->entindex(), "name" ), sizeof( szName ) );
	}
	for ( char *pChar = szName; *pChar; ++pChar )
	{
		if ( *pChar == '%' )
		{
			*pChar = ' ';
		}
	}

	if ( pPlayer->IsNetClient() ||
		( developer.GetInt() > 1 && !Q_stristr( szName, "Zombie" ) && !Q_stristr( szName, "Survivor" ) ) )
	{
		UTIL_ClientPrintAll( HUD_PRINTNOTIFY, "#Game_connected", szName[0] ? szName : "<unconnected>" );
	}
}

void ClientActive( edict_t *pEdict, bool bLoadGame )
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( CBaseEntity::Instance( pEdict ) );
	if ( pPlayer->GetTeamNumber() != TEAM_UNASSIGNED )
	{
		Warning( "Second ClientActive on %s\n", pPlayer->GetPlayerName() );
		return;
	}
	if ( TheDirector->IsChangingMapOrMission() )
	{
		char name[32];
		Q_strncpy( name, engine->GetClientConVarValue( pPlayer->entindex(), "name" ), sizeof( name ) );
		pPlayer->SetPlayerName( name );
	}
	FinishClientPutInServer( pPlayer );
}

const char *GetGameDescription()
{
	return "Left 4 Dead 2";
}

void ClientGamePrecache()
{
	CBaseEntity::PrecacheModel( "sprites/white.vmt" );
	CBaseEntity::PrecacheScriptSound( "Bounce.PistolShell" );
	CBaseEntity::PrecacheScriptSound( "Bounce.RifleShell" );
	CBaseEntity::PrecacheScriptSound( "Bounce.ShotgunShell" );
	engine->ForceExactFile( "scripts/dsp_presets.txt" );
	engine->ForceExactFile( "scripts/instructor_lessons.txt" );
}

void GameStartFrame()
{
	// Original 0x2D0300 is empty. IGameSystem dispatches rules Think after entities.
}

void InitBodyQue()
{
	// Original 0x23B0B0 is genuinely empty.
}
