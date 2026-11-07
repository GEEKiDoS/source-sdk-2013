//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/boss/tank.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

ConVar director_tank_lottery_entry_time( "director_tank_lottery_entry_time", "0.1", 0x00004004, "Time it takes for entry into the tank lottery" );
ConVar director_tank_lottery_selection_time( "director_tank_lottery_selection_time", "4", 0x00004004, "Time it takes for selection in the tank lottery" );
ConVar director_tank_offer_debug( "director_tank_offer_debug", "0", 0x00004004 );
ConVar tank_raffle_debug( "tank_raffle_debug", "0", 0x00006004, "Fill the tank raffle with dummy values for debugging" );

namespace
{
	bool TankPositiveTime( float time )
	{
		const unsigned int bits = FloatBits( time );
		return bits > 0u && bits <= 0x7f800000u;
	}
	bool TankDeadlineElapsed( float deadline )
	{
		// Ordered-positive admission first; original x87 FCOMIP/JB rejects
		// unordered now, rather than treating NaN as a completed deadline.
		if ( !TankPositiveTime( deadline ) ) return false;
		const unsigned int now = FloatBits( gpGlobals->curtime );
		return ( now & 0x80000000u ) == 0u && now <= 0x7f800000u && now >= FloatBits( deadline );
	}
	void StartTankControlTimer( CountdownTimer &timer, float duration )
	{
		// Native deadline store precedes duration; preserve separately widened
		// x87 addition followed by the actual float spill.
		timer.m_timestamp = (float)( (double)gpGlobals->curtime + (double)duration );
		timer.m_duration = duration;
	}
	double TankTimerProgress( const CountdownTimer &timer )
	{
		if ( !TankPositiveTime( timer.m_timestamp ) ) return 0.0;
		const float remaining = (float)( (double)timer.m_timestamp - (double)gpGlobals->curtime );
		const float ratio = _mm_cvtss_f32( _mm_div_ss( _mm_set_ss( remaining ), _mm_set_ss( timer.m_duration ) ) );
		const unsigned int bits = FloatBits( ratio ), magnitude = bits & 0x7fffffffu;
		if ( magnitude > 0x7f800000u ) return (double)ratio; // Native ordered clamps retain ratio NaN.
		if ( ( bits & 0x80000000u ) && magnitude != 0u ) return 0.0;
		if ( !( bits & 0x80000000u ) && bits > 0x3f800000u ) return 1.0;
		return (double)ratio; // Native retains negative zero.
	}
	bool HasLivePlayerEdict( CTerrorPlayer *player )
	{
		return player && player->edict() && ENTINDEX( player->edict() ) != 0;
	}
	bool IsTankLotteryCandidate( CTerrorPlayer *player )
	{
		return player && static_cast< const CTerrorPlayer * >( player )->IsConnected()
			&& player->GetTeamNumber() == TEAM_INFECTED && !player->IsBot()
			&& ( !player->IsAlive() || player->GetZombieClass() != 8 );
	}
	void ReplaceChosenPlayersCurrentZombie( CTerrorPlayer *player )
	{
		if ( player->GetZombieClass() != 8 && player->IsAlive() && !player->IsGhost() ) player->ReplaceWithBot( false );
	}
	struct TankFrustrationCheckpointScan
	{
		bool found;
		TankFrustrationCheckpointScan() : found( false ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			if ( player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) return true;
			TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
			if ( !area || !area->HasSpawnAttributes( 0x880 ) ) return true;
			CTerrorCheckpoint *transition = TheTerrorNavMesh()->GetTransitionCheckpoint();
			if ( transition && transition->GetAreas().Find( area ) != transition->GetAreas().InvalidIndex() ) return true;
			found = true;
			return false;
		}
	};
}

