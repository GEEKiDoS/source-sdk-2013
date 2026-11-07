//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_CHARGER_H
#define L4D2_CHARGER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class ChargerIntention;
class ChargerLocomotion;
class PlayerBody;
class ZombieBotVision;

// S4311A0, type descriptor107ADC80 / vtable106E3CCC.
class Charger : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Charger, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Charger();
	virtual ~Charger();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 6; }
	virtual bool CanBeShoved( CBaseEntity *source ) const OVERRIDE;
	virtual const char *GetOverrideStepSound( const char *baseSoundName ) OVERRIDE;
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
	bool IsChargeBlocked( const Vector &start, const Vector &end ) const;
	bool IsWaitingInCover() const { return m_isWaitingInCover; }
	void SetWaitingInCover( bool waiting ) { m_isWaitingInCover = waiting; }
private:
	ChargerIntention *m_intention;
	ChargerLocomotion *m_locomotion;
	PlayerBody *m_body;
	ZombieBotVision *m_vision;
	bool m_isWaitingInCover; // Native ctor deliberately leaves this byte untouched.
	CUtlVector< EHANDLE > m_chargeBlockers; // Spawn appends; it does not clear.
};
Charger *CreateChargerBot( const char *name );
#endif // L4D2_CHARGER_H
