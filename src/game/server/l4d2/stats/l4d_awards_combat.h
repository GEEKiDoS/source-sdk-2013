//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D_AWARDS_COMBAT_H
#define L4D_AWARDS_COMBAT_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/stats/l4d_awards.h"

class CAmbushAwardTemplate : public CAwardTemplate
{
public:
	explicit CAmbushAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void FinishMap();
	int m_kills;
	bool m_failedMap;
	bool m_startedInStartArea;
	bool m_died;
};

class CBoomerComboAwardTemplate : public CAwardTemplate
{
public:
	explicit CBoomerComboAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void Reset();
	float m_killTime;
	int m_kills;
	float m_awardTime;
};

class CGrenadeExpertAwardTemplate : public CAwardTemplate
{
public:
	explicit CGrenadeExpertAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void Reset();
	int m_pipeBombs;
	bool m_pipeHurtFriend;
	bool m_pipeHitEnemy;
	bool m_pipeMissed;
	int m_molotovs;
	bool m_molotovHurtFriend;
	bool m_molotovHitEnemy;
	bool m_molotovMissed;
	bool m_died;
	bool m_startedInStartArea;
};

class CGrenadierAwardTemplate : public CAwardTemplate
{
public:
	explicit CGrenadierAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	int m_kills;
};

class CHeadhunterAwardTemplate : public CAwardTemplate
{
public:
	explicit CHeadhunterAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void FinishMap();
	int m_kills;
	int m_headshots;
	bool m_failedMap;
	bool m_lowerTierMap;
	bool m_startedInStartArea;
	bool m_died;
};

class CHulkSlayerAwardTemplate : public CAwardTemplate
{
public:
	explicit CHulkSlayerAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	int m_damage[32];
};

class CHurdleAwardTemplate : public CAwardTemplate
{
public:
	explicit CHurdleAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	CountdownTimer m_cooldown;
};

class CItSurvivorAwardTemplate : public CAwardTemplate
{
public:
	explicit CItSurvivorAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void Reset();
	bool m_isIt;
	int m_kills;
	bool m_hurt;
};

class CMVPAwardTemplate : public CAwardTemplate
{
public:
	explicit CMVPAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CParryAwardTemplate : public CAwardTemplate
{
public:
	explicit CParryAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	CountdownTimer m_lungeShoveTimer;
};

class CProtectorAwardTemplate : public CAwardTemplate
{
public:
	explicit CProtectorAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	CountdownTimer m_cooldown;
};

class CSharpshooterAwardTemplate : public CAwardTemplate
{
public:
	explicit CSharpshooterAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void FinishMap();
	int m_shots;
	int m_hits;
	float m_hitTime;
	bool m_failedMap;
	bool m_lowerTierMap;
	bool m_startedInStartArea;
	bool m_died;
};

class CSkeetShootAwardTemplate : public CAwardTemplate
{
public:
	explicit CSkeetShootAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CSniperAwardTemplate : public CAwardTemplate
{
public:
	explicit CSniperAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void FinishMap();
	int m_kills;
	int m_longRangeKills;
	bool m_failedMap;
	bool m_lowerTierMap;
	bool m_startedInStartArea;
	bool m_died;
};

class CVomitStopAwardTemplate : public CAwardTemplate
{
public:
	explicit CVomitStopAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CWeaponExpertAwardTemplate : public CAwardTemplate
{
public:
	explicit CWeaponExpertAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	int m_weaponKills[7];
	int m_kills;
};

class CWhiplashAwardTemplate : public CAwardTemplate
{
public:
	explicit CWhiplashAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CWitchSlayerAwardTemplate : public CAwardTemplate
{
public:
	explicit CWitchSlayerAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void Reset();
	int m_damage;
	float m_firstDamageTime;
	bool m_pendingWitch;
	bool m_noMissedWitches;
	bool m_killedWitch;
	bool m_startedInStartArea;
	bool m_died;
};

#endif // L4D_AWARDS_COMBAT_H
