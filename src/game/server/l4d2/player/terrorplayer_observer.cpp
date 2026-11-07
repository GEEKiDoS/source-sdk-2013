//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/transitioning_player.h"
#include "l4d2/entities/survivor_rescue.h"
#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotManager.h"
#include "KeyValues.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

static void TerrorObserverFilterChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	// S:0x2FA590. Invalid filter strings restore the previous value.
	ConVar *pFilter = static_cast< ConVar * >( pVar );
	const char *pszValue = pFilter->GetString();
	if ( Q_stricmp( pszValue, "any" ) && Q_stricmp( pszValue, "human" ) &&
		Q_stricmp( pszValue, "bot" ) && Q_stricmp( pszValue, "survivor" ) &&
		Q_stricmp( pszValue, "infected" ) )
		pFilter->SetValue( pOldValue );
}

static ConVar spec_filter( "spec_filter", "any", 0x00000006,
	"Spectate a subset of all players (any|human|bot|survivor|infected)", TerrorObserverFilterChanged );
static ConVar spec_filter_infected( "spec_filter_infected", "any", 0x00000006,
	"Spectate filter for infected players (any|human|bot|survivor|infected)", TerrorObserverFilterChanged );
static ConVar spec_filter_survivors( "spec_filter_survivors", "survivor", 0x00000006,
	"Spectate filter for survivor players (any|human|bot|survivor|infected)", TerrorObserverFilterChanged );
static ConVar director_spectate_specials( "director_spectate_specials", "0", 0x00004004, "Allow spectating zombies" );

static int CountLivingObserverPlayers( int nTeam, bool bIncludeBots )
{
	// S:0x21F3B0: native connected state includes a disconnect in progress.
	int nCount = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && pPlayer->IsConnected() && pPlayer->IsAlive() &&
			( nTeam == TEAM_ANY || pPlayer->GetTeamNumber() == nTeam ) &&
			( bIncludeBots || !pPlayer->IsBot() ) )
			++nCount;
	}
	return nCount;
}

bool CTerrorPlayer::PassesObserverFilter( CBaseEntity *pTarget )
{
	// S:0x3078D0. Each restrictive filter has its own original fallback.
	if ( m_lifeState == LIFE_RESPAWNABLE )
		return false;
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pTarget );
	if ( pPlayer && pPlayer->IsGhost() )
		return false;
	if ( !IsNetClient() )
		return true;
	const char *pszFilter = GetTeamNumber() == TEAM_INFECTED ? spec_filter_infected.GetString() :
		( GetTeamNumber() == TEAM_SURVIVOR ? spec_filter_survivors.GetString() : spec_filter.GetString() );
	if ( !pTarget )
		return true;
	if ( !Q_stricmp( pszFilter, "human" ) )
		return CountLivingObserverPlayers( TEAM_ANY, false ) == 0 || ( pPlayer && !pPlayer->IsBot() );
	if ( !Q_stricmp( pszFilter, "bot" ) )
		return CountLivingObserverPlayers( TEAM_ANY, true ) == CountLivingObserverPlayers( TEAM_ANY, false ) ||
			( pPlayer && pPlayer->IsBot() );
	if ( !Q_stricmp( pszFilter, "survivor" ) )
		return CountLivingObserverPlayers( TEAM_SURVIVOR, true ) == 0 ||
			( pPlayer && pPlayer->GetTeamNumber() == TEAM_SURVIVOR );
	if ( !Q_stricmp( pszFilter, "infected" ) )
		return CountLivingObserverPlayers( TEAM_INFECTED, true ) == 0 ||
			( pPlayer && pPlayer->GetTeamNumber() == TEAM_INFECTED );
	return true;
}

