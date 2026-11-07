//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_CLAW_H
#define L4D2_WEAPON_CLAW_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/weapons/terror_weapon.h"
#ifdef CLIENT_DLL
#define CClaw C_Claw
#define CHunterClaw C_HunterClaw
#endif

class CClaw : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CClaw, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
#ifdef CLIENT_DLL
	virtual void DrawCrosshair() OVERRIDE;
#endif
protected:
	virtual void OnSwing() OVERRIDE;
	virtual void OnSwingEnd( bool hitAny ) OVERRIDE;
	virtual bool OnSwingHit( trace_t &trace, const Vector &direction, bool newHit ) OVERRIDE;
	virtual Vector GetClawDamageForce( CBaseEntity *target, const Vector &direction ) const;
#ifndef CLIENT_DLL
	virtual float GetClawDamage( CTerrorPlayer *victim ) const;
	virtual void OnClawHitPlayer( CTerrorPlayer *victim );
#endif
};

class CHunterClaw : public CClaw
{
public:
	DECLARE_CLASS( CHunterClaw, CClaw );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_HUNTER_CLAW; }
	virtual void Spawn() OVERRIDE;
	virtual void OnOwnerStunned( float duration ) OVERRIDE;
#ifndef CLIENT_DLL
	void OnPounceHit();
#endif
protected:
	virtual void Swing( float interval, float duration, float range ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
private:
	// S6104/C3304: plain local timer, no wire/save/prediction descriptor.
	CountdownTimer m_pounceHitTimer;
};
#endif // L4D2_WEAPON_CLAW_H
