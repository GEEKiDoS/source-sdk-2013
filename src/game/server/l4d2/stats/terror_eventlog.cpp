//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "EventLog.h"
#include "l4d2/player/terrorplayer.h"
#include "team.h"
#include "nav_mesh.h"
#include "tier0/memdbgon.h"

ConVar mp_logdetail( "mp_logdetail", "0", 0x00000006, "Logs attacks.  Values are: 0=off, 1=enemy, 2=teammate, 3=both)", true, 0.0f, true, 3.0f );

class CTerrorEventLog : public CEventLog
{
private:
	typedef CEventLog BaseClass;

public:
	virtual bool Init();

protected:
	virtual bool PrintEvent( IGameEvent *pEvent );
	virtual void GetEntityIdentity( CBaseEntity *pEntity, char *pBuffer, int nBufferSize );
	bool PrintTerrorEvent( IGameEvent *pEvent );
};

static CTerrorEventLog g_TerrorEventLog;

IGameSystem *GameLogSystem()
{
	return &g_TerrorEventLog;
}

bool CTerrorEventLog::Init()
{
	BaseClass::Init();
	ListenForGameEvent( "round_end" );
	ListenForGameEvent( "round_start" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_spawn" );
	return true;
}

bool CTerrorEventLog::PrintEvent( IGameEvent *pEvent )
{
	if ( PrintTerrorEvent( pEvent ) )
		return true;
	return BaseClass::PrintEvent( pEvent );
}

static const char *GetTerrorLogClass( CTerrorPlayer *pPlayer )
{
	if ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
	{
		static const char *s_pSurvivorNames[] =
		{
			"Gambler", "Producer", "Coach", "Mechanic", "NamVet", "TeenGirl", "Biker", "Manager"
		};
		int nCharacter = pPlayer->GetSurvivorCharacter();
		return nCharacter >= 0 && nCharacter < ARRAYSIZE( s_pSurvivorNames ) ? s_pSurvivorNames[nCharacter] : "Unknown";
	}
	if ( pPlayer->GetTeamNumber() == TEAM_INFECTED )
		return GetZombieClassName( pPlayer->m_zombieClass );
	return "";
}

void CTerrorEventLog::GetEntityIdentity( CBaseEntity *pEntity, char *pBuffer, int nBufferSize )
{
	if ( !pBuffer || nBufferSize <= 0 )
		return;
	if ( !pEntity )
	{
		Q_snprintf( pBuffer, nBufferSize, "\"%s<%i><%s><%s><Area %d>\"", "Unknown", 0, "", "", 0 );
		return;
	}

	CNavArea *pArea = pEntity->MyCombatCharacterPointer() && TheNavMesh ? TheNavMesh->GetNavArea( pEntity->GetAbsOrigin() ) : NULL;
	int nAreaID = pArea ? pArea->GetID() : 0;
	CTeam *pTeam = pEntity->GetTeam();
	const char *pTeamName = pTeam ? pTeam->GetName() : "";
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
	if ( pPlayer )
	{
		bool bAlive = pPlayer->IsAlive();
		const char *pState = bAlive ? ( pPlayer->IsOnFire() ? "BURNING" : "ALIVE" ) : "DEAD";
		Vector vecPosition = bAlive ? pPlayer->EyePosition() : pPlayer->GetAbsOrigin();
		const QAngle &angles = pPlayer->EyeAngles();
		Q_snprintf( pBuffer, nBufferSize,
			"\"%s<%i><%s><%s><%s><%s><%d+%d><%s %2.2f %2.2f %2.2f; setang %2.2f %2.2f %2.2f><Area %d>\"",
			pPlayer->GetPlayerName(), pPlayer->GetUserID(), pPlayer->GetNetworkIDString(), pTeamName,
			GetTerrorLogClass( pPlayer ), pState, pPlayer->GetHealth(), 0, bAlive ? "setpos_exact" : "setpos",
			vecPosition.x, vecPosition.y, vecPosition.z, angles.x, angles.y, angles.z, nAreaID );
		return;
	}

	const Vector &vecPosition = pEntity->GetAbsOrigin();
	const QAngle &angles = pEntity->EyeAngles();
	const char *pClassname = pEntity->GetClassname();
	if ( pEntity->MyCombatCharacterPointer() )
	{
		Q_snprintf( pBuffer, nBufferSize,
			"\"%s<%i><%s><%s><%s><%s><%d><setpos_exact %2.2f %2.2f %2.2f; setang %2.2f %2.2f %2.2f><Area %d>\"",
			pClassname, pEntity->entindex(), "", pTeamName, pClassname, pEntity->IsAlive() ? "ALIVE" : "DEAD",
			pEntity->GetHealth(), vecPosition.x, vecPosition.y, vecPosition.z, angles.x, angles.y, angles.z, nAreaID );
	}
	else
	{
		Q_snprintf( pBuffer, nBufferSize,
			"\"%s<%i><%s><%s><%s><%s><%d><setpos_exact %2.2f %2.2f %2.2f; setang %2.2f %2.2f %2.2f><Area 0>\"",
			pClassname, pEntity->entindex(), "", pTeamName, pClassname, pEntity->IsAlive() ? "ALIVE" : "DEAD",
			pEntity->GetHealth(), vecPosition.x, vecPosition.y, vecPosition.z, angles.x, angles.y, angles.z );
	}
}

bool CTerrorEventLog::PrintTerrorEvent( IGameEvent *pEvent )
{
	const char *pName = pEvent->GetName();
	if ( !Q_strncmp( pName, "round_end", Q_strlen( "round_end" ) ) )
	{
		int nWinner = pEvent->GetInt( "winner" );
		int nReason = pEvent->GetInt( "reason" );
		const char *pMessage = pEvent->GetString( "message" );
		if ( *pMessage == '#' )
			++pMessage;
		if ( nReason == 16 )
		{
			UTIL_LogPrintf( "World triggered \"Game_Commencing\"\n" );
			return true;
		}

		CTeam *pInfected = GetGlobalTeam( TEAM_INFECTED );
		CTeam *pSurvivor = GetGlobalTeam( TEAM_SURVIVOR );
		if ( nWinner == TEAM_SURVIVOR || nWinner == TEAM_INFECTED )
		{
			CTeam *pWinner = nWinner == TEAM_SURVIVOR ? pSurvivor : pInfected;
			UTIL_LogPrintf( "Team \"%s\" triggered \"%s\" (Infected \"%i\") (Survivor \"%i\")\n",
				pWinner->GetName(), pMessage, pInfected->GetScore(), pSurvivor->GetScore() );
		}
		else
		{
			UTIL_LogPrintf( "World triggered \"%s\" (Infected \"%i\") (Survivor \"%i\")\n",
				pMessage, pInfected->GetScore(), pSurvivor->GetScore() );
		}
		UTIL_LogPrintf( "Team \"Infected\" scored \"%i\" with \"%i\" players\n", pInfected->GetScore(), pInfected->GetNumPlayers() );
		UTIL_LogPrintf( "Team \"Survivor\" scored \"%i\" with \"%i\" players\n", pSurvivor->GetScore(), pSurvivor->GetNumPlayers() );
		UTIL_LogPrintf( "World triggered \"Round_End\"\n" );
		return true;
	}
	if ( !Q_strncmp( pName, "server_", 7 ) )
		return false;
	if ( !Q_strcmp( pName, "player_hurt" ) )
	{
		CBasePlayer *pVictim = UTIL_PlayerByUserId( pEvent->GetInt( "userid" ) );
		if ( !pVictim )
			return false;
		const char *pWeapon = pEvent->GetString( "weapon" );
		int nAttacker = pEvent->GetInt( "attacker" );
		CBaseEntity *pAttacker = nAttacker ? UTIL_PlayerByUserId( nAttacker ) : UTIL_EntityByIndex( pEvent->GetInt( "attackerentid" ) );
		if ( !pAttacker )
			return false;
		bool bTeamAttack = pAttacker->GetTeamNumber() == pVictim->GetTeamNumber() && pAttacker != pVictim;
		if ( !( mp_logdetail.GetInt() & ( bTeamAttack ? 2 : 1 ) ) )
			return true;

		const char *pHitgroup = "GENERIC";
		switch ( pEvent->GetInt( "hitgroup" ) )
		{
		case 0: pHitgroup = "generic"; break;
		case 1: pHitgroup = "head"; break;
		case 2: pHitgroup = "chest"; break;
		case 3: pHitgroup = "stomach"; break;
		case 4: pHitgroup = "left arm"; break;
		case 5: pHitgroup = "right arm"; break;
		case 6: pHitgroup = "left leg"; break;
		case 7: pHitgroup = "right leg"; break;
		}
		char szAttacker[256], szVictim[256];
		GetEntityIdentity( pAttacker, szAttacker, sizeof( szAttacker ) );
		GetEntityIdentity( pVictim, szVictim, sizeof( szVictim ) );
		UTIL_LogPrintf( "(DAMAGE)%s attacked %s with \"%s\" (damage \"%d\") (damage_armor \"%d\") (health \"%d\") (armor \"%d\") (hitgroup \"%s\")\n",
			szAttacker, szVictim, pWeapon, pEvent->GetInt( "dmg_health" ), pEvent->GetInt( "dmg_armor" ),
			pEvent->GetInt( "health" ), pEvent->GetInt( "armor" ), pHitgroup );
		return true;
	}
	if ( !Q_strncmp( pName, "player_death", Q_strlen( "player_death" ) ) )
	{
		int nUserID = pEvent->GetInt( "userid" );
		CBaseEntity *pVictim;
		if ( nUserID )
			pVictim = UTIL_PlayerByUserId( nUserID );
		else
		{
			int nEntityID = pEvent->GetInt( "entityid" );
			if ( !nEntityID )
				return false;
			pVictim = UTIL_EntityByIndex( nEntityID );
		}
		if ( !pVictim )
			return false;

		const char *pWeapon = pEvent->GetString( "weapon" );
		bool bHeadshot = pEvent->GetInt( "headshot" ) == 1;
		int nAttacker = pEvent->GetInt( "attacker" );
		CBaseEntity *pAttacker = nAttacker ? UTIL_PlayerByUserId( nAttacker ) : UTIL_EntityByIndex( pEvent->GetInt( "attackerentid" ) );
		char szAttacker[256], szVictim[256];
		GetEntityIdentity( pAttacker, szAttacker, sizeof( szAttacker ) );
		GetEntityIdentity( pVictim, szVictim, sizeof( szVictim ) );
		if ( pVictim == pAttacker )
			UTIL_LogPrintf( "(DEATH)%s committed suicide with \"%s\"\n", szVictim, pWeapon );
		else if ( pAttacker )
			UTIL_LogPrintf( "(DEATH)%s killed %s with \"%s\"%s\n", szAttacker, szVictim, pWeapon, bHeadshot ? " (headshot)" : "" );
		else
			UTIL_LogPrintf( "(DEATH)%s died from unknown attacker (probably World)\n", szVictim );
		return true;
	}
	if ( FStrEq( pName, "player_spawn" ) )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByUserId( pEvent->GetInt( "userid" ) );
		if ( pPlayer )
		{
			char szPlayer[256];
			GetEntityIdentity( pPlayer, szPlayer, sizeof( szPlayer ) );
			UTIL_LogPrintf( "%s spawned\n", szPlayer );
			return true;
		}
	}
	else if ( !Q_strncmp( pName, "round_start", Q_strlen( "round_start" ) ) )
	{
		UTIL_LogPrintf( "World triggered \"Round_Start\"\n" );
		return true;
	}
	return false;
}
