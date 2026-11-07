//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_WEAPON_H
#define L4D2_TERROR_WEAPON_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/weapon_csbase.h"
#include "util_shared.h"
#include "utlvector.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/weapons/response_rules_weapon_source.h"

#ifdef CLIENT_DLL
#define CTerrorPlayer C_TerrorPlayer
#define CTerrorWeapon C_TerrorWeapon
#define CTerrorGun C_TerrorGun
#endif

#if defined( L4D2 ) && defined( CLIENT_DLL )
EXTERN_RECV_TABLE( DT_CountdownTimer );
#endif

class CTerrorPlayer;
class CTerrorGun;

#if defined( L4D2 ) && !defined( CLIENT_DLL )
EXTERN_SEND_TABLE( DT_CountdownTimer );
#endif

int GetTerrorWeaponGlowType( CBaseEntity *user, int weaponID, int clip, int maxReserve, int *ownedWeaponID, bool forceCanUse );

enum TerrorWeaponID
{
	TERROR_WEAPON_NONE = 0,
	TERROR_WEAPON_PISTOL = 1,
	TERROR_WEAPON_SMG = 2,
	TERROR_WEAPON_PUMPSHOTGUN = 3,
	TERROR_WEAPON_AUTOSHOTGUN = 4,
	TERROR_WEAPON_RIFLE = 5,
	TERROR_WEAPON_HUNTING_RIFLE = 6,
	TERROR_WEAPON_SMG_SILENCED = 7,
	TERROR_WEAPON_SHOTGUN_CHROME = 8,
	TERROR_WEAPON_RIFLE_DESERT = 9,
	TERROR_WEAPON_SNIPER_MILITARY = 10,
	TERROR_WEAPON_SHOTGUN_SPAS = 11,
	TERROR_WEAPON_FIRST_AID_KIT = 12,
	TERROR_WEAPON_MOLOTOV = 13,
	TERROR_WEAPON_PIPE_BOMB = 14,
	TERROR_WEAPON_PAIN_PILLS = 15,
	TERROR_WEAPON_GASCAN = 16,
	TERROR_WEAPON_PROPANETANK = 17,
	TERROR_WEAPON_OXYGENTANK = 18,
	TERROR_WEAPON_MELEE = 19,
	TERROR_WEAPON_CHAINSAW = 20,
	TERROR_WEAPON_GRENADE_LAUNCHER = 21,
	TERROR_WEAPON_AMMO_PACK = 22,
	TERROR_WEAPON_ADRENALINE = 23,
	TERROR_WEAPON_DEFIBRILLATOR = 24,
	TERROR_WEAPON_VOMITJAR = 25,
	TERROR_WEAPON_RIFLE_AK47 = 26,
	TERROR_WEAPON_GNOME = 27,
	TERROR_WEAPON_COLA_BOTTLES = 28,
	TERROR_WEAPON_FIREWORKCRATE = 29,
	TERROR_WEAPON_UPGRADEPACK_INCENDIARY = 30,
	TERROR_WEAPON_UPGRADEPACK_EXPLOSIVE = 31,
	TERROR_WEAPON_PISTOL_MAGNUM = 32,
	TERROR_WEAPON_SMG_MP5 = 33,
	TERROR_WEAPON_RIFLE_SG552 = 34,
	TERROR_WEAPON_SNIPER_AWP = 35,
	TERROR_WEAPON_SNIPER_SCOUT = 36,
	TERROR_WEAPON_RIFLE_M60 = 37,
	TERROR_WEAPON_TANK_CLAW = 38,
	TERROR_WEAPON_HUNTER_CLAW = 39,
	TERROR_WEAPON_CHARGER_CLAW = 40,
	TERROR_WEAPON_BOOMER_CLAW = 41,
	TERROR_WEAPON_SMOKER_CLAW = 42,
	TERROR_WEAPON_SPITTER_CLAW = 43,
	TERROR_WEAPON_JOCKEY_CLAW = 44,
	TERROR_WEAPON_MACHINEGUN = 45,
	TERROR_WEAPON_VOMIT = 46,
	TERROR_WEAPON_SPLAT = 47,
	TERROR_WEAPON_POUNCE = 48,
	TERROR_WEAPON_LOUNGE = 49,
	TERROR_WEAPON_PULL = 50,
	TERROR_WEAPON_CHOKE = 51,
	TERROR_WEAPON_ROCK = 52,
	TERROR_WEAPON_PHYSICS = 53,
	TERROR_WEAPON_AMMO = 54,
	TERROR_WEAPON_UPGRADE_ITEM = 55
};

