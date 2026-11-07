//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_SPIT_H
#define L4D2_ABILITY_SPIT_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CSpitAbility C_SpitAbility
#endif

extern ConVar z_spit_interval;
extern ConVar z_spit_range;
extern ConVar z_spit_latency;
#ifndef CLIENT_DLL
extern ConVar z_spit_velocity;
#endif

class CSpitAbility : public CBaseAbility
{
public:
	DECLARE_CLASS( CSpitAbility, CBaseAbility );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CSpitAbility();
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual int GetActivationButton() const OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
	virtual bool CanActivateOnGround() const OVERRIDE;
	virtual void StopActivation( float duration ) OVERRIDE;
#ifdef CLIENT_DLL
	virtual bool IsReadyToActivate() OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
#else
	Vector GetSpitPosition() const;
#endif
private:
	CNetworkVar( bool, m_bHasBeenActivated );
	CountdownTimer m_spitTimer;
};
#endif // L4D2_ABILITY_SPIT_H