bool CTerrorPlayer::IsValidObserverTarget( CBaseEntity *pTarget )
{
	if ( !pTarget )
		return false;
	// S:0x308D40. The original non-player exception is CTransitioningPlayer,
	// a chapter-transition producer, not a corpse, physics prop or map camera.
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pTarget );
	if ( !pPlayer )
		return dynamic_cast< CTransitioningPlayer * >( pTarget ) != NULL;
	if ( pPlayer == this || pPlayer->IsEffectActive( EF_NODRAW ) || pPlayer->m_lifeState == LIFE_RESPAWNABLE )
		return false;
	if ( ( pPlayer->m_lifeState == LIFE_DEAD || pPlayer->m_lifeState == LIFE_DYING ) &&
		pPlayer->State_Get() != STATE_DEATH_ANIM )
		return false;

	static ConVarRef mp_forcecamera( "mp_forcecamera" );
	if ( GetTeamNumber() != TEAM_SPECTATOR )
	{
		if ( mp_forcecamera.GetInt() == OBS_ALLOW_TEAM && GetTeamNumber() != pPlayer->GetTeamNumber() )
			return false;
		if ( mp_forcecamera.GetInt() == OBS_ALLOW_NONE )
			return false;
	}
	if ( pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		return false;
	if ( pPlayer->GetTeamNumber() == TEAM_INFECTED && !director_spectate_specials.GetBool() &&
		( GetTeamNumber() == TEAM_SURVIVOR || !TerrorGameRules()->IsPlayerControlledZombies() ) )
		return false;
	if ( !PassesObserverFilter( pPlayer ) )
		return false;

	if ( GetTeamNumber() == TEAM_SPECTATOR )
	{
		CTerrorPlayer *pBot = GetSpectatedBot();
		if ( pBot )
			return pPlayer == pBot;
	}
	return true;
}

class CTerrorObserverTargetCollector
{
public:
	CTerrorObserverTargetCollector( CTerrorPlayer *pObserver, CUtlVector< CBaseEntity * > &targets )
		: m_pObserver( pObserver ), m_targets( targets )
	{
	}

	bool operator()( CBaseCombatCharacter *pTarget )
	{
		if ( m_pObserver->IsValidObserverTarget( pTarget ) )
			m_targets.AddToTail( pTarget );
		return true;
	}

private:
	CTerrorPlayer *m_pObserver;
	CUtlVector< CBaseEntity * > &m_targets;
};

void CTerrorPlayer::CollectObserverTargets( CUtlVector< CBaseEntity * > &targets )
{
	// S:0x330030 -> 0x322C10/0x317D40. Player order precedes the actual
	// population's non-player and chapter-transition observer records.
	targets.RemoveAll();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && pPlayer->IsConnected() && !pPlayer->MyNextBotPointer() && IsValidObserverTarget( pPlayer ) )
			targets.AddToTail( pPlayer );
	}
	CTerrorObserverTargetCollector collector( this, targets );
	TheNextBots().ForEachCombatCharacter( collector );
	const CUtlVector< CTransitioningPlayer * > &transitioning = CTransitioningPlayer::GetTransitioningPlayers();
	for ( int i = 0; i < transitioning.Count(); ++i )
		if ( IsValidObserverTarget( transitioning[i] ) )
			targets.AddToTail( transitioning[i] );
}

CBaseEntity *CTerrorPlayer::FindNextObserverTarget( bool bReverse )
{
	// S:0x335640 wraps the collected list; an absent old target selects its head.
	CUtlVector< CBaseEntity * > targets;
	CollectObserverTargets( targets );
	if ( targets.Count() == 0 )
		return NULL;
	const int nCurrent = targets.Find( GetObserverTarget() );
	if ( nCurrent == targets.InvalidIndex() )
		return targets[0];
	int nNext = nCurrent + ( bReverse ? -1 : 1 );
	if ( nNext < 0 )
		nNext = targets.Count() - 1;
	else if ( nNext >= targets.Count() )
		nNext = 0;
	return targets[nNext];
}

int CTerrorPlayer::GetNextObserverSearchStartPoint( bool bReverse )
{
	// CS S:0x2419C0 follows a dead player's own observer target first.
	CTerrorPlayer *pTarget = ToTerrorPlayer( GetObserverTarget() );
	if ( pTarget && !pTarget->IsAlive() && pTarget->GetObserverTarget() )
		return pTarget->GetObserverTarget()->entindex();
	return BaseClass::GetNextObserverSearchStartPoint( bReverse );
}

