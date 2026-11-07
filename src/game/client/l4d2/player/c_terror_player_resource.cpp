//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "tier0/memdbgon.h"
IMPLEMENT_CLIENTCLASS_DT( C_TerrorPlayerResource, DT_TerrorPlayerResource, CTerrorPlayerResource )
	RecvPropArray3( RECVINFO_ARRAY( m_maxHealth ), RecvPropInt( RECVINFO( m_maxHealth[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_primaryWeapon ), RecvPropInt( RECVINFO( m_primaryWeapon[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_grenade ), RecvPropInt( RECVINFO( m_grenade[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_firstAidSlot ), RecvPropInt( RECVINFO( m_firstAidSlot[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_pillsSlot ), RecvPropInt( RECVINFO( m_pillsSlot[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_isGhost ), RecvPropInt( RECVINFO( m_isGhost[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_isIncapacitated ), RecvPropInt( RECVINFO( m_isIncapacitated[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_wantsToPlay ), RecvPropInt( RECVINFO( m_wantsToPlay[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_zombieClass ), RecvPropInt( RECVINFO( m_zombieClass[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_tankTickets ), RecvPropInt( RECVINFO( m_tankTickets[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_listenServerHost ), RecvPropInt( RECVINFO( m_listenServerHost[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_teamSwitchRule ), RecvPropInt( RECVINFO( m_teamSwitchRule[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_flSurvivalRecordTime ), RecvPropFloat( RECVINFO( m_flSurvivalRecordTime[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_nSurvivalTopMedal ), RecvPropInt( RECVINFO( m_nSurvivalTopMedal[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_flBecomeGhostAt ), RecvPropFloat( RECVINFO( m_flBecomeGhostAt[0] ) ) ),
	RecvPropFloat( RECVINFO( m_tankLotteryEntryRatio ) ),
	RecvPropFloat( RECVINFO( m_tankLotterySelectionRatio ) ),
	RecvPropInt( RECVINFO( m_pendingTankPlayerIndex ) ),
	RecvPropInt( RECVINFO( m_isFinale ) ),
	RecvPropInt( RECVINFO( m_isSurvivorTeamReadyTime ) ),
	RecvPropInt( RECVINFO( m_survivorTeamReadyTime ) ),
	RecvPropInt( RECVINFO( m_nRoundSetupTimeRemaining ) ),
	RecvPropInt( RECVINFO( m_tempoState ) ),
	RecvPropInt( RECVINFO( m_hasAnySurvivorLeftSafeArea ) ),
	RecvPropInt( RECVINFO( m_isTeamFrozen ) ),
	RecvPropInt( RECVINFO( m_missionDuration ) ),
	RecvPropInt( RECVINFO( m_missionWipes ) ),
	RecvPropFloat( RECVINFO( m_starRating ) ),
	RecvPropInt( RECVINFO( m_sharedRandomSeed ), 0, RecvProxy_Int32ToInt32 ),
END_RECV_TABLE()
C_TerrorPlayerResource::C_TerrorPlayerResource()
{
	// Whole C261CE0, not just the previous five-field Director bridge.
	Q_memset( m_maxHealth, 0, sizeof( m_maxHealth ) );
	Q_memset( m_primaryWeapon, 0, sizeof( m_primaryWeapon ) );
	Q_memset( m_grenade, 0, sizeof( m_grenade ) );
	Q_memset( m_firstAidSlot, 0, sizeof( m_firstAidSlot ) );
	Q_memset( m_pillsSlot, 0, sizeof( m_pillsSlot ) );
	Q_memset( m_isGhost, 0, sizeof( m_isGhost ) );
	Q_memset( m_isIncapacitated, 0, sizeof( m_isIncapacitated ) );
	Q_memset( m_wantsToPlay, 0, sizeof( m_wantsToPlay ) );
	Q_memset( m_zombieClass, 0, sizeof( m_zombieClass ) );
	Q_memset( m_tankTickets, 0, sizeof( m_tankTickets ) );
	Q_memset( m_listenServerHost, 0, sizeof( m_listenServerHost ) );
	Q_memset( m_flBecomeGhostAt, 0, sizeof( m_flBecomeGhostAt ) );
	for ( int i = 0; i < MAX_PLAYERS_ARRAY_SAFE; ++i )
	{
		m_flSurvivalRecordTime[i] = -1.0f;
		m_nSurvivalTopMedal[i] = -1;
		// Native omits pre-receive TeamSwitchRule initialization. Minimal-skip
		// inactive0, replaced by the actual receive table, no fake producer.
		m_teamSwitchRule[i] = 0;
	}
	m_tankLotteryEntryRatio = 0.0f;
	m_tankLotterySelectionRatio = 0.0f;
	m_pendingTankPlayerIndex = 0;
	m_isFinale = false;
	m_isSurvivorTeamReadyTime = false;
	m_survivorTeamReadyTime = 0;
	m_nRoundSetupTimeRemaining = 0;
	m_tempoState = 0;
	m_hasAnySurvivorLeftSafeArea = false;
	m_isTeamFrozen = false;
	m_missionDuration = 0;
	m_missionWipes = 0;
	m_starRating = 3.0f;
	m_sharedRandomSeed = 0.0f;
}
int C_TerrorPlayerResource::GetMaxHealth( int index )
{
	return IsConnected( index ) ? m_maxHealth[index] : 0;
}
