//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/l4d_awards_campaign.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/infected/common/infected.h"
#include "team.h"
#include "NextBot/NextBotManager.h"
#include "tier0/memdbgon.h"

namespace
{
	bool StartedAtPlayerStart( const CTerrorPlayer *player )
	{
		// Native player+12524, distinct from GetLastKnownArea/current nav.
		const TerrorNavArea *area = ToTerrorNavArea( player->GetSpawnArea() );
		return area && area->HasSpawnAttributes( TERROR_NAV_PLAYER_START );
	}

	bool HasOtherSurvivors()
	{
		CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
		// Native S377590 tests != 1, not > 1 (including a missing team).
		return !team || team->GetNumPlayers() != 1;
	}

	bool IsConnectedAwardPlayer( CTerrorPlayer *player )
	{
		return player && !FNullEnt( player->edict() ) && player->entindex() && player->IsPlayer() && player->IsConnected();
	}

	struct SurvivorStandingCount
	{
		SurvivorStandingCount() : standing( 0 ), incapacitated( 0 ), dead( 0 ) {}
		void Count()
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( !IsConnectedAwardPlayer( player ) || player->GetTeamNumber() != TEAM_SURVIVOR )
					continue;
				if ( player->IsIncapacitated() )
					++incapacitated;
				else if ( player->IsAlive() )
					++standing;
				else
					++dead;
			}
		}
		int standing;
		int incapacitated;
		int dead;
	};

	struct NarrowEscapeSurvivors
	{
		NarrowEscapeSurvivors() : minimumHealth( 100 ), anyDead( false ), minimumFlow( 99999 ) {}
		void Count()
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( !IsConnectedAwardPlayer( player ) || player->GetTeamNumber() != TEAM_SURVIVOR )
					continue;
				if ( player->GetHealth() < minimumHealth && player->IsAlive() )
					minimumHealth = player->GetHealth();
				if ( player->IsAlive() )
				{
					// S37D9B0 truncates each accepted flow value to an integer.
					if ( minimumFlow > player->GetFlowDistance( FLOW_TOWARD_GOAL ) )
						minimumFlow = static_cast< int >( player->GetFlowDistance( FLOW_TOWARD_GOAL ) );
				}
				else
					anyDead = true;
			}
		}
		int minimumHealth;
		bool anyDead;
		int minimumFlow;
	};

	struct NarrowEscapeInfected
	{
		NarrowEscapeInfected() : count( 0 ), maximumFlow( 0 ) {}
		bool operator()( CBaseCombatCharacter *actor )
		{
			if ( actor->GetTeamNumber() != TEAM_INFECTED )
				return true;
			// Native counts infected players regardless of life state or arousal,
			// but only common Infected in the body's INTENSE arousal state.
			if ( actor->IsPlayer() )
				++count;
			else
			{
				Infected *infected = dynamic_cast< Infected * >( actor );
				if ( infected && infected->GetBodyInterface()->GetArousal() == IBody::INTENSE )
				{
					++count;
					if ( infected->GetFlowDistance() > maximumFlow )
						maximumFlow = static_cast< int >( infected->GetFlowDistance() );
				}
			}
			return true;
		}
		void Count()
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( IsConnectedAwardPlayer( player ) && !player->MyNextBotPointer() )
					( *this )( player );
			}
			TheNextBots().ForEachCombatCharacter( *this );
		}
		int count;
		int maximumFlow;
	};
}

CCheckpointCompleteAwardTemplate::CCheckpointCompleteAwardTemplate( int userID ) : CAwardTemplate( userID )
{
	ResetCampaign();
}

void CCheckpointCompleteAwardTemplate::ResetChapter()
{
	m_bHurt = false;
	m_bIncapacitated = false;
	m_bDied = false;
	m_bTeamHurt = false;
	m_bTeamIncapacitated = false;
	m_bTeamDied = false;
}

void CCheckpointCompleteAwardTemplate::ResetCampaign()
{
	ResetChapter();
	m_bCampaignHurt = false;
	m_bCampaignIncapacitated = false;
	m_bCampaignDied = false;
	m_bCampaignTeamHurt = false;
	m_bCampaignTeamIncapacitated = false;
	m_bCampaignTeamDied = false;
	m_bStartedAtPlayerStart = false;
}

void CCheckpointCompleteAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_ledge_grab" );
}

