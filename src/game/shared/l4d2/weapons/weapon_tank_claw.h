//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_TANK_CLAW_H
#define L4D2_WEAPON_TANK_CLAW_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/weapons/weapon_claw.h"
#ifdef CLIENT_DLL
#define CTankClaw C_TankClaw
#endif
// Native RTTI: CTankClaw -> CClaw -> CTerrorWeapon -> CWeaponCSBase.
class CTankClaw : public CClaw
{
public:
	DECLARE_CLASS( CTankClaw, CClaw );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	CTankClaw();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_TANK_CLAW; } // S:3E4250/C:309700
	virtual bool IsFullyAutomatic() const OVERRIDE { return false; } // S:3E4270/C:309720
	virtual bool CanPerformUseAction() const OVERRIDE; // S:3E35B0/C:3091C0, actual deadline
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE; // genuine empty S:3E3370/C:308EB0
	virtual void ItemPostFrame() OVERRIDE;
	CNetworkVar( bool, m_isLowAttack ); // native6124/client3324
	CNetworkVarEmbedded( CountdownTimer, m_lowAttackDurationTimer ); //6128/client3328
protected:
	virtual void OnSwing() OVERRIDE;
	virtual void OnSwingEnd( bool hitAny ) OVERRIDE;
	virtual bool OnSwingHit( trace_t &trace, const Vector &direction, bool newHit ) OVERRIDE;
	virtual void UpdateSwing() OVERRIDE;
	virtual Vector GetSwingDirection() const OVERRIDE; // native434/C427
	virtual float GetSwingStartYaw() const OVERRIDE;
	virtual float GetSwingEndYaw() const OVERRIDE;
	virtual Vector GetClawDamageForce( CBaseEntity *target, const Vector &direction ) const OVERRIDE;
#ifndef CLIENT_DLL
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
	// New retail Tank-only449/450/451; NOT the inherited three-argument435.
	virtual void TankSwing( float interval, float duration, float range, CTerrorPlayer *target );
#ifndef CLIENT_DLL
	virtual void OnTankHitPlayer( CTerrorPlayer *victim, bool newlyIncapacitated );
#endif
	virtual void OnWindupFinished();
private:
	void GroundPound(); // S:3E45D0/C:30A1D0
	void SweepHand( const Vector &from, const Vector &to ); // S:3E6660/C:30AC20
	Vector GetSwingOrigin() const; // retail weapon284/3E3480, absent SDK virtual
	float GetPhysicsPropForceScale( CBaseEntity *target ) const;
	CountdownTimer m_windupTimer; //6104/client3304, intentionally not predicted/networked
	float m_swingDuration, m_swingStartPitch; //6116/6120, no ctor initialization
#ifndef CLIENT_DLL
	CUtlVector< EHANDLE > m_lowAttackVictims; //6140, absent from actual client class
#endif
	Vector m_lastHandPosition; //6160/client3340, ctor deliberately omits
	bool m_hitWorld, m_hitSurvivor; //6172/6173, reset by OnSwing and actual impact
};
#endif
