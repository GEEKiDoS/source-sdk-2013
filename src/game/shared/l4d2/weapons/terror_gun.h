//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_GUN_H
#define L4D2_TERROR_GUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_weapon.h"
#ifdef CLIENT_DLL
#include "particles_new.h"
#endif


class CTerrorGun : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CTerrorGun, CTerrorWeapon );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CTerrorGun();
	virtual void Spawn() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual bool SendWeaponAnim( int iActivity ) OVERRIDE;
	virtual void ItemBusyFrame() OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual bool Reload() OVERRIDE;
	virtual void FinishReload() OVERRIDE;
	virtual void AbortReload() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual double GetFireRate() OVERRIDE;
	virtual bool IsFullyAutomatic() const OVERRIDE { return true; }
	virtual bool HasScope() const OVERRIDE { return false; } // Native3E6F40.
	virtual void ToggleZoom() OVERRIDE; // Native3E78C0, actual player FOV/speed/sound/event owner.
	virtual bool UseCSBaseItemPostFrame() const OVERRIDE { return false; } // Native3C1270.
	virtual int GetZoomFOV() const { return 30; } // S3E7150 genuine base policy.
	virtual int GetMaxClip1() const OVERRIDE;
	virtual float GetMaxAutoAimDeflection() OVERRIDE;
	virtual float WeaponAutoAimScale() OVERRIDE;
	virtual float GetMaxAutoAimRange() const;
	bool IsZoomed() const;
	virtual bool ReloadsSingly() const { return false; }
	virtual float GetMaxMovementSpread() const;
	virtual float GetMinStandingSpread() const;
	virtual float GetMinDuckingSpread() const;
	void UpdateSpread();
	void IncreaseSpread();
#ifdef CLIENT_DLL
	virtual void OnPreDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;
	virtual bool OnFireEvent( C_BaseViewModel *pViewModel, const Vector &origin, const QAngle &angles, int nEvent, const char *pOptions ) OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	// Original C:0x231A50 is null: shot events, not weapon parity, own TP flash.
	virtual void ProcessMuzzleFlashEvent() OVERRIDE {}
	virtual void ClientThink() OVERRIDE;
	virtual bool Simulate() OVERRIDE;
	virtual void UpdateOnRemove() OVERRIDE;
	virtual void DrawCrosshair() OVERRIDE;
	virtual void EjectBrass() OVERRIDE;
	virtual int GetEjectBrassAttachment() const OVERRIDE;
	void NotifyTerrorMuzzleFlash();
	virtual int GetTerrorMuzzleAttachment() const OVERRIDE;
#endif
	float GetCurrentSpread() const { return m_flCurrentSpread; }
	virtual float GetMinInAirSpread() const;
	virtual bool IsDualWielding() const OVERRIDE { return m_isDualWielding; } // S3C1320.
	virtual bool AddUpgrade( int upgrade ) OVERRIDE;
	virtual void RemoveUpgrade( int upgrade ) OVERRIDE;
	virtual void RemoveAllUpgrades() OVERRIDE;
	virtual bool HasUpgrade( int upgrade ) const OVERRIDE;
	virtual bool HasAnyUpgrades() OVERRIDE;
	virtual CTerrorGun *GetTerrorGun() OVERRIDE { return this; }
	virtual bool CanDualWield() const { return false; }
#ifndef CLIENT_DLL
	virtual void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE useType, float value ) OVERRIDE;
	virtual bool AcquireDualWeapons();
	virtual bool RemoveDualWeapons( bool force = false );
	virtual void UpdateDualState();
#endif

	CNetworkVar( bool, m_isDualWielding );
	CNetworkVar( bool, m_hasDualWeapons );
	CNetworkVar( bool, m_inInitialPickup );
	CNetworkVar( bool, m_reloadFromEmpty );
	CNetworkVar( int, m_partialReloadStage );
	CNetworkVar( int, m_upgradeBitVec );

protected:
	void ApplyUpgradeAmmo( int upgrade );
	void RemoveUpgradeAmmo();
	virtual void OnSwing() OVERRIDE;
	void FireBullet();
	void ApplyRecoil( CTerrorPlayer *player );
	bool ShouldUnzoom() const;
#ifdef CLIENT_DLL
	void UpdateWorldLaserSight();
	int m_iMuzzleAttachment[3];
	int m_iShellAttachment[3];
#endif
	float m_flCurrentSpread;
#ifdef CLIENT_DLL
	IntervalTimer m_muzzleFlashTimer;
	int m_nMuzzleFlashShots;
	bool m_bMuzzleSmoke;
	CountdownTimer m_gunPresentationTimer; // Native C3360: actual member lifetime, no inferred producer/reset.
	HPARTICLEFFECT m_laserSightWorldmodelEffect;
	bool m_oldIsDualWielding; // C3372: OnPreDataChanged snapshots before the base hook.
#else
	void DelayGunAttack( float duration );
	int m_nStoredSingleClip;
#endif
};

void EmitTerrorWeaponReloadSound( CTerrorPlayer *player, CWeaponCSBase *weapon, WeaponSound_t sound );

#endif // L4D2_TERROR_GUN_H