// Native S377590. Campaign flags are folded at a checkpoint, not on injury.
void CCheckpointCompleteAwardTemplate::OnCheckpoint()
{
	CTerrorPlayer *player = GetPlayer();
	if ( player->IsAlive() && player->IsInCheckpoint() )
	{
		if ( m_bDied )
		{
			m_bCampaignHurt = true;
			m_bCampaignIncapacitated = true;
			m_bCampaignDied = true;
		}
		else if ( m_bIncapacitated )
		{
			EarnAward( 7 );
			m_bCampaignHurt = true;
			m_bCampaignIncapacitated = true;
		}
		else if ( m_bHurt )
		{
			EarnAward( 8 );
			m_bCampaignHurt = true;
		}
		else
			EarnAward( 9 );
	}
	else
	{
		m_bCampaignHurt = true;
		m_bCampaignIncapacitated = true;
		m_bCampaignDied = true;
	}

	// Unlike the individual awards, the team branch has no alive/checkpoint
	// predicate. Do not make it depend on the preceding individual branch.
	if ( HasOtherSurvivors() )
	{
		if ( m_bTeamDied )
		{
			m_bCampaignTeamHurt = true;
			m_bCampaignTeamIncapacitated = true;
			m_bCampaignTeamDied = true;
		}
		else if ( m_bTeamIncapacitated )
		{
			EarnAward( 71 );
			m_bCampaignTeamHurt = true;
			m_bCampaignTeamIncapacitated = true;
		}
		else if ( m_bTeamHurt )
		{
			EarnAward( 72 );
			m_bCampaignTeamHurt = true;
		}
		else
			EarnAward( 73 );
	}
}

void CCheckpointCompleteAwardTemplate::OnEnemyDamage( CTerrorPlayer *player, CTerrorPlayer *victim, CBaseEntity *attacker )
{
	if ( !victim || !attacker )
		return;
	if ( attacker->GetTeamNumber() == player->GetTeamNumber() )
		return;
	if ( victim == player )
	{
		m_bHurt = true;
		m_bTeamHurt = true;
	}
	else if ( victim->GetTeamNumber() == player->GetTeamNumber() )
		m_bTeamHurt = true;
}

void CCheckpointCompleteAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) )
	{
		OnCheckpoint();
		ResetChapter();
	}
	else if ( !Q_stricmp( name, "player_first_spawn" ) )
	{
		if ( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
		{
			ResetCampaign();
			m_bStartedAtPlayerStart = StartedAtPlayerStart( player );
		}
	}
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		OnCheckpoint();
		if ( m_bStartedAtPlayerStart && !m_bCampaignDied )
			EarnAward( m_bCampaignIncapacitated ? 0 : m_bCampaignHurt ? 1 : 2 );
		if ( HasOtherSurvivors() && m_bStartedAtPlayerStart && !m_bCampaignTeamDied )
			EarnAward( m_bCampaignTeamIncapacitated ? 3 : m_bCampaignTeamHurt ? 4 : 5 );
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( victim == player )
		{
			m_bDied = true;
			m_bTeamDied = true;
		}
		else if ( victim && victim->GetTeamNumber() == player->GetTeamNumber() )
			m_bTeamDied = true;
	}
	else if ( !Q_stricmp( name, "player_incapacitated" ) )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( victim == player )
		{
			m_bIncapacitated = true;
			m_bTeamIncapacitated = true;
		}
		else if ( victim && victim->GetTeamNumber() == player->GetTeamNumber() )
			m_bTeamIncapacitated = true;
	}
	else if ( !Q_stricmp( name, "player_hurt" ) )
	{
		CBaseEntity *attacker = event->GetInt( "attacker" ) ?
			UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) : UTIL_EntityByIndex( event->GetInt( "attackerentid" ) );
		OnEnemyDamage( player, ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ), attacker );
	}
	else if ( !Q_stricmp( name, "player_ledge_grab" ) )
		OnEnemyDamage( player, ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ),
			UTIL_PlayerByUserId( event->GetInt( "causer" ) ) );
}

CDarkMissionAwardTemplate::CDarkMissionAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_bStartedAtPlayerStart( false ), m_bDied( false )
{
}

void CDarkMissionAwardTemplate::Init()
{
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "player_death" );
}

void CDarkMissionAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "finale_win" ) )
	{
		if ( !m_bDied && m_bStartedAtPlayerStart && !player->HasUsedFlashlightOutsideSafeArea() )
			EarnAward( 6 );
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
			m_bDied = true;
	}
	else if ( !Q_stricmp( name, "player_first_spawn" ) && UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
	{
		m_bDied = false;
		m_bStartedAtPlayerStart = StartedAtPlayerStart( player );
	}
}

