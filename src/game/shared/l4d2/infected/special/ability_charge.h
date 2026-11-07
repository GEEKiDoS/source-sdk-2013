//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_CHARGE_H
#define L4D2_ABILITY_CHARGE_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CCharge C_Charge
#endif
class CMoveData;
class CCharge : public CBaseAbility
{
public:
	DECLARE_CLASS( CCharge, CBaseAbility );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CCharge();
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool IsReadyToActivate() OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
	virtual int GetActivationButton() const OVERRIDE;
	virtual float GetMovementSpeed() const OVERRIDE;
	virtual float GetMaxSpeed() const OVERRIDE;
	virtual void StopActivation( float duration ) OVERRIDE;
	virtual void OnOwnerDamaged( const CTakeDamageInfo &info ) OVERRIDE;
	void OnPummelEnded();
	CTerrorPlayer *GetPotentialTarget() const { return m_hPotentialTarget.Get(); }
#ifdef CLIENT_DLL
	virtual float GetRechargeProgress() const OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
#else
	virtual bool OnPlayerMoveCollision( CBaseEntity *hit, const Vector &hitPosition, const Vector &direction, const trace_t &trace, CMoveData *move ) OVERRIDE;
private:
	void UpdatePotentialTarget();
	void ProbeAhead();
	void ImpactShock();
	void StaggerFromImpact();
	bool HasHitSurvivor() const;
#endif
private:
	void StartCharge();
	void EndCharge();
	CNetworkVar( float, m_chargeStartTime );
	CNetworkVar( bool, m_isCharging );
	CNetworkHandle( CTerrorPlayer, m_hPotentialTarget );
	Vector m_chargeStartPosition, m_lastProbePosition;
	CountdownTimer m_collisionTimer, m_probeTimer;
	bool m_hitSurvivor[8];
	bool m_hadVictim;
	Vector m_carryStartPosition;
};
#endif // L4D2_ABILITY_CHARGE_H
