//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_MELEE_WEAPON_H
#define L4D2_TERROR_MELEE_WEAPON_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"

#ifdef CLIENT_DLL
#define CTerrorMeleeWeapon C_TerrorMeleeWeapon
#endif

class CTerrorMeleeWeapon : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CTerrorMeleeWeapon, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	CTerrorMeleeWeapon();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual bool CanUseSecondaryShove() const OVERRIDE; // Native3EA360, actual secondary-script count.
	virtual void ItemPostFrame() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_MELEE; }
	virtual bool IsFullyAutomatic() const OVERRIDE { return true; }
	virtual const char *GetViewModel( int viewmodelindex = 0 ) const OVERRIDE;
	virtual const char *GetWorldModel() const OVERRIDE;
	virtual const char *GetShootSound( int iIndex ) const OVERRIDE;
	virtual const char *GetResponseRulesName() const OVERRIDE;
	virtual Activity GetDeployActivity() OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity baseAct ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
#ifdef CLIENT_DLL
	virtual const CHudTexture *GetSpriteActive() const OVERRIDE;
	virtual const CHudTexture *GetSpriteInactive() const OVERRIDE { return GetSpriteActive(); }
#endif

	void SetMeleeWeaponInfo( MeleeWeaponInfoHandle_t hInfo );
#ifndef CLIENT_DLL
	void SetMeleeScriptName( const char *pszName );
#endif
	MeleeWeaponInfoHandle_t GetMeleeWeaponInfoHandle() const { return m_hMeleeWeaponInfo; }
	const CMeleeWeaponInfo *GetMeleeInfo() const;
	const char *GetMeleeWeaponScriptName() const;
	const MeleeAttackInfo_t *GetCurrentMeleeAttack() const;
	Activity GetMeleeShoveActivity() const;

	CNetworkVar( float, m_flLastAttackTime );
	CNetworkVar( int, m_hMeleeWeaponInfo );
	CNetworkVar( int, m_iNextPrimaryAttackIndex );
	CNetworkVar( int, m_iNextStrongAttackIndex );
	CNetworkVar( int, m_iNextSecondaryAttackIndex );
	CNetworkVar( bool, m_bInMeleeSwing );
	CNetworkVarEmbedded( CountdownTimer, m_meleeSwingTimer );

private:
	Activity SelectMeleeAttack( bool bPrimary );
	void StartMeleeAttack( CTerrorPlayer *pPlayer, bool bPrimary );
	void UpdateMeleeSwing();
	bool TraceMeleeSwing( const Vector &direction );
	bool CanHitMeleeTarget( CBaseEntity *pTarget ) const;
	bool HasHitMeleeTarget( CBaseEntity *pTarget ) const;
#ifndef CLIENT_DLL
	double GetMeleeDamage( CBaseEntity *pTarget ) const;
	void ShoveNearbyCommonInfected( CBaseEntity *pVictim );
	string_t m_strMapSetScriptName;
#endif
	// Original contact exclusion is an entity pointer list, separate from the
	// common weapon's character-handle shove list. It is reset at swing start.
	CUtlVectorFixedGrowable< CBaseEntity *, 16 > m_meleeAffectedEntities;
	Vector m_vecLastMeleeDirection;
	const MeleeAttackInfo_t *m_pCurrentAttack;
	float m_flMeleeSwingProgress;
};

#ifndef CLIENT_DLL
void TraceTerrorMeleeTongue( CTerrorPlayer *pPlayer, const Vector &start, const Vector &end, int damageType, const Vector &damageDirection );
#endif

#endif // L4D2_TERROR_MELEE_WEAPON_H