CGoodTeammateAwardTemplate::CGoodTeammateAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_bChapterBadAward( false ), m_bCampaignBadAward( false ), m_bTeammateBadAward( false )
{
}

void CGoodTeammateAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "award_earned" );
	ListenForGameEvent( "finale_win" );
}

void CGoodTeammateAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) )
	{
		if ( player->IsInCheckpoint() && !m_bChapterBadAward )
			EarnAward( 74 );
		m_bChapterBadAward = false;
	}
	else if ( !Q_stricmp( name, "player_first_spawn" ) )
	{
		if ( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
		{
			m_bChapterBadAward = false;
			m_bCampaignBadAward = false;
			m_bTeammateBadAward = false;
		}
	}
	else if ( !Q_stricmp( name, "award_earned" ) )
	{
		CBasePlayer *recipient = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
		if ( recipient == player )
		{
			if ( GetAwardResponseType( event->GetInt( "award" ) ) == 2 )
			{
				m_bChapterBadAward = true;
				m_bCampaignBadAward = true;
			}
		}
		else if ( recipient && recipient->GetTeamNumber() == player->GetTeamNumber() && GetAwardResponseType( event->GetInt( "award" ) ) == 2 )
			m_bTeammateBadAward = true;
	}
	else if ( !Q_stricmp( name, "finale_win" ) && player->IsAlive() && !m_bCampaignBadAward )
	{
		EarnAward( 29 );
		if ( !m_bTeammateBadAward && HasOtherSurvivors() )
			EarnAward( 82 );
	}
}

CHeroAwardTemplate::CHeroAwardTemplate( int userID ) : CAwardTemplate( userID ), m_bWasLastStanding( false )
{
}

void CHeroAwardTemplate::Init()
{
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "map_transition" );
}

void CHeroAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_incapacitated" ) )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		CBasePlayer *attacker = UTIL_PlayerByUserId( event->GetInt( "attacker" ) );
		if ( victim == player )
			m_bWasLastStanding = false;
		else if ( victim && attacker && attacker != player && victim->GetTeamNumber() == player->GetTeamNumber() )
		{
			SurvivorStandingCount survivors;
			survivors.Count();
			if ( !player->IsIncapacitated() && player->IsAlive() && survivors.standing == 1 && survivors.dead == 0 && survivors.incapacitated >= 1 )
				m_bWasLastStanding = true;
		}
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		CBasePlayer *victim = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
		if ( victim && victim->GetTeamNumber() == player->GetTeamNumber() )
			m_bWasLastStanding = false;
	}
	else if ( !Q_stricmp( name, "map_transition" ) )
	{
		if ( m_bWasLastStanding )
		{
			SurvivorStandingCount survivors;
			survivors.Count();
			if ( !player->IsIncapacitated() && player->IsAlive() && survivors.standing > 1 && survivors.dead == 0 && survivors.incapacitated == 0 )
				EarnAward( 79 );
		}
		m_bWasLastStanding = false;
	}
}

CHulkShutOutAwardTemplate::CHulkShutOutAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_bTeamHurtByTank( false ), m_flLastAbortedTankDeath( -1.0f )
{
}

void CHulkShutOutAwardTemplate::Init()
{
	ListenForGameEvent( "player_spawn" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_death" );
}

void CHulkShutOutAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_hurt" ) )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		CTerrorPlayer *attacker = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) );
		if ( victim && attacker && victim->GetTeamNumber() == player->GetTeamNumber() &&
			attacker->GetTeamNumber() == TEAM_INFECTED && attacker->GetZombieClass() == 8 )
			m_bTeamHurtByTank = true;
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( !victim || victim->GetTeamNumber() != TEAM_INFECTED || victim->GetZombieClass() != 8 || TheDirector->IsFinale() )
			return;
		if ( event->GetBool( "abort" ) )
		{
			m_flLastAbortedTankDeath = gpGlobals->curtime;
			return;
		}
		if ( !m_bTeamHurtByTank )
			EarnAward( 81, UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) );
		else
			m_bTeamHurtByTank = false;
	}
	else if ( !Q_stricmp( name, "player_spawn" ) )
	{
		CTerrorPlayer *spawned = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( spawned && spawned->GetTeamNumber() == TEAM_INFECTED && spawned->GetZombieClass() == 8 &&
			gpGlobals->curtime != m_flLastAbortedTankDeath )
			m_bTeamHurtByTank = false;
	}
}

