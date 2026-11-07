//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_THROW_H
#define L4D2_ABILITY_THROW_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CThrow C_Throw
#else
class CTankRock;
#endif

class CThrow : public CBaseAbility
{
public:
	DECLARE_CLASS( CThrow, CBaseAbility );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	CThrow();
	virtual void Precache() OVERRIDE;
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual bool IsReadyToActivate() OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
	virtual void Update() OVERRIDE;
#ifndef CLIENT_DLL
	virtual void StopActivation( float duration ) OVERRIDE;
#endif
#ifdef CLIENT_DLL
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
#endif
private:
	void ClearThrow();
	CountdownTimer m_throwTimer;
	CountdownTimer m_viewmodelAttackTimer;
#ifndef CLIENT_DLL
	bool m_waitingForThrowAnimation;
	CountdownTimer m_createRockTimer;
	CountdownTimer m_releaseRockTimer;
	CHandle< CTankRock > m_rock;
	void StartThrowAnimationTimers();
	void ReleaseRock();
#endif
};
#endif // L4D2_ABILITY_THROW_H