bool CDirector::IsTankFrustrationSuppressed()
{
	// Native262390 ASM: WORD functor=0x0100,25F3B0(&functor),return BYTE0.
	// Hex-Rays loses the byref argument and falsely prints a constantfalse.
	TankFrustrationCheckpointScan scan;
	ForEachPlayer( scan );
	return scan.found;
}
double CDirector::GetTankOfferLotteryProgress() const { return TankTimerProgress( m_TankOfferLotteryTimer ); }
double CDirector::GetTankControlTransferProgress() const { return TankTimerProgress( m_TankControlTransferTimer ); }
void CDirector::ClearTankOffer()
{
	// Native25D580 intentionally leaves a stale/unresolved handle unchanged.
	CBaseEntity *entity = m_hPendingTankOffer.Get();
	if ( entity )
	{
		Tank *tank = dynamic_cast< Tank * >( entity );
		if ( tank ) tank->EndControlOffer();
		m_hPendingTankOffer = NULL;
	}
}
void CDirector::PrepareTankOffer( CTerrorPlayer *player, bool force )
{
	ClearTankOffer();
	if ( force )
	{
		Tank *tank = dynamic_cast< Tank * >( player );
		if ( tank ) tank->BeginControlOffer();
	}
	m_hPendingTankOffer = player;
}
void CDirector::OfferTank( CTerrorPlayer *player, bool force )
{
	// Native COMISS/JA rejects ONLY ordered-positive mission deadline. NaN is
	// admitted; do not replace with deadline<=0. Global finale/state read is real.
	if ( !player || TankPositiveTime( m_MissionEndTimer.m_timestamp ) ) return;
	if ( TheDirector->m_pScenario->GetFinaleType() != 5 && TheDirector->m_nGameState == 3 ) return;
	int humans = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *candidate = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( HasLivePlayerEdict( candidate ) && candidate->GetTeamNumber() == TEAM_INFECTED && !candidate->IsBot() ) ++humans;
	}
	PrepareTankOffer( player, force );
	if ( humans > 1 || tank_raffle_debug.GetBool() )
	{
		StartTankControlTimer( m_TankOfferLotteryTimer, director_tank_lottery_entry_time.GetFloat() );
		Msg( "Tank offer: Starting the lottery\n" );
	}
	else if ( humans == 1 )
	{
		StartTankControlTimer( m_TankOfferLotteryTimer, 0.01f );
		Msg( "Tank offer: Going to the only PZ\n" );
	}
	else
	{
		Msg( "Tank offer: making a bot, and giving him to nobody\n" );
		ClearTankOffer();
	}
}
void CDirector::UpdateTankOffer()
{
	if ( TankPositiveTime( m_MissionEndTimer.m_timestamp ) ) return; // Native also admits NaN here.
	if ( AreBossesProhibited() || IsInCommentaryMode() ) { ClearTankOffer(); return; }
	if ( TankDeadlineElapsed( m_TankOfferLotteryTimer.m_timestamp ) )
	{
		m_TankOfferLotteryTimer.Invalidate();
		StartTankControlTimer( m_TankControlTransferTimer, director_tank_lottery_selection_time.GetFloat() );
		CUtlVector< int > tickets;
		int total = 0, candidates = 0;
		if ( director_tank_offer_debug.GetBool() ) DevMsg( "TANK OFFER: Looking for players\n" );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			tickets.AddToTail( 0 ); // Native appends a zero for EVERY client slot.
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !IsTankLotteryCandidate( player ) ) continue;
			const int score = player->GetTotalSurvivorDamage(); // Sole canonical12444, no second tickets counter.
			total = (int)( (unsigned int)total + (unsigned int)score );
			++candidates;
			tickets[i-1] = score;
			if ( director_tank_offer_debug.GetBool() ) DevMsg( "TANK OFFER: Player %s has %d tickets\n", player->GetPlayerName(), score );
		}
		if ( candidates == 0 ) { ClearTankOffer(); return; } // Selection timer/winner omission intentionally retained.
		m_nTankOfferWinner = 0;
		if ( total <= 0 )
		{
			int choice = RandomInt( 1, candidates );
			if ( director_tank_offer_debug.GetBool() ) DevMsg( "TANK OFFER: No tickets, choosing randomly\n" );
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				// Native fallback uses live edict, NOT IsConnected. Do not replace
				// this loop with the stricter canonical connected-player collector.
				if ( HasLivePlayerEdict( player ) && player->GetTeamNumber() == TEAM_INFECTED && !player->IsBot()
					&& ( !player->IsAlive() || player->GetZombieClass() != 8 ) && --choice <= 0 )
				{ m_nTankOfferWinner = i; break; }
			}
		}
		else
		{
			int choice = RandomInt( 1, total );
			if ( director_tank_offer_debug.GetBool() ) DevMsg( "TANK OFFER: Winning ticket = %d\n", choice );
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				choice = (int)( (unsigned int)choice - (unsigned int)tickets[i-1] );
				if ( choice <= 0 ) { m_nTankOfferWinner = i; break; }
			}
		}
		if ( director_tank_offer_debug.GetBool() )
		{
			CTerrorPlayer *winner = ToTerrorPlayer( UTIL_PlayerByIndex( m_nTankOfferWinner ) );
			DevMsg( "TANK OFFER: Winning Player = %s\n", winner->GetPlayerName() ); // Native has NO missing-winner guard.
		}
	}
	if ( !TankDeadlineElapsed( m_TankControlTransferTimer.m_timestamp ) ) return;
	m_TankControlTransferTimer.Invalidate();
	CTerrorPlayer *offered = dynamic_cast< CTerrorPlayer * >( m_hPendingTankOffer.Get() );
	if ( offered ) ClearTankOffer(); // Native releases Tank stasis/damage before all eligibility reads.
	if ( !m_nTankOfferWinner ) return;
	CTerrorPlayer *winner = ToTerrorPlayer( UTIL_PlayerByIndex( m_nTankOfferWinner ) );
	if ( !winner || winner->GetTeamNumber() != TEAM_INFECTED ) return; // Native retains winner on this path.
	if ( offered && offered->GetTeamNumber() == TEAM_INFECTED && offered->IsAlive() && offered->GetZombieClass() == 8 && offered->IsBot() )
	{
		ReplaceChosenPlayersCurrentZombie( winner );
		m_nTankControlPassCount = (int)( (unsigned int)m_nTankControlPassCount + 1u );
		winner->TakeOverTankBot( offered ); // Native32B400, M5/Saved existing snapshot owner.
		m_nTankOfferWinner = 0;
		return;
	}
	bool transferred = false;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *oldTank = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( oldTank && static_cast< const CTerrorPlayer * >( oldTank )->IsConnected() && !oldTank->IsIncapacitated()
			&& oldTank->GetTeamNumber() == TEAM_INFECTED && oldTank->IsAlive() && oldTank->GetZombieClass() == 8 )
		{
			ReplaceChosenPlayersCurrentZombie( winner );
			m_nTankControlPassCount = (int)( (unsigned int)m_nTankControlPassCount + 1u );
			TheZombieManager->TransferTank( oldTank, winner ); // Native35AD30, genuine M5 owner.
			m_nTankOfferWinner = 0;
			transferred = true;
			// Original scans to the end: no invented break or single-Tank repair.
		}
	}
	if ( !transferred ) m_nTankOfferWinner = 0;
}
