//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_CARRIED_ITEM_H
#define L4D2_WEAPON_CARRIED_ITEM_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/base_backpack_item.h"

#ifdef CLIENT_DLL
#define CCarriedProp C_CarriedProp
#define CCarriedPropWeapon C_CarriedPropWeapon
#endif

// The original RTTI hierarchy is CCarriedProp : CBaseBackpackItem. Its
// concrete network tables deliberately inherit DT_BaseBackpackItem directly.
class CCarriedProp : public CBaseBackpackItem
{
public:
	DECLARE_CLASS( CCarriedProp, CBaseBackpackItem );

	CCarriedProp();
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual bool StartAction( int mode, int buttons ) OVERRIDE;
	virtual CBaseEntity *FindActionTarget( CTerrorPlayer *pPlayer, int mode ) OVERRIDE;
	virtual bool CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) OVERRIDE;
	virtual float GetActionDuration() const OVERRIDE { return 0.0f; }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 0; }
#endif
	virtual int GetActionButtons( int mode ) const OVERRIDE;
	virtual bool CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE { return ACT_INVALID; }
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetDrawActivity() OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;

	virtual bool IsCarriedProp() const OVERRIDE { return true; }
	virtual const char *GetCarriedPropModel() const = 0;
	virtual bool ShouldUseHandAttachmentForDrop() const OVERRIDE { return false; }
#ifndef CLIENT_DLL
	virtual void Equip( CBaseCombatCharacter *pOwner ) OVERRIDE;
	virtual void SetWeaponSpawn( CBaseEntity *pSpawn );
	CBaseEntity *GetWeaponSpawn() const { return m_hWeaponSpawn.Get(); }
	virtual bool DropCarriedItem( bool bThrow );
#endif

protected:
	static Activity CarriedActivity( const char *pName );
	CHandle< CBaseEntity > m_hUseTarget;
#ifndef CLIENT_DLL
	CBaseEntity *CreatePhysicsProp();
	bool DropToPhysicsPropInternal( bool bThrow );
	CHandle< CBaseEntity > m_hWeaponSpawn;
	int m_nSavedGlowType;
#endif
};

// Gas and cola retain their original weapon entity on drop. The other
// carried objects convert back into model-owned physics props.
class CCarriedPropWeapon : public CCarriedProp
{
public:
	DECLARE_CLASS( CCarriedPropWeapon, CCarriedProp );
#ifndef CLIENT_DLL
	virtual void Equip( CBaseCombatCharacter *pOwner ) OVERRIDE;
	virtual bool DropCarriedItem( bool bThrow ) OVERRIDE;
protected:
	int m_nDroppedWeaponGlowType;
#endif
};

#endif // L4D2_WEAPON_CARRIED_ITEM_H