bool CTerrorPlayer::IsGenuineSurvivorBot( CTerrorPlayer *pPlayer )
{
	// Native NextBot exposes the real SurvivorBot identity. Fake-client or
	// generic bot flags alone must never authorize survivor possession.
	INextBot *pBot = pPlayer ? pPlayer->MyNextBotPointer() : NULL;
	return pBot && pBot->MySurvivorBotPointer() != NULL;
}

CTerrorPlayer *CTerrorPlayer::FindSurvivorBotForCharacter( SurvivorCharacterType character )
{
	// S:0x303E20 stops at the first matching character, even if it is human.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && pPlayer->IsConnected() &&
			( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
			pPlayer->GetSurvivorCharacter() == character )
			return IsGenuineSurvivorBot( pPlayer ) ? pPlayer : NULL;
	}
	return NULL;
}

void CTerrorPlayer::BeginObservingSurvivorBot( CTerrorPlayer *pBot )
{
	// Human portion of S:0x306BD0/0x338D90. The real bot owns the preceding
	// SetHumanSpectator operation and its userid/connection lifetime.
	if ( !IsGenuineSurvivorBot( pBot ) )
		return;
	SetObserverTarget( pBot );
	if ( GetObserverMode() == OBS_MODE_ROAMING )
		SetObserverMode( OBS_MODE_CHASE );
	KeyValues *pData = new KeyValues( "data" );
	pData->SetInt( "character", pBot->GetSurvivorCharacter() );
	ShowViewPortPanel( "takeover_survivor_bar", true, pData );
	pData->deleteThis();
}

