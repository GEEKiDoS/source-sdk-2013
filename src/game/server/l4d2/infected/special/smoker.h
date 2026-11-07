//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SMOKER_H
#define L4D2_SMOKER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class SmokerIntention;
class SmokerLocomotion;
class PlayerBody;
class ZombieBotVision;

// Native constructor S4536B0, type descriptor107ADC98 / vtable106E924C.
class Smoker : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Smoker, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Smoker();
	virtual ~Smoker();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 1; }
	virtual bool IsAbleToBreak( const CBaseEntity *entity ) const OVERRIDE;
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
private:
	SmokerIntention *m_intention;
	SmokerLocomotion *m_locomotion;
	PlayerBody *m_body;
	ZombieBotVision *m_vision;
};
Smoker *CreateSmokerBot( const char *name );
#endif // L4D2_SMOKER_H
