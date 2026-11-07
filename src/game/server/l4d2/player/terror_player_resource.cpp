//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terror_player_resource.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "vstdlib/random.h"
#include <limits.h>
#include "tier0/memdbgon.h"
LINK_ENTITY_TO_CLASS( terror_player_manager, CTerrorPlayerResource );
IMPLEMENT_SERVERCLASS_ST( CTerrorPlayerResource, DT_TerrorPlayerResource )
	SendPropArray3( SENDINFO_ARRAY3( m_maxHealth ), SendPropInt( SENDINFO_ARRAY( m_maxHealth ), 16, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_primaryWeapon ), SendPropInt( SENDINFO_ARRAY( m_primaryWeapon ), 8, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_grenade ), SendPropInt( SENDINFO_ARRAY( m_grenade ), 8, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_firstAidSlot ), SendPropInt( SENDINFO_ARRAY( m_firstAidSlot ), 8, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_pillsSlot ), SendPropInt( SENDINFO_ARRAY( m_pillsSlot ), 8, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_isGhost ), SendPropInt( SENDINFO_ARRAY( m_isGhost ), 1, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_isIncapacitated ), SendPropInt( SENDINFO_ARRAY( m_isIncapacitated ), 1, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_wantsToPlay ), SendPropInt( SENDINFO_ARRAY( m_wantsToPlay ), 1, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_zombieClass ), SendPropInt( SENDINFO_ARRAY( m_zombieClass ), 8, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_tankTickets ), SendPropInt( SENDINFO_ARRAY( m_tankTickets ), 16, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_listenServerHost ), SendPropInt( SENDINFO_ARRAY( m_listenServerHost ), 1, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_TeamSwitchRule ), SendPropInt( SENDINFO_ARRAY( m_TeamSwitchRule ), 2, SPROP_UNSIGNED ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_flSurvivalRecordTime ), SendPropFloat( SENDINFO_ARRAY( m_flSurvivalRecordTime ), 32, SPROP_NOSCALE ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_nSurvivalTopMedal ), SendPropInt( SENDINFO_ARRAY( m_nSurvivalTopMedal ), 8, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_flBecomeGhostAt ), SendPropFloat( SENDINFO_ARRAY( m_flBecomeGhostAt ), 32, SPROP_NOSCALE ) ),
	SendPropFloat( SENDINFO( m_tankLotteryEntryRatio ), 32, 0 ),
	SendPropFloat( SENDINFO( m_tankLotterySelectionRatio ), 32, 0 ),
	SendPropInt( SENDINFO( m_pendingTankPlayerIndex ), 32, 0 ),
	SendPropInt( SENDINFO( m_isFinale ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_isSurvivorTeamReadyTime ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_survivorTeamReadyTime ), 32, 0 ),
	SendPropInt( SENDINFO( m_nRoundSetupTimeRemaining ), 32, 0 ),
	SendPropInt( SENDINFO( m_tempoState ), 32, 0 ),
	SendPropInt( SENDINFO( m_hasAnySurvivorLeftSafeArea ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_isTeamFrozen ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_missionDuration ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionWipes ), 32, 0 ),
	SendPropInt( SENDINFO( m_sharedRandomSeed ), 32, 0, SendProxy_Int32ToInt32 ),