class CTerrorWeapon : public CWeaponCSBase, public IResponseRulesWeaponSource
{
public:
	DECLARE_CLASS( CTerrorWeapon, CWeaponCSBase );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CTerrorWeapon();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual void ItemBusyFrame() OVERRIDE;
	virtual bool CanDeploy() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual bool SendWeaponAnim( int iActivity ) OVERRIDE;
	virtual double GetReloadAnimationScale() OVERRIDE;
	virtual double GetDeployAnimationScale() OVERRIDE;
	virtual Activity GetDeployActivity() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual const char *GetViewModel( int viewmodelindex = 0 ) const OVERRIDE;
	virtual bool IsFullyAutomatic() const { return false; }
	virtual bool IsCarriedProp() const { return false; }
	virtual bool IsDualWielding() const { return false; } // S24D710.
	virtual bool CanPerformUseAction() const { return true; } // S24D720 genuine base policy.
	virtual bool IsAttacking() const OVERRIDE;
	virtual bool HasNativePickupState() const OVERRIDE { return m_helpingHandState != 0; } // S3D0840/C2FC450.
	virtual bool CanUseSecondaryShove() const { return true; } // Native1660/24D650; distinct from CS405.
	virtual bool CanUseSecondaryShoveWhileReloading() const { return true; } // S41624D660/C409230DF0, all45S/56C real descendants.
	virtual bool UseCSBaseItemPostFrame() const { return true; } // Native1680/24D700.
	virtual Activity TranslatePlayerActivity( Activity activity ) OVERRIDE;
	virtual bool CanFidget();
	// Genuine common-weapon upgrade policies; guns own the actual bit vector.
	virtual bool AddUpgrade( int upgrade ) { return false; }
	virtual void RemoveUpgrade( int upgrade ) {}
	virtual void RemoveAllUpgrades() {}
	virtual bool HasUpgrade( int upgrade ) const { return false; }
	CTerrorPlayer *GetTerrorPlayerOwner() const;
	void PreventFireUntilButtonRelease();
	void CancelSwing(); // Native3ECB20, count snapshot before clear; no final sweep.
	void MakeWeaponBloody();
	virtual const char *GetResponseRulesName() const OVERRIDE;
	virtual void OnOwnerStunned( float duration );
	bool HandlePrimaryAttackUse();
	void SetHelpingHandState( int state );
	void SuppressHelpingHand( float duration = 0.0f );
	virtual bool CanDisplayHelpingHand() const { return true; }
	virtual bool Reload() OVERRIDE;
	virtual Activity TranslateViewModelLayerActivity( Activity activity );
	virtual Activity TranslateViewModelActivity( Activity activity );
	virtual int GetViewModelLayer( Activity activity );
	virtual bool CanBeUsedBy( CTerrorPlayer *player );
#ifdef CLIENT_DLL
	double GetBloodyWeaponBlend() const;
	virtual bool IsAllowedToSwitch() OVERRIDE;
	virtual bool CanBeGivenTo( CTerrorPlayer *recipient ) OVERRIDE { return false; } // C230DD0, all56 real Terror descendants.
	virtual void OverrideMouseInput( float *x, float *y ) OVERRIDE; // C310570, genuine common client357.
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual bool Simulate() OVERRIDE; // Actual C30F1D0, not a Think producer.
	virtual bool GetStencilState( ShaderStencilState_t &state ) OVERRIDE;
#endif
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
	DECLARE_ENT_SCRIPTDESC();
	void ScriptSetClip1( int clip );
	void ScriptSetClip2( int clip );
	virtual void Drop( const Vector &velocity ) OVERRIDE;
	virtual void DefaultTouch( CBaseEntity *other ) OVERRIDE;
	virtual CBaseEntity *GetPreviousOwner() const OVERRIDE;
	virtual CBaseEntity *GetGiveTarget() const;
	void GiveToPlayer( CTerrorPlayer *giver, CTerrorPlayer *recipient );
	virtual bool CanBeGivenTo( CTerrorPlayer *recipient ) OVERRIDE;
	void GiveThink();
	EHANDLE m_hPreviousOwner;
	EHANDLE m_hGiveTarget;
	CountdownTimer m_ownerTransferTimer;
	void SetForcedDropAttacker( CBaseEntity *attacker ) { m_hForcedDropAttacker = attacker; }
	CBaseEntity *GetForcedDropAttacker() const { return m_hForcedDropAttacker.Get(); }
	EHANDLE m_hForcedDropAttacker; // Native5500 has the genuine INVALID constructor store.
#endif
	void SetPickedUpOnTransition() { m_bPickedUpOnTransition = true; }
	CNetworkVar( bool, m_bPickedUpOnTransition );
	CNetworkVar( int, m_DroppedByInfectedGender );

