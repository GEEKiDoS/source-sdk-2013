//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef PLAYER_REPORTER_H
#define PLAYER_REPORTER_H
#ifdef _WIN32
#pragma once
#endif

#include "GameEventListener.h"
#include "igamesystem.h"
#include "tier1/utllinkedlist.h"
#include "tier1/utlvector.h"

class CTerrorPlayer;
class CAwardTemplate;
class KeyValues;

// Semantic representation of the original PlayerReporter record (S2BBDA0).
// Native offset 4 is a process-lifetime player serial, not a Steam account ID.
struct PlayerReport_t
{
	PlayerReport_t( int userID, int playerID, int team );
	~PlayerReport_t();
	void ResetAwards( CTerrorPlayer *player );
	int DamageTaken() const;
	int Accuracy() const;
	int HeadshotAccuracy() const;
	const char *GetName() const; // Native2BB080, actual current userID lookup, not stored history.
	const char *FormatVictims( const char *prefix ) const;
	const char *FormatInjuries( const char *prefix ) const;
	const char *FormatDeath( const char *prefix ) const;
	const char *FormatMedic( const char *prefix ) const;
	const char *FormatSkill( const char *prefix ) const;
	void PrintTo( CTerrorPlayer *recipient, const char *unusedTitle ) const; // Native2BB470.
	int userID;
	int playerID;
	bool alive;
	int team;
	bool enteredStartArea;
	bool transitioned;
	int zombieKills[9];
	int survivorKills;
	int awards[149];
	int damageTaken[9];
	int selfDamage;
	int friendlyDamage;
	int medkitsUsed;
	int medkitsShared;
	int pillsUsed;
	int adrenalineUsed;
	int defibrillatorsUsed;
	int pipebombsUsed;
	int molotovsUsed;
	int boomerBilesUsed;
	int shotsFired;
	int shotsHit;
	int headshots;
	int meleeKills;
	int revives;
	int incaps;
	int deaths;
	CUtlVector< CAwardTemplate * > awardTemplates;
	int awardsTeam;
	bool pendingHit;
	bool pendingHeadshot;
};

// Actual RTTI PlayerReporter, vtable S687DEC. This is not an achievement backend.
class PlayerReporter : public CGameEventListener, public CAutoGameSystem
{
public:
	PlayerReporter();
	virtual bool Init() OVERRIDE;
	virtual void Shutdown() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	PlayerReport_t *GetPlayerReport( CTerrorPlayer *player );
	void RemovePlayerReport( CTerrorPlayer *player );
	void OnPlayerTeamChanged( CTerrorPlayer *player );
	void OnBulletGroupFinished( CTerrorPlayer *player );
	void Clear();
	void PrintReport( CTerrorPlayer *recipient, const char *who, const char *category, const char *title ); // Native2BB640.
private:
	CUtlLinkedList< PlayerReport_t * > m_players;
};

PlayerReporter &GetPlayerReporter();

#endif // PLAYER_REPORTER_H
