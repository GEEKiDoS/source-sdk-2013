//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_team_size.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/director/zombiemanager.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

// Native EnsureTeamSize S30E710 / S309E80. Collection order is client-index
// order; human displacement precedes the bot pass. Neither vector is sorted.
class EnsureTeamSize : public IPlayerFunctor
{
public:
	EnsureTeamSize( int desiredSize, int team, int displacedTeam ) :
		m_desiredSize( desiredSize ), m_team( team ), m_displacedTeam( displacedTeam ) {}
	virtual bool operator()( CBasePlayer *basePlayer ) OVERRIDE
	{
		if ( basePlayer && basePlayer->IsPlayer() && basePlayer->GetTeamNumber() == m_team )
		{
			CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( basePlayer );
			// Ledger306: the native wrong-type cast has no defined field access.
			if ( player )
			{
				if ( player->IsFakeClient() ) m_bots.AddToTail( player );
				else m_humans.AddToTail( player );
			}
		}
		return true;
	}
	virtual void OnEndIteration( bool allElementsIterated ) OVERRIDE
	{
		while ( m_humans.Count() > 0 && m_desiredSize < m_humans.Count() )
		{
			m_humans[0]->CommitSuicide( false );
			m_humans[0]->ChangeTeam( m_displacedTeam );
			m_humans[0]->m_wantsToPlay = true;
			m_humans.Remove( 0 );
		}
		while ( m_bots.Count() > 0 && m_desiredSize < m_bots.Count() + m_humans.Count() )
		{
			int index = 0;
			for ( ; index < m_bots.Count(); ++index )
			{
				CTerrorPlayer *bot = m_bots[index];
				if ( !bot->IsAlive() || bot->IsGhost() ) break;
				// Alive nonghost Tanks are ALWAYS protected, without a trace.
				if ( bot->GetTeamNumber() == TEAM_INFECTED && bot->GetZombieClass() == 8 ) continue;
				if ( !IsTerrorSpawnEntityVisibleToTeam( bot, TEAM_SURVIVOR, 0, 0.0f, NULL ) ) break;
			}
			if ( index == m_bots.Count() ) return;
			engine->ServerCommand( CFmtStr( "kickid %d team size exceeded\n", engine->GetPlayerUserId( m_bots[index]->edict() ) ) );
			m_bots.Remove( index );
		}
	}
private:
	int m_desiredSize;
	int m_team;
	int m_displacedTeam;
	CUtlVector< CTerrorPlayer * > m_bots;
	CUtlVector< CTerrorPlayer * > m_humans;
};

// S30C430 / S30E820. No manager means no enumeration and no team changes.
void OnSurvivorLimitChanged( IConVar *var, const char *oldValue, float oldFloat )
{
	ConVar *value = static_cast< ConVar * >( var );
	if ( !TheZombieManager ) return;
	EnsureTeamSize ensure( value->GetInt(), TEAM_SURVIVOR, TEAM_SPECTATOR );
	IPlayerFunctor &functor = ensure;
	ForEachPlayer( functor );
}
void OnMaxPlayerZombiesChanged( IConVar *var, const char *oldValue, float oldFloat )
{
	ConVar *value = static_cast< ConVar * >( var );
	if ( !TheZombieManager ) return;
	EnsureTeamSize ensure( value->GetInt(), TEAM_INFECTED, TEAM_SPECTATOR );
	IPlayerFunctor &functor = ensure;
	ForEachPlayer( functor );
}
