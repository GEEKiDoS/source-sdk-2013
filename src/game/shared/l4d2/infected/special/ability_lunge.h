//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_LUNGE_H
#define L4D2_ABILITY_LUNGE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CLunge C_Lunge
#endif
class CMoveData;

// Hunter. The retail Jockey ability is CLeap, not CLunge.
class CLunge : public CBaseAbility
{
public:
	DECLARE_CLASS( CLunge, CBaseAbility );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CLunge();
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual bool IsReadyToActivate() OVERRIDE { return true; }
	virtual int GetActivationButton() const OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
	virtual Vector *GetQueuedMovement( bool markChanged ) OVERRIDE;
	virtual void OnTouch( CBaseEntity *other ) OVERRIDE;
	virtual void OnOwnerLanded() OVERRIDE;
	virtual void OnOwnerDamaged( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void StopActivation( float duration ) OVERRIDE;
	virtual bool OnPlayerMoveCollision( CBaseEntity *other, const Vector &position,
		const Vector &direction, const trace_t &trace, CMoveData *move ) OVERRIDE;
	bool IsWallKickAvailable() const;
#ifdef CLIENT_DLL
	virtual float GetRechargeProgress() const OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
#endif

private:
	CNetworkVarEmbedded( CountdownTimer, m_lungeAgainTimer );
	CNetworkVar( float, m_lungeStartTime );
	CNetworkVector( m_queuedLunge );
	CNetworkVar( bool, m_isLunging );
	bool m_interrupted;
	CountdownTimer m_attackCooldown;
	bool m_touchedPlayer;
	bool m_damagedDoor;
};
#endif // L4D2_ABILITY_LUNGE_H
