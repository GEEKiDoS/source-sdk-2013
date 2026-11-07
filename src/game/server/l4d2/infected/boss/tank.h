//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TANK_H
#define L4D2_TANK_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class TankIntention;
class TankLocomotion;
class PlayerBody;
class ZombieBotVision;
class Tank : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Tank, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Tank();
	virtual ~Tank();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 8; } // S:46A0B0, true CTerrorPlayer virtual337 override
	virtual int OnTakeDamage_Alive( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
	void BeginControlOffer(); // S:469EE0
	void EndControlOffer(); // S:469F20
private:
	PlayerBody *m_body; // native17200
	TankIntention *m_intention;
	TankLocomotion *m_locomotion;
	ZombieBotVision *m_vision;
	CUtlVector< int > m_attackerUserIDs; //17216, not entity handles
	int m_damageTypes; //17236
	bool m_killEventSent; //17240
};
Tank *CreateTankBot( const char *name ); // S:2FDAD0, genuine NextBot player factory
#endif