static void GetTerrorObserverTargetView( CTerrorPlayer *pObserver, CBaseEntity *pTarget, Vector &origin, QAngle &angles )
{
	// S:0x1732F0/0x30A3E0 trace an eight-unit camera hull, then subtract the
	// original dead-view offset. This is not the SDK's duck-player hull trace.
	MDLCACHE_CRITICAL_SECTION();
	const Vector start = pTarget->EyePosition();
	angles = pTarget->EyeAngles();
	Vector forward;
	AngleVectors( angles, &forward );
	VectorNormalize( forward );
	trace_t trace;
	UTIL_TraceHull( start, start - forward * 64.0f,
		Vector( -8.0f, -8.0f, -8.0f ), Vector( 8.0f, 8.0f, 8.0f ),
		pObserver->PhysicsSolidMaskForEntity(), pTarget, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
	origin = trace.endpos - VEC_DEAD_VIEWHEIGHT;
}

bool CTerrorPlayer::CanRoam()
{
	// S:0x3076E0 consults this actual client's userinfo, not a server-wide bool.
	const char *pszAllow = engine->GetClientConVarValue( entindex(), "spec_allowroaming" );
	if ( pszAllow && atoi( pszAllow ) == 0 )
		return false;
	if ( GetTeamNumber() == TEAM_SPECTATOR && GetSpectatedBot() )
		return false;
	return true;
}

bool CTerrorPlayer::SetObserverTarget( CBaseEntity *pTarget )
{
	// S:0x30A3E0 accepts NULL and resets FOV even for an unchanged target.
	// Assembly at 0x30A459/0x30A4BC supplies this player as the requester.
	if ( pTarget && !IsValidObserverTarget( pTarget ) )
		return false;
	const bool bChanged = m_hObserverTarget.Get() != pTarget;
	if ( bChanged )
		m_hObserverTarget = pTarget;
	SetFOV( this, 0 );
	ClearDelayedDSP();
	if ( bChanged && pTarget && GetObserverMode() == OBS_MODE_ROAMING && !IsFakeClient() )
	{
		const char *pszCinematic = engine->GetClientConVarValue( entindex(), "spec_cinematic" );
		if ( !pszCinematic || atoi( pszCinematic ) == 0 )
		{
			Vector origin;
			QAngle angles;
			GetTerrorObserverTargetView( this, pTarget, origin, angles );
			JumptoPosition( origin, angles );
		}
	}
	return true;
}

bool CTerrorPlayer::SetObserverMode( int nMode )
{
	// Terror S:0x2FCC00 keeps a rescued survivor in its fixed closet view.
	if ( GetObserverMode() == OBS_MODE_FIXED )
	{
		CBaseEntity *pEntity = NULL;
		while ( ( pEntity = gEntList.FindEntityByClassname( pEntity, "info_survivor_rescue" ) ) != NULL )
		{
			CSurvivorRescue *pRescue = dynamic_cast< CSurvivorRescue * >( pEntity );
			if ( pRescue && pRescue->GetRescueTarget() == this )
				return false;
		}
	}

	// Original base S:0x1732F0. L4D2's paired native observer enum has no
	// PASSTIME POI entry between CHASE and ROAMING.
	if ( nMode < OBS_MODE_NONE || nMode > OBS_MODE_ROAMING )
		return false;
	if ( m_lifeState == LIFE_RESPAWNABLE )
	{
		nMode = OBS_MODE_ROAMING;
	}
	else if ( nMode > OBS_MODE_FIXED )
	{
		if ( IsNetClient() && !IsFakeClient() && !CanRoam() )
		{
			int nLivingTargets = 0;
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( pPlayer && pPlayer->IsConnected() && pPlayer->IsAlive() && IsValidObserverTarget( pPlayer ) )
					++nLivingTargets;
			}
			if ( nMode == OBS_MODE_ROAMING && nLivingTargets != 0 )
				nMode = OBS_MODE_IN_EYE;
		}
		if ( GetTeamNumber() > TEAM_SPECTATOR )
		{
			static ConVarRef mp_forcecamera( "mp_forcecamera" );
			if ( mp_forcecamera.GetInt() == OBS_ALLOW_TEAM )
				nMode = OBS_MODE_IN_EYE;
			else if ( mp_forcecamera.GetInt() == OBS_ALLOW_NONE )
				nMode = OBS_MODE_FIXED;
		}
	}

	if ( m_iObserverMode > OBS_MODE_DEATHCAM )
		m_iObserverLastMode = m_iObserverMode;
	m_iObserverMode = nMode;
	Vector origin = EyePosition();
	QAngle angles = EyeAngles();
	CBaseEntity *pTarget = GetObserverTarget();
	if ( pTarget )
		GetTerrorObserverTargetView( this, pTarget, origin, angles );

	switch ( nMode )
	{
	case OBS_MODE_NONE:
	case OBS_MODE_DEATHCAM:
	case OBS_MODE_FIXED:
		SetFOV( this, 0 );
		SetViewOffset( vec3_origin );
		if ( pTarget )
			JumptoPosition( origin, angles );
		SetMoveType( MOVETYPE_NONE );
		break;
	case OBS_MODE_IN_EYE:
	case OBS_MODE_CHASE:
		SetObserverTarget( GetObserverTarget() );
		SetMoveType( MOVETYPE_OBSERVER );
		break;
	case OBS_MODE_ROAMING:
		{
			SetFOV( this, 0 );
			if ( IsFakeClient() )
			{
				SetObserverTarget( NULL );
			}
			else
			{
				const char *pszCinematic = engine->GetClientConVarValue( entindex(), "spec_cinematic" );
				if ( pszCinematic && atoi( pszCinematic ) != 0 )
					SetObserverTarget( NULL );
				else
					SetObserverTarget( GetObserverTarget() );
			}
			SetViewOffset( vec3_origin );
			if ( pTarget )
				JumptoPosition( origin, angles );
			SetMoveType( MOVETYPE_OBSERVER );
		}
		break;
	}
	CheckObserverSettings();
	return true;
}

