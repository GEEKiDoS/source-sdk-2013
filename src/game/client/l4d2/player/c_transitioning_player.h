//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_TRANSITIONING_PLAYER_H
#define L4D2_C_TRANSITIONING_PLAYER_H
#ifdef _WIN32
#pragma once
#endif

#include "c_basecombatcharacter.h"
#include "l4d2/terror_shareddefs.h"
#include "tier1/utlvector.h"

class C_AddonProp;
class ITraceFilter;

class C_TransitioningPlayer : public C_BaseCombatCharacter
{
public:
	DECLARE_CLASS( C_TransitioningPlayer, C_BaseCombatCharacter );
	DECLARE_CLIENTCLASS();
	C_TransitioningPlayer();
	virtual ~C_TransitioningPlayer();
	virtual void ClientThink() OVERRIDE;
	virtual void NotifyShouldTransmit( ShouldTransmitState_t state ) OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual const char *GetIDString() OVERRIDE;
	virtual int GetHealth() const OVERRIDE { return m_iHealth; }

	const char *GetPlayerName() const { return m_playerName; }
	int GetUserID() const { return m_userID; }
	int GetScore() const { return m_score; }
	int GetDeaths() const { return m_deaths; }
	int GetHealthBuffer() const { return m_healthBuffer; }
	int GetLoadingProgress() const { return m_loadingProgress; }
	SurvivorCharacterType GetCharacter() const { return m_character; }
	bool IsTransitionAlive() const { return m_isAlive; }
	bool IsTransitionDying() const { return m_isDying; }
	bool IsIncapacitated() const { return m_isIncapacitated; }
	bool IsIT() const { return m_isIT; }
	Color GetTeamColor();

private:
	void ClearAddonModels();
	void CreateAddonModel( int weaponID );

	int m_userID;
	int m_score;
	int m_deaths;
	int m_healthBuffer;
	int m_loadingProgress;
	bool m_isAlive;
	bool m_isDying;
	bool m_isIncapacitated;
	bool m_isIT;
	char m_playerName[32];
	SurvivorCharacterType m_character;
	bool m_hasMolotov;
	bool m_hasGrenade;
	bool m_hasFirstAidKit;
	bool m_hasPainPills;
	int m_primaryWeaponID;
	int m_secondaryWeaponID;
	LocalFlexController_t m_blinkController;
	CUtlVector< CHandle<C_AddonProp> > m_addonModels;
};

const CUtlVector<C_TransitioningPlayer *> &GetTransitioningPlayers();
void ResetTransitioningPlayers();
void TraceTransitioningPlayers( const Vector &start, const Vector &end, unsigned int mask, ITraceFilter *filter, trace_t *trace );

#endif