END_SEND_TABLE()
CTerrorPlayerResource::~CTerrorPlayerResource()
{
	if ( g_pPlayerResource == this ) g_pPlayerResource = NULL;
}
void CTerrorPlayerResource::Spawn()
{
	// Whole S343F30. MaxHealth, TeamSwitchRule and SharedRandomSeed are NOT reset.
	for ( int i = 0; i < MAX_PLAYERS_ARRAY_SAFE; ++i )
	{
		m_primaryWeapon.Set( i, 0 ); m_grenade.Set( i, 0 ); m_firstAidSlot.Set( i, 0 ); m_pillsSlot.Set( i, 0 );
		m_isGhost.Set( i, false ); m_isIncapacitated.Set( i, false ); m_wantsToPlay.Set( i, false );
		m_zombieClass.Set( i, -1 ); m_tankTickets.Set( i, -1 ); m_listenServerHost.Set( i, false );
		m_flSurvivalRecordTime.Set( i, -1.0f ); m_nSurvivalTopMedal.Set( i, -1 ); m_flBecomeGhostAt.Set( i, 0.0f );
	}
	m_isFinale = false; m_tankLotteryEntryRatio = 0.0f; m_tankLotterySelectionRatio = 0.0f; m_pendingTankPlayerIndex = 0;
	m_isSurvivorTeamReadyTime = false; m_survivorTeamReadyTime = 0; m_nRoundSetupTimeRemaining = 0; m_tempoState = 0;
	m_hasAnySurvivorLeftSafeArea = TheDirector->HasAnySurvivorLeftSafeArea();
	m_isTeamFrozen = TheDirector->GetSessionManager()->IsTeamFrozen();
	m_missionDuration = 0; m_missionWipes = 0;
	BaseClass::Spawn();
}
void CTerrorPlayerResource::SetBecomeGhostAt( int index, float deadline )
{
	// Native S343310 guards1..32; network array Set compares raw bits, as3432B0.
	if ( index >= 1 && index <= MAX_PLAYERS ) m_flBecomeGhostAt.Set( index, deadline );
}
void CTerrorPlayerResource::SetTankTickets( int index, int tickets )
{
	// S343250 has no extra index guard; callers supply the actual player slot.
	m_tankTickets.Set( index, tickets );
}
static int TerrorResourceWeaponID( CTerrorPlayer *player, int slot )
{
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( player->Weapon_GetSlot( slot ) );
	return weapon ? weapon->GetWeaponID() : 0;
}
void CTerrorPlayerResource::UpdatePlayerData()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsConnected() || player->IsDisconnecting() ) continue;
		m_maxHealth.Set( i, MAX( 0, player->GetMaxHealth() ) );
		const int team = player->GetTeamNumber();
		// Inherited score is native12440/12500, never FragCount or killScore12432.
		m_iScore.Set( i, team == TEAM_INFECTED ? player->m_missionSurvivorDamage.Get() : team == TEAM_SURVIVOR ? player->m_score : 0 );
		m_primaryWeapon.Set( i, TerrorResourceWeaponID( player, 0 ) );
		m_TeamSwitchRule.Set( i, player->GetTeamSwitchRule() );
		m_grenade.Set( i, TerrorResourceWeaponID( player, 2 ) );
		m_pillsSlot.Set( i, TerrorResourceWeaponID( player, 4 ) );
		m_firstAidSlot.Set( i, TerrorResourceWeaponID( player, 3 ) );
		m_isGhost.Set( i, player->IsGhost() ); m_isIncapacitated.Set( i, player->IsIncapacitated() ); m_wantsToPlay.Set( i, player->m_wantsToPlay );
		// Main's single supported split-screen slot0 projects original host owner to itself.
		m_listenServerHost.Set( i, !engine->IsDedicatedServer() && player->entindex() == 1 );
		m_flSurvivalRecordTime.Set( i, player->IsFakeClient() ? -1.0f : player->m_flBestSurvivalTime );
		m_nSurvivalTopMedal.Set( i, player->IsFakeClient() ? -1 : player->m_nSurvivalMedal );
		m_zombieClass.Set( i, team == TEAM_INFECTED ? player->GetZombieClass() : -1 );
		// Test PRIOR frame's winner, before refreshing the global after the loop.
		if ( m_pendingTankPlayerIndex.Get() == 0 && team == TEAM_INFECTED )
			SetTankTickets( i, player->IsFakeClient() ? -1 : player->GetTotalSurvivorDamage() );
	}
	m_isFinale = TheDirector->IsFinale();
	m_tankLotteryEntryRatio = static_cast<float>( TheDirector->GetTankOfferLotteryProgress() );
	m_tankLotterySelectionRatio = static_cast<float>( TheDirector->GetTankControlTransferProgress() );
	m_pendingTankPlayerIndex = TheDirector->GetTankControlWinner();
	m_isSurvivorTeamReadyTime = TheDirector->GetSessionManager()->IsStartAreaExitBlocked();
	m_survivorTeamReadyTime = TheDirector->GetSessionManager()->GetSurvivorReadyTime();
	m_nRoundSetupTimeRemaining = TheDirector->GetScavengeMode()->GetReadyCountdownSeconds();
	m_tempoState = TheDirector->GetIntensityState();
	m_hasAnySurvivorLeftSafeArea = TheDirector->HasAnySurvivorLeftSafeArea();
	m_isTeamFrozen = TheDirector->GetSessionManager()->IsTeamFrozen();
	m_missionDuration = TheDirector->GetMissionDuration(); m_missionWipes = TheDirector->GetMissionLossCount();
	const float seed = static_cast<float>( RandomInt( INT_MIN, INT_MAX ) );
	if ( m_sharedRandomSeed != seed ) m_sharedRandomSeed = seed;
	// Apply native lower resource seam in existing_owner_seams.txt: lower native
	//189450 never overwrites m_iScore with SDK FragCount at this phase.
	BaseClass::UpdatePlayerData();
}

BEGIN_DATADESC( CTerrorPlayerResource )
END_DATADESC()