void CLeft4DeadAwardTemplate::Init()
{
	ListenForGameEvent( "mission_lost" );
}

void CLeft4DeadAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( event->GetName() && player && player->GetTeamNumber() == TEAM_SURVIVOR && !Q_stricmp( event->GetName(), "mission_lost" ) )
		EarnAward( 99 );
}

void CLeftBehindAwardTemplate::Init()
{
	ListenForGameEvent( "survivor_rescue_abandoned" );
}

void CLeftBehindAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( event->GetName() && player && player->GetTeamNumber() == TEAM_SURVIVOR &&
		!Q_stricmp( event->GetName(), "survivor_rescue_abandoned" ) && player->IsAlive() && !player->IsIncapacitated() )
		EarnAward( 100 );
}

void CNarrowEscapeAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
}

void CNarrowEscapeAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "map_transition" ) )
		return;
	NarrowEscapeSurvivors survivors;
	survivors.Count();
	static ConVarRef survivor_limp_health( "survivor_limp_health" );
	if ( survivors.minimumHealth < survivor_limp_health.GetInt() )
	{
		NarrowEscapeInfected infected;
		infected.Count();
		if ( infected.count >= 5 && survivors.minimumFlow - infected.maximumFlow <= 250 )
		{
			if ( !survivors.anyDead )
				EarnAward( 77 );
			else if ( player->IsAlive() )
				EarnAward( 25 );
		}
	}
}

CPerfectTeamAwardTemplate::CPerfectTeamAwardTemplate( int userID ) : CAwardTemplate( userID ), m_bTeamBadAward( false )
{
}

void CPerfectTeamAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "award_earned" );
}

void CPerfectTeamAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) )
	{
		if ( player->IsInCheckpoint() && HasOtherSurvivors() && !m_bTeamBadAward )
			EarnAward( 78 );
		m_bTeamBadAward = false;
	}
	else if ( !Q_stricmp( name, "player_first_spawn" ) )
	{
		if ( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
			m_bTeamBadAward = false;
	}
	else if ( !Q_stricmp( name, "award_earned" ) )
	{
		CBasePlayer *recipient = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
		if ( recipient && recipient->GetTeamNumber() == player->GetTeamNumber() && !m_bTeamBadAward && GetAwardResponseType( event->GetInt( "award" ) ) == 2 )
			m_bTeamBadAward = true;
	}
}

void CScenarioWinAwardTemplate::Init()
{
	ListenForGameEvent( "finale_win" );
}

void CScenarioWinAwardTemplate::FireGameEvent( IGameEvent *event )
{
	// Original S37ED20 (vtable 106A94E4), verified against all 0x2B bytes:
	// GetName, GetPlayer, pure GetTeamNumber, local epilogue/ret4. Both JZs
	// target 37ED46; no shared tail, award call or stat store is omitted.
	if ( event->GetName() )
	{
		CTerrorPlayer *player = GetPlayer();
		if ( player )
			player->GetTeamNumber();
	}
}

CToughGuyAwardTemplate::CToughGuyAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_bReceivedHealing( false ), m_bDied( false ), m_bStartedAtPlayerStart( false )
{
}

void CToughGuyAwardTemplate::Init()
{
	ListenForGameEvent( "heal_success" );
	ListenForGameEvent( "pills_used" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "player_death" );
}

void CToughGuyAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !event->GetName() || !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "finale_win" ) )
	{
		if ( !m_bDied && m_bStartedAtPlayerStart && !m_bReceivedHealing )
			EarnAward( 41 );
	}
	else if ( !Q_stricmp( name, "heal_success" ) || !Q_stricmp( name, "pills_used" ) )
	{
		// S380360 deliberately uses "subject" for pills_used as well as
		// heal_success. Do not repair this to the pills event's "userid".
		if ( UTIL_PlayerByUserId( event->GetInt( "subject" ) ) == player )
			m_bReceivedHealing = true;
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
			m_bDied = true;
	}
	else if ( !Q_stricmp( name, "player_first_spawn" ) && UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
	{
		m_bReceivedHealing = false;
		m_bDied = false;
		m_bStartedAtPlayerStart = StartedAtPlayerStart( player );
	}
}
