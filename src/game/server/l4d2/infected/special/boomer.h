//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOOMER_H
#define L4D2_BOOMER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class BoomerIntention;
class BoomerLocomotion;
class PlayerBody;
class ZombieBotVision;
class Boomer : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Boomer, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Boomer();
	virtual ~Boomer();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 2; }
	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void SpawnTraceBlood( float damage, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace ) OVERRIDE;
	virtual bool CanBeShoved( CBaseEntity *pusher ) const OVERRIDE;
	virtual bool IsAbleToBreak( const CBaseEntity *entity ) const OVERRIDE;
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
	bool ShouldHide() const;
private:
	PlayerBody *m_body;
	BoomerIntention *m_intention;
	BoomerLocomotion *m_locomotion;
	ZombieBotVision *m_vision;
	CountdownTimer m_leakTimer;
};
Boomer *CreateBoomerBot( const char *name );
#endif // L4D2_BOOMER_H
