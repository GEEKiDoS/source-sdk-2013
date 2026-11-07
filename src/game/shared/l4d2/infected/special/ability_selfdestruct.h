//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_SELFDESTRUCT_H
#define L4D2_ABILITY_SELFDESTRUCT_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CSelfDestruct C_SelfDestruct
#endif

class CSelfDestruct : public CBaseAbility
{
public:
	DECLARE_CLASS( CSelfDestruct, CBaseAbility );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CSelfDestruct();
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual int GetActivationButton() const OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
	virtual bool CanActivateOnGround() const OVERRIDE;
#ifdef CLIENT_DLL
	virtual float GetRechargeProgress() const OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
#endif
private:
	CNetworkVar( bool, m_bHasBeenActivated );
	CountdownTimer m_detonateTimer;
};
#endif // L4D2_ABILITY_SELFDESTRUCT_H
