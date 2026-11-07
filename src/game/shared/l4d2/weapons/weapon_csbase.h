//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_CSBASE_H
#define L4D2_WEAPON_CSBASE_H
#ifdef _WIN32
#pragma once
#endif

#include "basecombatweapon_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/terror_shareddefs.h"

#ifdef CLIENT_DLL
#define CWeaponCSBase C_WeaponCSBase
#define CCSPlayer C_CSPlayer
#define CTerrorPlayer C_TerrorPlayer
#endif

int AliasToWeaponID( const char *name ); // S24EB00, first case-insensitive substring match.
const char *WeaponIDToAlias( int nWeaponID );
const char *WeaponIDToClassname( int nWeaponID ); // S24EB70, full classname domain.
class CCSPlayer;
class CTerrorPlayer;

class CWeaponCSBase : public CBaseCombatWeapon
{
public:
	DECLARE_CLASS( CWeaponCSBase, CBaseCombatWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CWeaponCSBase();
	virtual ~CWeaponCSBase();
	virtual void Precache() OVERRIDE;
	virtual bool IsPredicted() const OVERRIDE { return true; }
	virtual const unsigned char *GetEncryptionKey() OVERRIDE { return NULL; }
	virtual void ItemPostFrame() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual bool CanPerformSecondaryAttack() const OVERRIDE { return true; } // S40524D640/C399230DC0.
	virtual void UpdateShieldPostFrame(); // S40424EDA0/C3982317E0, fresh owner resolution.
	virtual bool ReloadPrimaryClip(); // S407250070/C401231AA0, distinct from Reload.
	virtual bool CanDeploy() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *switchingTo = NULL ) OVERRIDE;
	virtual bool DefaultDeploy( char *viewModel, char *weaponModel, int activity, char *animationExtension ) OVERRIDE;
	virtual bool SendWeaponAnim( int activity ) OVERRIDE;
	virtual const char *GetViewModel( int viewmodelindex = 0 ) const OVERRIDE;
	virtual double GetFireRate() OVERRIDE; // L4D2 native wide return; stock generic declaration remains float.
	virtual const Vector &GetBulletSpread() OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity activity ) { return activity; } // S38824F960/C3742316D0; separate from inherited generic ActivityOverride.
	virtual bool ShouldBlockPrimaryFire() OVERRIDE { return true; } // S24D600.
	virtual Activity GetDeployActivity() { return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_DRAW ); }
	virtual int GetWeaponID() const { return 0; }
	virtual bool IsAttacking() const { return false; } // S38724F950/C3732316C0.
	virtual bool HasNativePickupState() const { return false; } // S38424F940/C3712316B0; NOT force-pickup.
	virtual bool IsWeaponZoomed() const { return false; } // S39224ECA0/C378231700, independent native policy.
	virtual bool HasScope() const { return false; } // S24D580.
#ifdef CLIENT_DLL
	virtual bool HideViewModelWhenZoomed() const { return true; } // C397230D70; SG5523070D0 overrides false.
#endif
	virtual void ToggleZoom() {} // S24D5A0 genuine nullsub.
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity );
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity );
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity );
	virtual bool IsPerformingAction() const { return false; } // S401/C387.
	virtual const char *GetPlayerModel() const { return GetWorldModel(); }
	virtual bool CanBeDropped() const { return true; }
	virtual bool ShouldUseHandAttachmentForDrop() const { return true; }
	virtual float GetDropDistance() const { return 64.0f; }
	virtual bool ShouldMergeWithWeaponSpawn() const;
	virtual bool HasAnyUpgrades() { return false; } // S24D630.
	virtual void SetWeaponWorldModel( const char *model ); // S39924FE60/C3852327B0.
	virtual bool CanBeGivenTo( CTerrorPlayer *recipient );
	virtual void DefaultTouch( CBaseEntity *other ) OVERRIDE;
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	virtual bool ShouldRemoveOnRoundRestart() const;
	virtual bool KeyValue( const char *key, const char *value ) OVERRIDE;
	virtual bool IsForcePickup() const { return m_bForcePickup; } // S38624D560, server-only state.
	void SetForcePickup( bool force ) { m_bForcePickup = force; }
	virtual bool Reload() OVERRIDE;
	virtual bool DefaultReload( int clip1, int clip2, int activity ); // S38224F750 adds dispatch, base helper stays nonvirtual.
	virtual void OnPickedUp( CBaseCombatCharacter *newOwner ) OVERRIDE;
	virtual bool PhysicsSplash( const Vector &point, const Vector &normal, float rawSpeed, float scaledSpeed ) OVERRIDE;
	virtual void Drop( const Vector &velocity ) OVERRIDE;
	virtual void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE useType, float value ) OVERRIDE;
	virtual void GiveTo( CBaseEntity *other ) OVERRIDE;
	virtual bool IsRemoveable(); // CS-added1532; generic base stays nonvirtual.
	virtual CBaseEntity *Respawn() OVERRIDE;
	virtual void Materialize() OVERRIDE;
	void AttemptToMaterialize();
	void RemovalThink();
#else
	bool IsForcePickup() const { return false; } // Main-approved actual client absence; no field or setter.
	virtual bool IsAllowedToSwitch() OVERRIDE;
	virtual bool ShouldDrawWeapon() OVERRIDE;
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	virtual bool ShouldPredict() OVERRIDE;
	virtual int GetUseType( CBaseEntity *user ) OVERRIDE;
	virtual void DrawCrosshair() OVERRIDE;
	virtual void HideCrosshair();
	virtual int GetTerrorMuzzleAttachment() const;
	virtual int GetEjectBrassAttachment() const;
	virtual void EjectBrass();
	virtual void ProcessMuzzleFlashEvent() OVERRIDE;
	virtual float CalcViewmodelBob() OVERRIDE;
	virtual void AddViewmodelBob( CBaseViewModel *viewmodel, Vector &origin, QAngle &angles ) OVERRIDE;
#endif

	const CCSWeaponInfo &GetCSWpnData() const;
	virtual float GetMaxSpeed() const { return GetCSWpnData().m_flMaxSpeed; }
	const CTerrorWeaponInfo &GetTerrorWpnData() const;
	void SetExtraPrimaryAmmo( int ammo );
	int GetExtraPrimaryAmmo() const { return m_iExtraPrimaryAmmo; }
	CNetworkVar( int, m_iExtraPrimaryAmmo );
protected:
	bool PlayEmptySound(); // Genuine nonvirtual S24ECB0/C231710 false.
private:
	bool HasCSPrimaryAmmo(); // S24F7C0/C232220; not generic HasPrimaryAmmo.
protected:
	bool m_bDelayFire; // S5352/C2588, constructor true only.
	float m_flAccuracy; // Native S5356/C2592; no constructor write/wire/SAVE/PRED.
private:
	float m_flDecreaseShotsFired; // S5360/C2596, one native decay/Deploy clock; constructor omission.
	float m_flOwnerPickupLockUntil; // S5368/C2604, real ctor store on both sides.
	CCSPlayer *m_pDroppedWeaponOwner; // S5372/C2608, RAW pointer, not transfer EHANDLE.
	int m_iNumPrimaryAmmoClips; // S5376/C2584, real ammo KeyValue owner.
#ifndef CLIENT_DLL
	bool m_bForcePickup; // S5380 only.
#else
	bool m_bCSAutoReloadAttempted; // C2520 only, ctor false / successful ReloadPrimaryClip true.
	int m_iCrosshairAlpha; // C2576, Deploy80; no constructor store.
#endif
};

#endif // L4D2_WEAPON_CSBASE_H
