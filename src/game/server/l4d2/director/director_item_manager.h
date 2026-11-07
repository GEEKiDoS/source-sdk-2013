//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DIRECTOR_ITEM_MANAGER_H
#define DIRECTOR_ITEM_MANAGER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/mem_zero_on_new.h"
#include "tier1/utlmap.h"
#include "tier1/utlvector.h"
#include "l4d2/gamerules/mapinfo.h"
#include "l4d2/weapons/weapon_spawn.h"

class CTerrorPlayer;
class CPropHealthCabinet;
class CWeaponSpawn;
class CWeaponSpawnConfigurable;
class TerrorNavArea;
class IGameEvent;

// The original Director+1580 owner. Pending configurable spawns, healing
// encounters, revisit snapshots, and the byte-indexed nav cache are distinct
// collections. Neither the session manager nor the weapon construction list
// owns any of this state.
class CDirectorItemManager : public CMemZeroOnNew
{
public:
	CDirectorItemManager();
	~CDirectorItemManager();
	virtual void Reset();
	virtual void OnLevelEnd();
	virtual void OnMapStart();
	void Update();
	void FireGameEvent( IGameEvent *event );
	void RegisterWeaponSpawn( CWeaponSpawnConfigurable *spawn );
	bool RemoveItemFromSpawnManager( CBaseEntity *item );
	TerrorNavArea *GetItemNavArea( CBaseEntity *item );
	int CountTrackedWeapons( int weaponID ) const;
	void OnHealthCabinetOpened( CPropHealthCabinet *cabinet );
	bool CanSpawnCSWeapon( int weaponID, bool ignoreChance ) const;
	bool CanSpawnMeleeWeapon( const char *name ) const;
	bool CanUseWeapon( int weaponID ) const;
	void OnWeaponUsed( int weaponID );
	void LogScavengeItems( const char *filename ) const;

private:
	struct NavCacheEntry
	{
		TerrorNavArea *area;
		float timestamp;
	};
	typedef CUtlMap< CBaseHandle, NavCacheEntry, unsigned char > ItemNavCache;
	struct HealingItem
	{
		EHANDLE item;
		float flow;
	};
	typedef WeaponSpawnSelection_t Item;
	struct ItemSet
	{
		const char *name;
		bool isModel;
		CUtlVector< Item * > encounters;
		CUtlVector< Item > items;
		bool clustered;
	};
	enum ItemType
	{
		PILLS, MOLOTOV, AMMO, PIPE_BOMB, PISTOL, MAGNUM, GAS_CAN,
		PROPANE_TANK, OXYGEN_TANK, MELEE, ADRENALINE, DEFIBRILLATOR,
		VOMITJAR, UPGRADE, UPGRADEPACK, CHAINSAW, GRENADE_LAUNCHER,
		M60, CONFIGURABLE, ITEM_TYPE_COUNT
	};
	struct SavedItem
	{
		Vector origin;
		QAngle angles;
		int weaponOrMeleeID;
		int count;
		int spawnFlags;
	};
	struct SavedItemSet
	{
		CUtlVector< SavedItem > items;
	};
	struct RevisitMap
	{
		char mapName[128];
		SavedItemSet sets[23];
	};
	void CollectItems();
	void CollectItemSet( ItemSet &set, const char *name, bool isModel, bool skipFinale, bool clustered );
	void GroupItems( ItemSet &set, bool skipFinale, float range, CUtlVector< Item * > *finale );
	void SelectConfigurableWeapons();
	int PopulateItems( CUtlVector< Item * > &encounters, int desired, unsigned int method );
	void PlaceItemsAlongFlow( CUtlVector< Item * > &encounters, int desired );
	void PopulateMap();
	void RemoveEncounter( Item *first );
	void DrawItemSet( const ItemSet &set, const char *label ) const;
	void CollectHealingItems();
	void UpdateHealingItems();
	void ConvertPillsToKits( float ratio );
	bool ReplaceHealingItem( CWeaponSpawn *old, const char *classname, EHANDLE &replacement );
	RevisitMap *FindRevisit( const char *map ) const;
	RevisitMap *GetRevisitSource() const;
	void SaveRevisit();
	void RestoreRevisit( RevisitMap *map );
	void RemoveAllItems();
	void ClearRevisits();
	static int CompareHealingFlow( const HealingItem *a, const HealingItem *b );
	static bool LessHandle( const CBaseHandle &a, const CBaseHandle &b );

	CountdownTimer m_healingUpdateTimer;
	CUtlVector< HealingItem > m_healingItems;
	int m_firstHealingItem;
	bool m_healingItemsCollected;
	ItemSet m_itemSets[ITEM_TYPE_COUNT];
	CUtlVector< RevisitMap * > m_revisitMaps;
	CUtlVector< Item * > m_finaleEncounters;
	CUtlVector< CHandle< CWeaponSpawnConfigurable > > m_pendingWeaponSpawns;
	ItemNavCache m_itemNavCache;
	bool m_allowSuperWeapon;
	bool m_allowDefibrillator;
};
#endif
