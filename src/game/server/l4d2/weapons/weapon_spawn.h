//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_SPAWN_H
#define L4D2_WEAPON_SPAWN_H
#ifdef _WIN32
#pragma once
#endif

#include "items.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "l4d2/weapons/response_rules_weapon_source.h"
#include "l4d2/entities/visibility_monitor.h"
#include "tier1/utlvector.h"

class CTerrorPlayer;
class CTerrorWeapon;
class CTerrorWeaponInfo;

// The item manager owns this native circular record, including non-weapons.
// Selection traverses its existing links instead of allocating a neighbor copy.
struct WeaponSpawnSelection_t
{
	Vector origin;
	QAngle angles;
	WeaponSpawnSelection_t *next;
	EHANDLE entity;
};

// Original map flags, not the target CItem's SF_ITEM_START_CONSTRAINED.
enum
{
	SF_WEAPON_SPAWN_PHYSICS = 1,
	SF_WEAPON_SPAWN_MUST_EXIST = 2,
	SF_WEAPON_SPAWN_ABSORB_ANY = 4,
	SF_WEAPON_SPAWN_INFINITE = 8,
	SF_WEAPON_SPAWN_START_CONSTRAINED = 16
};

class CWeaponSpawn : public CItem, public IResponseRulesWeaponSource
{
public:
	DECLARE_CLASS( CWeaponSpawn, CItem );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CWeaponSpawn();
	virtual ~CWeaponSpawn();
	static const CUtlVector< CWeaponSpawn * > &GetWeaponSpawns();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual int GetUsePriority( CBaseEntity *user ) OVERRIDE { return IsEffectActive( EF_NODRAW ) ? 0 : 1; } // S29DC90.
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;
	virtual bool MyTouch( CBasePlayer *pPlayer ) OVERRIDE { return false; }
	virtual void ItemTouch( CBaseEntity *pOther ) OVERRIDE;

	virtual int GetWeaponID() const { return 0; }
	virtual void SetWeaponID( int id );
	virtual const char *GetWeaponModel() const;
	virtual const char *GetResponseRulesName() const OVERRIDE;
	virtual const char *GetWeaponSubType() const { return ""; }
	virtual int GetReplacementWeaponID() const { return 0; }
	virtual const char *GetReplacementEntityName() const { return NULL; }
	virtual bool ShouldConsumeCount( const CTerrorWeaponInfo *pInfo ) const;
	virtual bool CanReplaceWeapon( CTerrorWeapon *pWeapon ) const;
	virtual CBaseEntity *GiveToPlayer( CTerrorPlayer *pPlayer, const char *pszClassname );

	int GetItemCount() const { return m_itemCount; }
	void SetItemCount( int count ) { m_itemCount = count; }
	int GetWeaponSkin() const { return m_nWeaponSkin; }
	void SetWeaponSkin( int value ) { m_nWeaponSkin = value; }
	void SetGlowRange( float value ) { m_flGlowRange = value; }
	virtual int GetUseType( CBaseEntity *user ) OVERRIDE;
	const char *GetWeaponName() const;
	bool IsReplacement() const { return m_bReplacement; }
	bool AbsorbDroppedWeapon( CTerrorWeapon *pWeapon );

protected:
	void SetSpawnModel();
	bool ReplaceWithScriptedWeapon();
	void RegisterVisibility( float flRange = 400.0f, VisibilityMonitorCallback_t predicate = NULL );
	void UnregisterVisibility();
	void RetireEmptySpawn();

	CNetworkVar( int, m_weaponID );
	CNetworkVar( float, m_flGlowRange );
	int m_itemCount;
	int m_nWeaponSkin;
	int m_initialItemCount;
	bool m_bReplacement;
};

class CWeaponSpawnConfigurable : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CWeaponSpawnConfigurable, CWeaponSpawn );
	DECLARE_DATADESC();

	virtual void Spawn() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return m_weaponID; }
	virtual void SetWeaponID( int id ) OVERRIDE;
	virtual bool ShouldConsumeCount( const CTerrorWeaponInfo *pInfo ) const OVERRIDE;

	// The Director supplies its original circular-list traversal order here.
	// spawn_without_director calls this with NULL; it is not a fake cluster.
	void SelectWeapon( const WeaponSpawnSelection_t *pNeighbors = NULL );
	int ParseWeaponSelection();
	int GetSelectionType() const { return m_selectionType; }
	bool SpawnsWithoutDirector() const { return m_bSpawnWithoutDirector; }

private:
	string_t m_iszWeaponToSpawn;
	bool m_bSpawnWithoutDirector;
	bool m_bNoCSWeapons;
	int m_requestedWeaponID;
	int m_requestedTier;
	int m_requestedType;
	int m_requestedSlot;
	int m_selectionType;
};

class CWeaponAmmoSpawn : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CWeaponAmmoSpawn, CWeaponSpawn );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE;
	virtual const char *GetWeaponModel() const OVERRIDE;
	virtual const char *GetResponseRulesName() const OVERRIDE { return "Ammo"; } // S29F5E0
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;
};

class CWeaponScavengeItemSpawn : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CWeaponScavengeItemSpawn, CWeaponSpawn );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CWeaponScavengeItemSpawn();
	virtual void Spawn() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE;
	virtual const char *GetWeaponModel() const OVERRIDE { return ""; }
	virtual const char *GetResponseRulesName() const OVERRIDE { return "GasCan"; } // S29CDB0

	// Original S:29E580 is also called directly by the real cluster owner.
	// The input payload is unused; this is not a synthetic AcceptInput path.
	void InputSpawnScavengeItem( inputdata_t &inputdata );
	void InputTurnGlowsOn( inputdata_t &inputdata );
	void InputTurnGlowsOff( inputdata_t &inputdata );

private:
	void SetGlowState( int state );
	int m_iGlowState;
	EHANDLE m_hItem;
	COutputEvent m_OnItemPickedUp;
};

class CWeaponMeleeSpawn : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CWeaponMeleeSpawn, CWeaponSpawn );
	DECLARE_DATADESC();

	CWeaponMeleeSpawn();
	virtual void Spawn() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE;
	virtual const char *GetWeaponModel() const OVERRIDE;
	virtual const char *GetWeaponSubType() const OVERRIDE;
	virtual const char *GetResponseRulesName() const OVERRIDE;
	virtual bool CanReplaceWeapon( CTerrorWeapon *pWeapon ) const OVERRIDE;
	virtual CBaseEntity *GiveToPlayer( CTerrorPlayer *pPlayer, const char *pszClassname ) OVERRIDE;
	MeleeWeaponInfoHandle_t GetMeleeWeaponInfoHandle() const { return m_meleeWeapon; }
	void SetMeleeWeaponInfoHandle( MeleeWeaponInfoHandle_t handle ) { m_meleeWeapon = handle; }
	void SetMeleeWeaponName( string_t name ) { m_iszMeleeWeapon = name; SelectMeleeWeapon(); }

private:
	void SelectMeleeWeapon();
	string_t m_iszMeleeWeapon;
	MeleeWeaponInfoHandle_t m_meleeWeapon;
};

void AssociateDroppedTerrorWeaponWithSpawn( CTerrorWeapon *pNewWeapon, const CTerrorWeapon *pDroppedFrom );
bool MergeDroppedTerrorWeaponWithSpawn( CTerrorWeapon *pWeapon );

#endif // L4D2_WEAPON_SPAWN_H
