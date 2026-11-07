//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TRANSITIONING_PLAYER_H
#define TRANSITIONING_PLAYER_H
#ifdef _WIN32
#pragma once
#endif
#include "basecombatcharacter.h"
#include "GameEventListener.h"
#include "l4d2/terror_shareddefs.h"
#include "tier1/utlvector.h"

// S3545C0 directly constructs CombatCharacter plus the event-listener base.
// Its native network ancestry deliberately remains DT_BaseFlex.
class CTransitioningPlayer : public CBaseCombatCharacter, public CGameEventListener
{
public:
	DECLARE_CLASS( CTransitioningPlayer, CBaseCombatCharacter );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	CTransitioningPlayer();
	virtual ~CTransitioningPlayer();
	virtual bool IsAlive() OVERRIDE { return m_isAlive; }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	const char *GetPlayerName() const { return m_playerName; }
	int GetUserID() const { return m_userID; }
	bool IsIT() const { return m_isIT; }
	void Initialize( const char *name, int score, int deaths, int userID, int team,
		int health, int healthBuffer, bool alive, bool dying, bool incapacitated,
		bool isIT, SurvivorCharacterType character );
	void SetWeapons( int primary, int secondary, bool molotov, bool grenade, bool firstAidKit, bool painPills );
	void TransitionThink();
	static const CUtlVector< CTransitioningPlayer * > &GetTransitioningPlayers();
	static void ResetTransitioningPlayerRegistry();
	static void RemoveByUserID( int userID );

private:
	CNetworkVar( int, m_userID );
	CNetworkVar( int, m_score );
	CNetworkVar( int, m_deaths );
	CNetworkVar( int, m_healthBuffer );
	CNetworkVar( int, m_loadingProgress );
	CNetworkVar( bool, m_isAlive );
	CNetworkVar( bool, m_isDying );
	CNetworkVar( bool, m_isIncapacitated );
	CNetworkVar( bool, m_isIT );
	CNetworkString( m_playerName, 32 );
	CNetworkVar( int, m_character );
	CNetworkVar( bool, m_hasMolotov );
	CNetworkVar( bool, m_hasGrenade );
	CNetworkVar( bool, m_hasFirstAidKit );
	CNetworkVar( bool, m_hasPainPills );
	CNetworkVar( int, m_primaryWeaponID );
	CNetworkVar( int, m_secondaryWeaponID );
};

// Retail IVEngineServer+76 supplies this independently of an edict/player.
// Main F10 capability decision: target SDK has no per-client loading-progress
// query; the sole bridge reports0, without a signon-state percentage mapping.
int TerrorEngine_GetLoadingProgressForUserID( int userID );
bool TerrorEngine_IsUserIDInServer( int userID );
#endif

