//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_PROP_MINIGUN_H
#define C_PROP_MINIGUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_base_mounted_weapon.h"

class CSoundPatch;

// prop_minigun_l4d1, not prop_minigun.
class C_PropMinigun : public C_BaseMountedWeapon
{
public:
	DECLARE_CLASS( C_PropMinigun, C_BaseMountedWeapon );
	DECLARE_CLIENTCLASS();

	C_PropMinigun();
	virtual ~C_PropMinigun();
	virtual void Precache() OVERRIDE;
	virtual void OnDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual void UpdateClientSideAnimation() OVERRIDE;
	virtual unsigned int ComputeClientSideAnimationFlags() OVERRIDE;
	virtual void StandardBlendingRules( CStudioHdr *pHdr, Vector pos[], Quaternion q[], float currentTime, int boneMask ) OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	virtual void GetUserPosition( Vector *pPosition ) OVERRIDE;

	virtual bool IsUsable( C_BaseEntity *pUser ) OVERRIDE;
	virtual const wchar_t *GetUseString( C_BaseEntity *pUser ) OVERRIDE;

private:
	enum SoundState
	{
		SOUND_IDLE = 0,
		SOUND_OVERHEATED,
		SOUND_SPIN_UP,
		SOUND_FIRING,
		SOUND_SPIN_DOWN,
		SOUND_SPIN_DOWN_OVERHEATED
	};

	void UpdateSound();
	void UpdateFiringEffects();
	void UpdateBarrel( float deltaTime );
	void FireEffects();
	void StartShells();
	void StopShells();

	// Retail preserves the sub-.033 remainder by writing the timer timestamp.
	// The target IntervalTimer has no timestamp setter.
	float m_flFireTimestamp;
	float m_flNextTracerTime;
	float m_flNextRicochetTime;
	float m_flNextImpactTime;
	float m_flBarrelSpeed;
	float m_flBarrelTargetSpeed;
	float m_flBarrelRotation;
	CSoundPatch *m_pSound;
	SoundState m_soundState;

	C_PropMinigun( const C_PropMinigun & );
};

#endif // C_PROP_MINIGUN_H
