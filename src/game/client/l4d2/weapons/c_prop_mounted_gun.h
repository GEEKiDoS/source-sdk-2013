//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_PROP_MOUNTED_GUN_H
#define C_PROP_MOUNTED_GUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_base_mounted_weapon.h"

// Both prop_minigun and prop_mounted_machine_gun create this class.
class C_PropMountedGun : public C_BaseMountedWeapon
{
public:
	DECLARE_CLASS( C_PropMountedGun, C_BaseMountedWeapon );
	DECLARE_CLIENTCLASS();

	C_PropMountedGun();
	virtual ~C_PropMountedGun();
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

	float GetHeatBlend() const;
	virtual bool IsUsable( C_BaseEntity *pUser ) OVERRIDE;
	virtual const wchar_t *GetUseString( C_BaseEntity *pUser ) OVERRIDE;

private:
	C_PropMountedGun( const C_PropMountedGun & );
};

#endif // C_PROP_MOUNTED_GUN_H