	CNetworkVarEmbedded( CountdownTimer, m_attackTimer );
	CNetworkVarEmbedded( CountdownTimer, m_swingTimer );
	CNetworkVar( int, m_nUpgradedPrimaryAmmoLoaded );
	CNetworkVar( float, m_reloadQueuedStartTime );
	CNetworkVar( bool, m_releasedAltFireButton );
	CNetworkVar( bool, m_releasedFireButton );
	CNetworkVar( bool, m_isHoldingFireButton );
	CNetworkVar( bool, m_isHoldingAltFireButton );
	CNetworkVar( int, m_iBloodyWeaponLevel ); // Native5420/client2652, sole weapon-level owner.

protected:
	void UpdateHelpingHand();
	CBaseEntity *FindHelpingHandTarget();
	void SendHelpingHandAnim( Activity activity );
	CNetworkVarEmbedded( IntervalTimer, m_helpingHandTimer );
	CNetworkVar( int, m_helpingHandState );
	CNetworkHandle( CBaseEntity, m_helpingHandTarget );
	CNetworkVar( float, m_helpingHandExtendDuration );
	CNetworkVarEmbedded( CountdownTimer, m_helpingHandSuppressionTimer );
	CNetworkVarEmbedded( IntervalTimer, m_helpingHandTargetTimer );
	bool m_bSendingHelpingHandAnim;
	float m_reloadStartActivityTime; // Native5448 cutoff; actual gun/shotgun Reload writers, no invented reset.
	virtual void Swing( float interval, float duration, float range ); // S3F06F0/C311620.
	virtual Vector GetSwingDirection() const;
	virtual float GetSwingStartYaw() const;
	virtual float GetSwingEndYaw() const;
	virtual float GetSwingStartPitch() const;
	virtual float GetSwingEndPitch() const;
	virtual void UpdateReloadInterruption(); // Native3F14F0; NOT CheckReload04E0C0.
#ifdef CLIENT_DLL
	bool TraceSwing( const Vector &direction ); // Genuine client312420 producer.
	int m_iLaserSightAttachment; // C3296, written only by actual C30F1E0 model callback; no constructor reset.
#endif
	virtual void UpdateSwing();
	virtual void OnSwing();
	virtual void UpdateReloadQueue();
	virtual void OnSwingEnd( bool bHitAny );
	virtual bool OnSwingHit( trace_t &trace, const Vector &direction, bool bNewHit );
	CUtlVectorFixed< EHANDLE, 128 > m_swingAffectedEntities;
	bool m_bSwingInProgress;
	float m_flSwingRange;
	Vector m_vecLastSwingDirection;
	int m_nSwingFrames;
	int m_nShoveTargetCount;
	bool m_bStompTarget;
	EHANDLE m_hStompTarget;
#ifdef CLIENT_DLL
	CountdownTimer m_incapacitatedDizzyTimer; // C3264: real member ctor, deliberately NO Spawn reset.
	float m_incapacitatedDizzyMouseX; // C3276, actual local input state, not transport/prediction.
	float m_incapacitatedDizzyMouseY; // C3280.
#endif
	CountdownTimer m_hitSoundTimer;
	friend class CTerrorPlayer;
};

#endif // L4D2_TERROR_WEAPON_H