void CTerrorPlayer::CheckObserverSettings()
{
	// S:0x16F150 does not validate a fixed rescue view as an in-eye target.
	if ( m_bForcedObserverMode )
	{
		CBaseEntity *pTarget = GetObserverTarget();
		if ( !IsValidObserverTarget( pTarget ) )
			pTarget = FindNextObserverTarget( false );
		if ( pTarget )
		{
			m_bForcedObserverMode = false;
			SetObserverMode( m_iObserverLastMode );
			SetObserverTarget( pTarget );
		}
		return;
	}
	if ( m_iObserverLastMode < OBS_MODE_FIXED )
		m_iObserverLastMode = OBS_MODE_ROAMING;
	if ( m_iObserverMode != OBS_MODE_IN_EYE && m_iObserverMode != OBS_MODE_CHASE )
		return;

	ValidateCurrentObserverTarget();
	CTerrorPlayer *pTarget = ToTerrorPlayer( GetObserverTarget() );
	if ( !pTarget )
		return;
	if ( m_iObserverMode == OBS_MODE_IN_EYE )
	{
		const int nMask = FL_ONGROUND | FL_DUCKING;
		const int nFlags = pTarget->GetFlags() & nMask;
		if ( ( GetFlags() & nMask ) != nFlags )
		{
			const int nCombinedFlags = ( GetFlags() & ~nMask ) | nFlags;
			ClearFlags();
			AddFlag( nCombinedFlags );
		}
		if ( GetViewOffset() != pTarget->GetViewOffset() )
			SetViewOffset( pTarget->GetViewOffset() );
	}
	if ( m_Local.m_PlayerFog.m_hCtrl.Get() != pTarget->m_Local.m_PlayerFog.m_hCtrl.Get() )
		m_Local.m_PlayerFog.m_hCtrl.Set( pTarget->m_Local.m_PlayerFog.m_hCtrl.Get() );
}

static ConVar director_afk_timeout( "director_afk_timeout", "45", 0x00000106,
	"Humans idle longer than this many seconds will be forced to spectator mode" );

void CCSPlayer::UpdateLastActivity()
{
	// S:0x242B90 runs after the native base PreThink. Neither this field nor
	// the view-angle snapshot is the last-received-usercmd timestamp.
	if ( m_afButtonLast != m_nButtons )
		m_flLastActivityTime = gpGlobals->curtime;
	if ( IsAlive() || GetObserverMode() == OBS_MODE_ROAMING || GetObserverMode() == OBS_MODE_CHASE )
	{
		const QAngle &angles = EyeAngles();
		if ( angles != m_lastActivityAngles )
		{
			m_flLastActivityTime = gpGlobals->curtime;
			m_lastActivityAngles = angles;
		}
	}
}

bool CTerrorPlayer::IsIdle() const
{
	// S:0x301010 uses a strict world-time comparison. Spectators subtract
	// one second from the timeout; a zero timeout disables automatic idling.
	const float flTimeout = director_afk_timeout.GetFloat();
	if ( flTimeout == 0.0f || IsBot() )
		return false;
	const float flThreshold = GetTeamNumber() == TEAM_SPECTATOR ? flTimeout - 1.0f : flTimeout;
	static ConVarRef nav_edit( "nav_edit" );
	return gpGlobals->curtime > m_flLastActivityTime + flThreshold && !nav_edit.GetBool();
}

static int CountHumanPlayersOnTeam( int nTeam )
{
	// S:0x2511E0(team,false) includes dead humans and does not count bots.
	int nCount = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && !pPlayer->IsBot() && pPlayer->GetTeamNumber() == nTeam )
			++nCount;
	}
	return nCount;
}

void CTerrorPlayer::GoAwayFromKeyboard()
{
	// S:0x2FC3F0 only advances the real idle clock. Possession, team transfer
	// and the real SurvivorBot substitute are owned by the subsequent path.
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
		return;
	if ( CountHumanPlayersOnTeam( TEAM_SURVIVOR ) == 1 &&
		CountHumanPlayersOnTeam( TEAM_INFECTED ) == 0 && GetTeamNumber() == TEAM_SURVIVOR )
		return;
	m_flLastActivityTime = gpGlobals->curtime - director_afk_timeout.GetFloat() - 1.0f;
}

CON_COMMAND_F( go_away_from_keyboard, "Become an idle observer." , 0x00000004)
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( pPlayer )
		pPlayer->GoAwayFromKeyboard();
}
