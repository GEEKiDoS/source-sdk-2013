//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_LEAP_H
#define L4D2_ABILITY_LEAP_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CLeap C_Leap
#endif

// Jockey. The retail Hunter ability is CLunge, not CLeap.
class CLeap : public CBaseAbility
{
public:
	DECLARE_CLASS( CLeap, CBaseAbility );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CLeap();
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual bool IsReadyToActivate() OVERRIDE { return true; }
	virtual int GetActivationButton() const OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
	virtual Vector *GetQueuedMovement( bool markChanged ) OVERRIDE;
	virtual void OnTouch( CBaseEntity *other ) OVERRIDE;
	virtual void OnOwnerDamaged( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void StopActivation( float duration ) OVERRIDE;
	bool IsWallKickAvailable() const;
	void SetLeapDirection( const Vector &direction ) { m_leapDirection = direction; }
#ifdef CLIENT_DLL
	virtual float GetRechargeProgress() const OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
#endif

private:
	CNetworkVarEmbedded( CountdownTimer, m_leapAgainTimer );
	CNetworkVar( float, m_leapStartTime );
	CNetworkVector( m_queuedLeap );
	CNetworkVar( bool, m_isLeaping );
	bool m_interrupted;
	CountdownTimer m_attackCooldown;
	bool m_touchedPlayer;
	bool m_damagedDoor;
	Vector m_leapDirection;
	CountdownTimer m_attachTimer;
};
#endif // L4D2_ABILITY_LEAP_H
