//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_ABILITY_H
#define L4D2_BASE_ABILITY_H
#ifdef _WIN32
#pragma once
#endif

#include "util_shared.h"
#ifdef CLIENT_DLL
#include "c_baseentity.h"
#define CBaseAbility C_BaseAbility
#define CTerrorPlayer C_TerrorPlayer
#else
#include "baseentity.h"
#endif

class CTerrorPlayer;
class CTakeDamageInfo;
class CMoveData;

// Native CBaseAbility / C_BaseAbility. Ability entities are not weapons.
class CBaseAbility : public CBaseEntity
{
public:
	DECLARE_CLASS( CBaseAbility, CBaseEntity );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	CBaseAbility();
	CTerrorPlayer *GetOwner() const;
#ifndef CLIENT_DLL
	virtual void RebindOwner( CTerrorPlayer *owner );
#endif
	virtual void SetOwner( CTerrorPlayer *owner );
	virtual bool CanActivate();
	virtual bool ActivateAbility();
	virtual void Update();
	virtual bool IsReadyToActivate();
	virtual void SuppressActivation( float duration );
	virtual double GetActivationTimerRemaining() const;
	virtual int GetActivationButton() const;
	virtual bool TryActivate();
	virtual void OnButtonReleased();
	virtual bool IsActive() const;
	virtual bool CanActivateOnGround() const;
	virtual Vector *GetQueuedMovement( bool markChanged );
	virtual void OnTouch( CBaseEntity *other );
	virtual void OnOwnerJumped();
	virtual void OnOwnerLanded();
	virtual float GetMovementSpeed() const;
	virtual float GetMaxSpeed() const;
	virtual float GetFrictionMultiplier() const;
	virtual bool CanJump() const;
	virtual bool OnPlayerMoveCollision( CBaseEntity *hit, const Vector &hitPosition,
		const Vector &direction, const trace_t &trace, CMoveData *move );
	virtual void OnOwnerDamaged( const CTakeDamageInfo &info );
	virtual void StopActivation( float duration );
	virtual void OnRemove();

	void SetRechargeTime( float recharge, float elapsed = 0.0f );
	bool IsActivationSuppressed() const;
	bool HasBeenUsed() const { return m_hasBeenUsed; }
	float GetRechargeDuration() const;
#ifdef CLIENT_DLL
	float GetActivationSuppressionTimestamp() const
	{
		return m_activationSupressedTimer.m_timestamp.Get();
	}
	virtual float GetRechargeProgress() const;
	virtual bool ShouldPredict() OVERRIDE;
	virtual const char *GetCooldownBackground() const;
	virtual const char *GetCooldownForeground() const;
#else
	static CBaseAbility *CreateForPlayer( CTerrorPlayer *owner );
	virtual void RemoveAbility();
	virtual void DebugVictim( CTerrorPlayer *victim, const char *format, ... ) {} // S360630 genuine cdecl RET.
	virtual int UpdateTransmitState() OVERRIDE;
	virtual int ShouldTransmit( const CCheckTransmitInfo *info ) OVERRIDE;
	virtual void UpdateOnRemove() OVERRIDE;
#endif

protected:
	CNetworkVarEmbedded( CountdownTimer, m_activationSupressedTimer );
	CNetworkHandle( CTerrorPlayer, m_owner );
	CNetworkVarEmbedded( CountdownTimer, m_nextActivationTimer );
	CNetworkVar( bool, m_hasBeenUsed );
};

#endif // L4D2_BASE_ABILITY_H
