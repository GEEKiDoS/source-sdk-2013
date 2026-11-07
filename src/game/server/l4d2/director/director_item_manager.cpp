//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "l4d2/entities/prop_health_cabinet.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "datacache/imdlcache.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/terror_native_qsort.h"
#include "ndebugoverlay.h"
#include "l4d2/nav/terror_nav_world.h"
#include "filesystem.h"
#include "tier0/vcrmode.h"
#include <time.h>
#include <emmintrin.h>
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"

extern ConVar director_item_cluster_range;
extern ConVar director_weapon_cluster_range;
static ConVar director_convert_pills( "director_convert_pills", "1", 0x00004004 );
static ConVar director_convert_pills_critical_health( "director_convert_pills_critical_health", "50", 0x00004004 );
ConVar director_convert_pills_to_defib_health( "director_convert_pills_to_defib_health", "25", 0x00004004, "" );
static ConVar director_must_create_all_scavenge_items( "director_must_create_all_scavenge_items", "0", 0x00004004 );
static ConVar director_item_placement_method( "director_item_placement_method", "1", 0x00004004, "Which scavenge item placement method to use (0 = old, 1 = new)" );
static ConVar director_per_map_weapon_upgrade_chance( "director_per_map_weapon_upgrade_chance", "0.25", 0x00004004 );
static ConVar director_cs_weapon_spawn_chance( "director_cs_weapon_spawn_chance", "0.25", 0x00004004 );
static ConVar director_vs_convert_pills( "director_vs_convert_pills", "0.5", 0x00000006, "What fraction of pain pills should be converted to health kits in vs mode" );
ConVar director_debug_revisit( "director_debug_revisit", "0", 0x00004004, "" );

ConVar director_debug_scavenge_items( "director_debug_scavenge_items", "0", 0x00004004, "" );
ConVar director_item_placement_spew( "director_item_placement_spew", "0", 0x00004004, "Whether director item placement should spew a bunch of stats about what it did." );
// S25CDE0 -> S2904F0: this is the real mode producer, not an assumed ratio.
static int GetHealingItemConversion( float *ratio )
{
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
	{
		*ratio = director_vs_convert_pills.GetFloat();
		return 2;
	}
	return 1;
}

static const char *s_revisitClasses[] =
{
	"weapon_pain_pills_spawn", "weapon_first_aid_kit_spawn", "weapon_molotov_spawn",
	"weapon_pipe_bomb_spawn", "weapon_pistol_spawn", "weapon_pistol_magnum_spawn",
	"weapon_ammo_spawn", "upgrade_laser_sight", "weapon_adrenaline_spawn",
	"weapon_defibrillator_spawn", "weapon_vomitjar_spawn", "weapon_gascan_spawn",
	"weapon_upgradepack_incendiary_spawn", "weapon_upgradepack_explosive_spawn",
	"weapon_grenade_launcher_spawn", "weapon_chainsaw_spawn", "weapon_rifle_m60_spawn"
};
static const char *s_revisitModels[] =
{
	"models/props_junk/gascan001a.mdl", "models/props_equipment/oxygentank01.mdl",
	"models/props_junk/propanecanister001a.mdl", "models/props_junk/gnome.mdl"
};

#pragma float_control( precise, on, push )
static double GetScavengeArea()
{
	float area = 0.0f;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *nav = ToTerrorNavArea( TheNavAreas[i] );
		if ( !( nav->GetFlowDistance() < 0.0f ) && !nav->HasSpawnAttributes( TERROR_NAV_FINALE ) )
			area += nav->GetSizeY() * nav->GetSizeX();
	}
	// S27CDC0 / S283C40: SSE accumulation, x87 multiply by an F32 constant.
	return (double)area * (double)0.000000077160493f;
}
static int CountFromDensity( float density, float area )
{
	// S28418A and subsequent __ftol2_sse calls truncate the DOUBLE product.
	return _mm_cvttsd_si32( _mm_set_sd( (double)density * (double)area ) );
}
#pragma float_control( pop )

bool CDirectorItemManager::LessHandle( const CBaseHandle &a, const CBaseHandle &b )
{
	return (unsigned int)a.ToInt() < (unsigned int)b.ToInt();
}
CDirectorItemManager::CDirectorItemManager() : m_itemNavCache( LessHandle )
{
	// S2846F0 has no stores to these scalar fields. The sole native
	// CMemZeroOnNew allocation clears them before the members construct.
}
CDirectorItemManager::~CDirectorItemManager()
{
	ClearRevisits();
}
void CDirectorItemManager::Reset()
{
	// S27FA00. Retain capacity and pending map-created weapon spawns.
	m_itemNavCache.RemoveAll();
	m_healingItems.RemoveAll();
	m_healingItemsCollected = false;
	m_healingUpdateTimer.Start( 5.0f );
	m_allowSuperWeapon = true;
	m_allowDefibrillator = true;
}
void CDirectorItemManager::ClearRevisits()
{
	m_revisitMaps.PurgeAndDeleteElements();
}
void CDirectorItemManager::OnMapStart()
{
	// S2846A0: versus always discards the previous visit snapshots. In other
	// modes only the genuine map/mission transition bracket retains them.
	if ( TerrorGameRules()->IsVersusMode() || !TheDirector->IsChangingMapOrMission() )
		ClearRevisits();
}
void CDirectorItemManager::OnLevelEnd()
{
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter && chapter->GetInt( "revisitable", 0 ) )
		SaveRevisit();
}
void CDirectorItemManager::RegisterWeaponSpawn( CWeaponSpawnConfigurable *spawn )
{
	// S27F960 deliberately appends; it neither deduplicates nor registers a
	// constructor-order world-spawner list or a scavenge cluster.
	if ( spawn && !spawn->IsMarkedForDeletion() )
		m_pendingWeaponSpawns.AddToTail( spawn );
}
bool CDirectorItemManager::RemoveItemFromSpawnManager( CBaseEntity *item )
{
	// S281670 is CUtlRBTree::Find + RemoveAt. The two bytes written after
	// Unlink are the tree's free-node links, NOT an item FIFO. CUtlMap owns
	// the same byte-indexed tree and preserves the complete EHANDLE serial.
	CBaseHandle handle = item ? item->GetRefEHandle() : CBaseHandle( INVALID_EHANDLE );
	unsigned char index = m_itemNavCache.Find( handle );
	if ( index == m_itemNavCache.InvalidIndex() )
		return false;
	m_itemNavCache.RemoveAt( index );
	return true;
}
#pragma float_control( precise, on, push )
TerrorNavArea *CDirectorItemManager::GetItemNavArea( CBaseEntity *item )
{
	CBaseHandle handle = item ? item->GetRefEHandle() : CBaseHandle( INVALID_EHANDLE );
	unsigned char index = m_itemNavCache.Find( handle );
	if ( index != m_itemNavCache.InvalidIndex() )
	{
		m_itemNavCache[index].timestamp = gpGlobals->curtime;
		return m_itemNavCache[index].area;
	}
	// S2814D0 expires only on a miss. In-order successor is captured BEFORE
	// removing the node; the cache is not a frame-pruned queue.
	for ( unsigned char i = m_itemNavCache.FirstInorder(); i != m_itemNavCache.InvalidIndex(); )
	{
		unsigned char next = m_itemNavCache.NextInorder( i );
		if ( gpGlobals->curtime - m_itemNavCache[i].timestamp > 10.0f || !m_itemNavCache.Key( i ).Get() )
			m_itemNavCache.RemoveAt( i );
		i = next;
	}
	// S28162B dereferences NULL on a miss. Standing minimal-skip rule:
	// retain the preceding miss-pruning, skip only the missing-item query.
	if ( !item )
		return NULL;
	NavCacheEntry entry;
	entry.area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( item->WorldSpaceCenter(), false, 500.0f, true, true, TEAM_ANY ) );
	entry.timestamp = gpGlobals->curtime;
	m_itemNavCache.Insert( handle, entry );
	return entry.area;
}
#pragma float_control( pop )
int CDirectorItemManager::CountTrackedWeapons( int weaponID ) const
{
	int count = 0;
	FOR_EACH_MAP( m_itemNavCache, i )
	{
		CBaseEntity *item = static_cast< CBaseEntity * >( m_itemNavCache.Key( i ).Get() );
		CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( item );
		if ( spawn )
		{
			if ( spawn->GetWeaponID() == weaponID )
				++count;
		}
		else if ( item )
		{
			CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( item->MyCombatWeaponPointer() );
			if ( weapon && weapon->GetWeaponID() == weaponID )
				++count;
		}
	}
	return count;
}
bool CDirectorItemManager::CanUseWeapon( int weaponID ) const
{
	if ( weaponID == 20 || weaponID == 37 || weaponID == 21 )
		return m_allowSuperWeapon;
	if ( weaponID == 24 )
		return m_allowDefibrillator;
	return true;
}
void CDirectorItemManager::OnWeaponUsed( int weaponID )
{
	if ( weaponID == 20 || weaponID == 37 || weaponID == 21 )
		m_allowSuperWeapon = false;
	if ( weaponID == 24 )
		m_allowDefibrillator = false;
}
#pragma float_control( precise, on, push )
bool CDirectorItemManager::CanSpawnCSWeapon( int weaponID, bool ignoreChance ) const
{
	// S27BBC0. Preserve the apparently redundant cola branch and >= RNG
	// comparison. Mission metadata is not the effective gamemode descriptor.
	if ( Engine_IsCreatingXboxReslist() )
		return true;
	if ( weaponID == 28 )
	{
		if ( Engine_IsCreatingXboxReslist() )
		{
			KeyValues *mission = TerrorGameRules()->GetMissionInfo();
			if ( mission )
				return mission->GetInt( "allow_cola", 0 ) != 0;
		}
		return true;
	}
	if ( weaponID <= 32 || weaponID > 36 )
		return true;
	KeyValues *mission = TerrorGameRules()->GetMissionInfo();
	if ( mission && mission->GetInt( "no_cs_weapons", 0 ) )
		return false;
	if ( ignoreChance )
		return true;
	return director_cs_weapon_spawn_chance.GetFloat() >= RandomFloat( 0.0f, 1.0f );
}
#pragma float_control( pop )
bool CDirectorItemManager::CanSpawnMeleeWeapon( const char *name ) const
{
	if ( Engine_IsCreatingXboxReslist() || V_strnicmp( name, "knife", 5 ) )
		return true;
	KeyValues *mission = TerrorGameRules()->GetMissionInfo();
	return !mission || !mission->GetInt( "no_cs_weapons", 0 );
}
#pragma float_control( precise, on, push )
int CDirectorItemManager::CompareHealingFlow( const HealingItem *a, const HealingItem *b )
{
	if ( b->flow > a->flow )
		return -1;
	return a->flow > b->flow; // S27BA56/JBE: unordered compares equal.
}
#pragma float_control( pop )
void CDirectorItemManager::CollectHealingItems()
{
	if ( m_healingItemsCollected )
		return;
	MDLCACHE_CRITICAL_SECTION();
	const CUtlVector< CWeaponSpawn * > &spawns = CWeaponSpawn::GetWeaponSpawns();
	for ( int i = 0; i < spawns.Count(); ++i )
	{
		CWeaponSpawn *spawn = spawns[i];
		if ( !spawn || spawn->IsMarkedForDeletion() )
			continue;
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( spawn->GetWeaponID() );
		if ( !info || info->m_WeaponType != TERROR_WEAPONTYPE_HEALING )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( spawn->GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY ) );
		if ( area )
		{
			HealingItem item;
			item.item = spawn;
			item.flow = area->GetFlowDistance();
			m_healingItems.AddToTail( item );
		}
	}
	TerrorNativeQSort( m_healingItems.Base(), m_healingItems.Count(), sizeof( HealingItem ),
		[]( const void *a, const void *b ) -> int
		{
			return CompareHealingFlow( static_cast< const HealingItem * >( a ), static_cast< const HealingItem * >( b ) );
		} );
	m_healingItemsCollected = true;
	m_firstHealingItem = 0;
}
#pragma float_control( precise, on, push )
void CDirectorItemManager::OnHealthCabinetOpened( CPropHealthCabinet *cabinet )
{
	Vector cabinetMins, cabinetMaxs;
	cabinet->CollisionProp()->WorldSpaceAABB( &cabinetMins, &cabinetMaxs );
	for ( int i = m_firstHealingItem; i < m_healingItems.Count(); ++i )
	{
		CBaseEntity *item = m_healingItems[i].item.Get();
		if ( !item )
			continue;
		Vector mins, maxs;
		item->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		if ( maxs.x >= cabinetMins.x && cabinetMaxs.x >= mins.x &&
			maxs.y >= cabinetMins.y && cabinetMaxs.y >= mins.y &&
			maxs.z >= cabinetMins.z && cabinetMaxs.z >= mins.z )
		{
			m_healingItems[i].item = NULL;
			const Vector &center = item->WorldSpaceCenter();
			DevMsg( "Pills inside opening cabinet visible to team at ( %3.2f, %3.2f, %3.2f )\n", center.x, center.y, center.z );
		}
	}
}
#pragma float_control( pop )
bool CDirectorItemManager::ReplaceHealingItem( CWeaponSpawn *old, const char *classname, EHANDLE &replacement )
{
	EHANDLE previous = old;
	CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( CreateEntityByName( classname ) );
	if ( !spawn )
		return false;
	replacement = spawn;
	spawn->SetAbsOrigin( old->GetAbsOrigin() );
	spawn->SetAbsAngles( old->GetAbsAngles() );
	spawn->AddSpawnFlags( 0x8000 );
	spawn->SetItemCount( 1 );
	DispatchSpawn( spawn );
	// S280750/S280AD0: a spawn script may delete either entity. Keep the
	// successful creation's handle/counters; skip only stale-pointer work.
	if ( previous.Get() )
		UTIL_Remove( previous.Get() );
	return true;
}
#pragma float_control( precise, on, push )
void CDirectorItemManager::UpdateHealingItems()
{
	CollectHealingItems();
	int kitsNeeded = 0;
	bool hasDyingSurvivor = false;
	bool hasDefibrillator = false;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsConnected() ||
			player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() )
			continue;
		// S27CAD0 uses the native 242B20 ascending inventory scan for ID24,
		// but the distinct 2C98F0 ID-byte cache for ID12. Neither is a type query.
		for ( int slot = 0; slot < MAX_WEAPONS; ++slot )
		{
			CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( player->GetWeapon( slot ) );
			if ( weapon && weapon->GetWeaponID() == TERROR_WEAPON_DEFIBRILLATOR )
			{
				hasDefibrillator = true;
				break;
			}
		}
		if ( player->IsOnThirdStrike() )
			hasDyingSurvivor = true;
		if ( director_convert_pills_critical_health.GetFloat() > (float)player->GetHealth() )
		{
			if ( !player->GetWeaponOfType( TERROR_WEAPON_FIRST_AID_KIT ) )
				++kitsNeeded;
		}
		else if ( player->GetWeaponOfType( TERROR_WEAPON_FIRST_AID_KIT ) )
			--kitsNeeded;
	}
	bool needsDefibrillator = hasDyingSurvivor && !hasDefibrillator;
	CTerrorPlayer *leader = FindHighestFlowSurvivor( 0 );
	if ( !leader || !leader->GetLastKnownArea() )
		return;
	float flow = ToTerrorNavArea( leader->GetLastKnownArea() )->GetFlowDistance();
	for ( int i = m_firstHealingItem; i < m_healingItems.Count(); ++i )
	{
		HealingItem &entry = m_healingItems[i];
		if ( flow - 500.0f < entry.flow )
		{
			if ( entry.flow > flow + 2000.0f )
				return;
			CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( entry.item.Get() );
			if ( !spawn )
				continue;
			if ( spawn->GetWeaponID() == 24 )
				needsDefibrillator = false;
			else if ( spawn->GetWeaponID() == 12 )
				--kitsNeeded;
			else if ( IsTerrorSpawnPointVisibleToTeam( spawn->WorldSpaceCenter(), TEAM_SURVIVOR, 0, 1000.0f, NULL, spawn ) )
			{
				entry.item = NULL;
				const Vector &center = spawn->WorldSpaceCenter();
				DevMsg( "Pills visible to team at ( %3.2f, %3.2f, %3.2f )\n", center.x, center.y, center.z );
			}
			else if ( kitsNeeded > 0 || needsDefibrillator )
			{
				bool kit = kitsNeeded > 0;
				EHANDLE replacement;
				if ( ReplaceHealingItem( spawn, kit ? "weapon_first_aid_kit_spawn" : "weapon_defibrillator_spawn", replacement ) )
				{
					entry.item = replacement;
					if ( kit )
					{
						--kitsNeeded;
						L4DGameStats().Event_FirstAidKitSpawned();
						if ( replacement.Get() )
						{
							const Vector &origin = replacement->GetAbsOrigin();
							DevMsg( "Converted Pain Pills to First Aid Kit at ( %3.2f, %3.2f, %3.2f )\n", origin.x, origin.y, origin.z );
						}
					}
					else
					{
						needsDefibrillator = false;
						L4DGameStats().Event_DefibrillatorSpawned();
						if ( replacement.Get() )
						{
							const Vector &origin = replacement->GetAbsOrigin();
							DevMsg( "Converted healing item to Defibrillator at ( %3.2f, %3.2f, %3.2f )\n", origin.x, origin.y, origin.z );
						}
					}
				}
			}
		}
		else
			++m_firstHealingItem;
	}
}
#pragma float_control( pop )
#pragma float_control( precise, on, push )
void CDirectorItemManager::ConvertPillsToKits( float ratio )
{
	CollectHealingItems();
	int pills = 0;
	for ( int i = 0; i < m_healingItems.Count(); ++i )
	{
		CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( m_healingItems[i].item.Get() );
		if ( spawn && spawn->GetWeaponID() == 15 && !TheTerrorNavMesh()->GetCheckpoint( spawn->GetAbsOrigin() ) )
			++pills;
	}
	if ( !m_healingItems.Count() )
		return;
	float amount = (float)pills * ratio;
	if ( amount > (float)pills )
		amount = (float)pills;
	else if ( amount < 0.0f )
		amount = 0.0f;
	int desired = _mm_cvttss_si32( _mm_set_ss( amount ) );
	// S280AD0 uses this reverse shuffle, including the final singleton draw.
	for ( int i = m_healingItems.Count() - 1; i >= 0; --i )
	{
		int other = RandomInt( i, m_healingItems.Count() - 1 );
		V_swap( m_healingItems[i], m_healingItems[other] );
	}
	for ( int i = 0; i < m_healingItems.Count() && desired > 0; ++i )
	{
		CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( m_healingItems[i].item.Get() );
		if ( !spawn || spawn->GetWeaponID() != 15 || TheTerrorNavMesh()->GetCheckpoint( spawn->GetAbsOrigin() ) )
			continue;
		EHANDLE replacement;
		if ( ReplaceHealingItem( spawn, "weapon_first_aid_kit_spawn", replacement ) )
		{
			m_healingItems[i].item = replacement;
			L4DGameStats().Event_FirstAidKitSpawned();
			--desired;
			if ( replacement.Get() )
			{
				const Vector &origin = replacement->GetAbsOrigin();
				DevMsg( "Vs mode converted Pain Pills to First Aid Kit at ( %3.2f, %3.2f, %3.2f )\n", origin.x, origin.y, origin.z );
			}
		}
	}
}
#pragma float_control( pop )
#pragma float_control( precise, on, push )
void CDirectorItemManager::Update()
{
	if ( director_debug_scavenge_items.GetBool() )
	{
		static const ItemType order[] =
		{
			PILLS, MOLOTOV, AMMO, PIPE_BOMB, PISTOL, MAGNUM, GAS_CAN,
			OXYGEN_TANK, PROPANE_TANK, MELEE, ADRENALINE, DEFIBRILLATOR,
			VOMITJAR, UPGRADE, UPGRADEPACK, CHAINSAW, GRENADE_LAUNCHER, M60, CONFIGURABLE
		};
		static const char *const labels[] =
		{
			"PILLS", "MOLOTOV", "AMMO", "PIPE BOMB", "PISTOL", "MAGNUM", "GAS CAN",
			"O2 TANK", "PROPANE TANK", "MELEE WEAPON", "ADRENALINE", "DEFIBRILLATOR",
			"VOMIT JAR", "UPGRADE", "UPGRADEPACK", "CHAINSAW", "GRENADE LAUNCHER", "M60", "WEAPONSPAWN"
		};
		for ( int i = 0; i < ARRAYSIZE( order ); ++i )
			DrawItemSet( m_itemSets[order[i]], labels[i] );
	}
	float ratio;
	if ( director_convert_pills.GetBool() && TerrorGameRules()->GetDifficultyLevel() != 3 &&
		!TerrorGameRules()->IsPlayerControlledZombies() &&
		TheDirector->GetScriptedMode()->QueryOptionBool( "cm_AllowPillConversion", true ) &&
		m_healingUpdateTimer.m_timestamp > 0.0f && m_healingUpdateTimer.IsElapsed() &&
		GetHealingItemConversion( &ratio ) == 1 )
	{
		UpdateHealingItems();
		m_healingUpdateTimer.Start( 5.0f );
	}
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
void CDirectorItemManager::GroupItems( ItemSet &set, bool skipFinale, float range, CUtlVector< Item * > *finale )
{
	set.encounters.RemoveAll();
	for ( int i = 0; i < set.items.Count(); ++i )
	{
		Item *first = &set.items[i];
		if ( !set.clustered )
		{
			set.encounters.AddToTail( first );
			continue;
		}
		if ( first->next )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( first->origin, false, 500.0f, true, true, TEAM_ANY ) );
		if ( finale && area && area->HasSpawnAttributes( TERROR_NAV_FINALE ) )
		{
			if ( !skipFinale )
				finale->AddToTail( first );
		}
		else
			set.encounters.AddToTail( first );
		Item *last = first;
		for ( int j = i + 1; j < set.items.Count(); ++j )
		{
			Item *other = &set.items[j];
			if ( !other->next && range * range > ( first->origin - other->origin ).LengthSqr() )
			{
				last->next = other;
				other->next = first;
				last = other;
			}
		}
	}
}
#pragma float_control( pop )
void CDirectorItemManager::CollectItemSet( ItemSet &set, const char *name, bool isModel, bool skipFinale, bool clustered )
{
	set.name = name;
	set.isModel = isModel;
	set.clustered = clustered;
	set.items.RemoveAll();
	const char *classname = isModel ? "prop_physics" : name;
	for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, classname ); entity;
		entity = gEntList.FindEntityByClassname( entity, classname ) )
	{
		if ( entity->IsMarkedForDeletion() || ( isModel && V_stricmp( STRING( entity->GetModelName() ), name ) ) )
			continue;
		if ( !isModel && !TerrorGameRules()->IsScavengeMode() && !TerrorGameRules()->IsSurvivalMode() )
		{
			if ( entity->HasSpawnFlags( SF_WEAPON_SPAWN_MUST_EXIST ) )
				continue;
			TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( entity->GetAbsOrigin(), false, 500.0f, true, true, TEAM_ANY ) );
			if ( area && area->HasSpawnAttributes( TERROR_NAV_PLAYER_START | TERROR_NAV_CHECKPOINT ) )
				continue;
		}
		Item item;
		item.origin = entity->GetAbsOrigin();
		item.angles = entity->GetAbsAngles();
		item.next = NULL;
		item.entity = entity;
		set.items.AddToTail( item );
	}
	GroupItems( set, skipFinale, director_item_cluster_range.GetFloat(), &m_finaleEncounters );
	if ( director_debug_scavenge_items.GetBool() )
		Msg( "Collected %d total %s items in %d encounters \n", set.items.Count(), name, set.encounters.Count() );
}
void CDirectorItemManager::CollectItems()
{
	m_finaleEncounters.RemoveAll();
	bool skipFinale = !TerrorGameRules()->IsScavengeMode() && !TerrorGameRules()->IsSurvivalMode();
	CollectItemSet( m_itemSets[MOLOTOV], "weapon_molotov_spawn", false, false, true );
	CollectItemSet( m_itemSets[PIPE_BOMB], "weapon_pipe_bomb_spawn", false, false, true );
	CollectItemSet( m_itemSets[VOMITJAR], "weapon_vomitjar_spawn", false, false, true );
	CollectItemSet( m_itemSets[PILLS], "weapon_pain_pills_spawn", false, skipFinale, true );
	CollectItemSet( m_itemSets[PISTOL], "weapon_pistol_spawn", false, skipFinale, true );
	CollectItemSet( m_itemSets[MAGNUM], "weapon_pistol_magnum_spawn", false, skipFinale, true );
	CollectItemSet( m_itemSets[MELEE], "weapon_melee_spawn", false, skipFinale, true );
	CollectItemSet( m_itemSets[ADRENALINE], "weapon_adrenaline_spawn", false, skipFinale, true );
	CollectItemSet( m_itemSets[DEFIBRILLATOR], "weapon_defibrillator_spawn", false, skipFinale, true );
	CollectItemSet( m_itemSets[AMMO], "weapon_ammo_spawn", false, false, false );
	CollectItemSet( m_itemSets[UPGRADEPACK], "weapon_upgradepack_*", false, false, false );
	CollectItemSet( m_itemSets[UPGRADE], "upgrade_*", false, false, false );
	CollectItemSet( m_itemSets[CHAINSAW], "weapon_chainsaw_spawn", false, false, false );
	CollectItemSet( m_itemSets[GRENADE_LAUNCHER], "weapon_grenade_launcher_spawn", false, false, false );
	CollectItemSet( m_itemSets[M60], "weapon_rifle_m60_spawn", false, false, false );
	CollectItemSet( m_itemSets[GAS_CAN], s_revisitModels[0], true, false, true );
	CollectItemSet( m_itemSets[OXYGEN_TANK], s_revisitModels[1], true, false, true );
	CollectItemSet( m_itemSets[PROPANE_TANK], s_revisitModels[2], true, false, true );
}
void CDirectorItemManager::SelectConfigurableWeapons()
{
	if ( GetRevisitSource() )
	{
		FOR_EACH_VEC( m_pendingWeaponSpawns, i )
		{
			if ( m_pendingWeaponSpawns[i] )
				UTIL_Remove( m_pendingWeaponSpawns[i] );
		}
		m_pendingWeaponSpawns.RemoveAll();
		return;
	}
	ItemSet &set = m_itemSets[CONFIGURABLE];
	set.name = "weapon_spawn";
	set.isModel = false;
	set.clustered = true;
	set.items.RemoveAll();
	set.encounters.RemoveAll();
	ItemSet all;
	all.name = "weapon_spawn";
	all.isModel = false;
	all.clustered = true;
	FOR_EACH_VEC( m_pendingWeaponSpawns, i )
	{
		CWeaponSpawnConfigurable *spawn = m_pendingWeaponSpawns[i];
		if ( !spawn || spawn->IsMarkedForDeletion() )
			continue;
		Item item;
		item.origin = spawn->GetAbsOrigin();
		item.angles = spawn->GetAbsAngles();
		item.next = NULL;
		item.entity = spawn;
		all.items.AddToTail( item );
		if ( !spawn->SpawnsWithoutDirector() && !spawn->HasSpawnFlags( SF_WEAPON_SPAWN_MUST_EXIST ) )
			set.items.AddToTail( item );
	}
	m_pendingWeaponSpawns.RemoveAll();
	// Original S27C8E0 really is a positive-only qsort comparator. Do not
	// "fix" it to a conventional three-way comparator or stable sort.
	TerrorNativeQSort( all.items.Base(), all.items.Count(), sizeof( Item ),
		[]( const void *a, const void *b ) -> int
		{
			const Item *firstItem = static_cast< const Item * >( a );
			const Item *secondItem = static_cast< const Item * >( b );
			CWeaponSpawnConfigurable *first = static_cast< CWeaponSpawnConfigurable * >( firstItem->entity.Get() );
			CWeaponSpawnConfigurable *second = static_cast< CWeaponSpawnConfigurable * >( secondItem->entity.Get() );
			return ( first ? first->GetSelectionType() : 0 ) < ( second ? second->GetSelectionType() : 0 );
		} );
	// S280639..280682 retains cvar-based grouping if map-info creation failed.
	float range = director_weapon_cluster_range.GetFloat();
	CMapInfo *map = GetMapInfo();
	if ( map )
		range = map->GetParameter( CMapInfo::CONFIGURABLE_WEAPON_CLUSTER_RANGE );
	GroupItems( all, false, range, NULL );
	GroupItems( set, false, range, NULL );
	FOR_EACH_VEC( all.items, i )
	{
		Item *first = &all.items[i];
		CWeaponSpawnConfigurable *spawn = static_cast< CWeaponSpawnConfigurable * >( first->entity.Get() );
		if ( !spawn )
			continue;
		spawn->SelectWeapon( first );
	}
}
void CDirectorItemManager::RemoveEncounter( Item *first )
{
	Item *item = first;
	do
	{
		if ( item->entity )
		{
			UTIL_Remove( item->entity );
			item->entity = NULL;
		}
		item = item->next;
	}
	while ( item && item != first );
}
int CDirectorItemManager::PopulateItems( CUtlVector< Item * > &encounters, int desired, unsigned int method )
{
	int count = encounters.Count();
	if ( director_debug_scavenge_items.GetBool() && count )
	{
		CBaseEntity *entity = encounters[0]->entity.Get();
		Msg( "Attempting to populate %s %d items out of a possible %d \n",
			entity ? entity->GetClassname() : "UNKNOWN", desired, count );
	}
	if ( !count )
		return 0;
	if ( director_must_create_all_scavenge_items.GetBool() )
		return count;
	if ( TerrorGameRules()->IsSurvivalMode() || TerrorGameRules()->IsScavengeMode() || desired <= 0 )
	{
		for ( int i = count - 1; i >= 0; --i )
			RemoveEncounter( encounters[i] );
		return 0;
	}
	if ( method <= 1 )
	{
		for ( int i = 0; i < count - 1; ++i )
		{
			int other = RandomInt( i, count - 1 );
			V_swap( encounters[i], encounters[other] );
		}
	}
	if ( method == 1 )
		PlaceItemsAlongFlow( encounters, desired );
	else
	{
		desired = MIN( count, desired );
		for ( int i = desired; i < count; ++i )
			RemoveEncounter( encounters[i] );
	}
	int remaining = 0;
	FOR_EACH_VEC( encounters, i )
	{
		if ( encounters[i] && encounters[i]->entity && !encounters[i]->entity->IsMarkedForDeletion() )
			++remaining;
	}
	return remaining;
}
#pragma float_control( precise, on, push )
void CDirectorItemManager::PlaceItemsAlongFlow( CUtlVector< Item * > &encounters, int desired )
{
	float length = TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance();
	if ( !encounters.Count() )
		return;
	desired = MIN( desired, encounters.Count() );
	const bool spew = director_item_placement_spew.GetBool();
	const bool newMethod = director_item_placement_method.GetInt() == 1;
	if ( newMethod && spew )
		Msg( "Starting requested placement of %d items\n", desired );
	if ( newMethod )
	{
		float firstFlow = 1.0e9f;
		float lastFlow = 0.0f;
		FOR_EACH_VEC( encounters, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( encounters[i]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
			if ( area && area->GetFlowDistance() > 0.0f )
			{
				if ( firstFlow > area->GetFlowDistance() ) firstFlow = area->GetFlowDistance();
				if ( area->GetFlowDistance() > lastFlow ) lastFlow = area->GetFlowDistance();
			}
		}
		length = lastFlow - firstFlow;
		if ( spew )
			Msg( " first potential item at %2.2f, last at %2.2f, total dist is %2.2f, desired spacing is %2.2f.\n",
				firstFlow, lastFlow, length, (double)length / (double)desired );
	}
	const float started = (float)Plat_FloatTime();
	CUtlVector< Item * > available;
	available.AddVectorToTail( encounters );
	encounters.RemoveAll();
	int invalidCount = 0;
	if ( newMethod )
	{
		// Native invalid candidates survive in the world. The temporary
		// rejected-vector payload is unread; only its diagnostic count is live.
		while ( available.Count() )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( available[0]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
			if ( area && !( area->GetFlowDistance() < 0.0f ) ) break; // Native COMISS0,flow/JBE admits unordered.
			++invalidCount;
			available.Remove( 0 );
		}
		if ( !available.Count() )
			return;
	}
	encounters.AddToTail( available[0] );
	available.Remove( 0 );
	if ( newMethod )
	{
		int attempts = 0;
		int failures = 0;
		float variation = 0.5f;
		while ( encounters.Count() < desired && available.Count() )
		{
			++attempts;
			const float spacing = (float)( (double)length / (double)desired );
			const float minimum = (float)( (double)RandomFloat( 1.0f - MIN( variation, 1.0f ), 1.0f ) * (double)spacing );
			const float maximum = (float)( (double)RandomFloat( 1.0f, variation + 1.0f ) * (double)spacing );
			const int candidate = RandomInt( 0, available.Count() - 1 );
			TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( available[candidate]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
			if ( !area || area->GetFlowDistance() < 0.0f )
			{
				++invalidCount;
				available.Remove( candidate );
				continue;
			}
			bool tooClose = false;
			bool withinMaximum = false;
			FOR_EACH_VEC( encounters, i )
			{
				TerrorNavArea *selected = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( encounters[i]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
				if ( !selected ) continue; // S28213A NULL: skip only this undefined distance.
				const double distanceWide = fabs( (double)area->GetFlowDistance() - (double)selected->GetFlowDistance() );
				const float distance = (float)distanceWide;
				if ( (double)minimum > distanceWide )
				{
					tooClose = true;
					break;
				}
				if ( maximum > distance ) withinMaximum = true;
			}
			if ( !tooClose && ( withinMaximum || failures > 10 ) )
			{
				encounters.AddToTail( available[candidate] );
				available.Remove( candidate );
				failures = 0;
				continue;
			}
			if ( ++failures > 50 )
			{
				failures = 0;
				variation += 0.1f;
			}
			if ( attempts > 100 * desired )
			{
				if ( spew )
					Msg( " aborting after %d attempts. Had already placed %d items.\n", 100 * desired, encounters.Count() );
				while ( encounters.Count() < desired && available.Count() )
				{
					encounters.AddToTail( available[0] );
					available.Remove( 0 );
				}
			}
		}
	}
	else
	{
		const int attempts = available.Count() * available.Count();
		for ( int attempt = 0; encounters.Count() < desired && attempt != attempts; ++attempt )
		{
			const float spacing = (float)( (double)length / (double)desired );
			const double minimum = (double)RandomFloat( 0.5f, 1.0f ) * (double)spacing;
			const float minimumSquared = (float)( minimum * minimum );
			const double maximum = (double)RandomFloat( 1.0f, 1.5f ) * (double)spacing;
			const float maximumSquared = (float)( maximum * maximum );
			bool selected = false;
			for ( int i = 0; i < available.Count() && !selected; ++i )
			{
				TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( available[i]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
				if ( !area ) continue;
				FOR_EACH_VEC( encounters, j )
				{
					TerrorNavArea *other = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( encounters[j]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
					if ( !other ) continue;
					const float distance = other->GetFlowDistance() - area->GetFlowDistance();
					const float distanceSquared = distance * distance;
					if ( distanceSquared >= minimumSquared && maximumSquared >= distanceSquared )
					{
						encounters.AddToTail( available[i] );
						available.Remove( i );
						selected = true;
						break;
					}
				}
			}
		}
		while ( encounters.Count() < desired )
		{
			encounters.AddToTail( available[0] );
			available.Remove( 0 );
		}
	}
	const float finished = (float)Plat_FloatTime();
	if ( spew )
	{
		Msg( "Placement finished. Requested %d, placed %d of %d potential items.\n",
			desired, encounters.Count(), encounters.Count() + available.Count() + invalidCount );
		if ( newMethod ) Msg( " %d items were on invalid NavMesh.\n", invalidCount );
		Msg( " %2.2f seconds spent on placement.\n", (double)finished - (double)started );
	}
	FOR_EACH_VEC( available, i )
		RemoveEncounter( available[i] );
	if ( spew && encounters.Count() > 1 )
	{
		int current = 0;
		TerrorNavArea *first = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( encounters[0]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
		FOR_EACH_VEC( encounters, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( encounters[i]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
			if ( !first || !area ) continue; // Native diagnostic NULL read only.
			if ( first->GetFlowDistance() > area->GetFlowDistance() ) { first = area; current = i; }
		}
		if ( !first ) return;
		Msg( "First encounter: %d, %2.2f\n", current, first->GetFlowDistance() );
		float sum = 0.0f, minimum = 100000000.0f, maximum = 0.0f;
		for ( ;; )
		{
			float best = 1.0e10f;
			int next = -1;
			TerrorNavArea *selected = NULL;
			FOR_EACH_VEC( encounters, i )
			{
				if ( i == current ) continue;
				TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( encounters[i]->origin, false, 10000.0f, false, true, TEAM_ANY ) );
				if ( !area ) continue;
				const float distance = area->GetFlowDistance() - first->GetFlowDistance();
				if ( distance > 0.0f && best > distance ) { best = distance; next = i; selected = area; }
			}
			if ( !selected ) break;
			first = selected;
			current = next;
			Msg( " next encounter: %d %2.2f\n", next, selected->GetFlowDistance() );
			sum += best;
			if ( minimum > best ) minimum = best;
			if ( best > maximum ) maximum = best;
		}
		Msg( "Avg dist: %2.2f, min: %2.2f, max: %2.2f\n", (double)sum / (double)( encounters.Count() - 1 ), minimum, maximum );
	}
}
#pragma float_control( pop )

CDirectorItemManager::RevisitMap *CDirectorItemManager::FindRevisit( const char *map ) const
{
	FOR_EACH_VEC( m_revisitMaps, i )
	{
		if ( !V_strnicmp( m_revisitMaps[i]->mapName, map, 128 ) )
			return m_revisitMaps[i];
	}
	return NULL;
}
CDirectorItemManager::RevisitMap *CDirectorItemManager::GetRevisitSource() const
{
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( !chapter )
		return NULL;
	if ( TerrorGameRules()->IsVersusMode() )
		return TheDirector->IsInSecondHalfOfRound() ? FindRevisit( STRING( gpGlobals->mapname ) ) : NULL;
	const char *source = chapter->GetString( "revisit_source", NULL );
	return source ? FindRevisit( source ) : NULL;
}
void CDirectorItemManager::SaveRevisit()
{
	RevisitMap *map = new RevisitMap;
	V_strncpy( map->mapName, STRING( gpGlobals->mapname ), sizeof( map->mapName ) );
	for ( int type = 0; type < 23; ++type )
	{
		const char *name = type < 17 ? s_revisitClasses[type] : type < 21 ? s_revisitModels[type - 17] : type == 21 ? "weapon_spawn" : "weapon_melee_spawn";
		const char *classname = type >= 17 && type < 21 ? "prop_physics" : name;
		for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, classname ); entity;
			entity = gEntList.FindEntityByClassname( entity, classname ) )
		{
			if ( entity->IsMarkedForDeletion() || ( type >= 17 && type < 21 && V_stricmp( STRING( entity->GetModelName() ), name ) ) )
				continue;
			SavedItem &item = map->sets[type].items[map->sets[type].items.AddToTail()];
			item.origin = entity->GetAbsOrigin();
			item.angles = entity->GetAbsAngles();
			item.count = 1;
			CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( entity );
			if ( spawn )
			{
				item.count = spawn->GetItemCount();
				if ( type == 21 )
					item.weaponOrMeleeID = spawn->GetWeaponID();
			}
			if ( type == 22 )
			{
				CWeaponMeleeSpawn *melee = dynamic_cast< CWeaponMeleeSpawn * >( entity );
				if ( melee )
				{
					item.weaponOrMeleeID = melee->GetMeleeWeaponInfoHandle();
					item.count = melee->GetItemCount();
					item.spawnFlags = melee->GetSpawnFlags();
				}
			}
		}
	}
	// Latest saved visit wins; repeated map names are not deduplicated.
	m_revisitMaps.AddToHead( map );
}
void CDirectorItemManager::RemoveAllItems()
{
	for ( int type = 0; type < 22; ++type )
	{
		const char *name = type < 17 ? s_revisitClasses[type] : type < 21 ? s_revisitModels[type - 17] : "weapon_melee_spawn";
		const char *classname = type >= 17 && type < 21 ? "prop_physics" : name;
		CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, classname );
		while ( entity )
		{
			CBaseEntity *next = gEntList.FindEntityByClassname( entity, classname );
			if ( type < 17 || type == 21 || !V_stricmp( STRING( entity->GetModelName() ), name ) )
				UTIL_Remove( entity );
			entity = next;
		}
	}
	gEntList.CleanupDeleteList();
}
void CDirectorItemManager::RestoreRevisit( RevisitMap *map )
{
	if ( director_debug_revisit.GetBool() )
		DevMsg( "REVISIT: revisit source found, removing all items\n" );
	RemoveAllItems();
	int healingItems = 0;
	for ( int type = 0; type < 23; ++type )
	{
		const char *name = type < 17 ? s_revisitClasses[type] : type < 21 ? s_revisitModels[type - 17] : type == 21 ? "weapon_spawn" : "weapon_melee_spawn";
		FOR_EACH_VEC( map->sets[type].items, i )
		{
			const SavedItem &item = map->sets[type].items[i];
			if ( type >= 17 && type < 21 )
			{
				if ( director_debug_revisit.GetBool() )
					DevMsg( "REVISIT: restoring prop_physics ( %s )\n", name );
				CBaseEntity *prop = CreateEntityByName( "prop_physics" );
				if ( prop )
				{
					prop->SetModel( name );
					prop->SetAbsOrigin( item.origin );
					prop->SetAbsAngles( item.angles );
					DispatchSpawn( prop );
				}
				continue;
			}
			CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( CreateEntityByName( name ) );
			if ( !spawn )
				continue;
			if ( director_debug_revisit.GetBool() )
				DevMsg( "REVISIT: restoring %s\n", name );
			spawn->SetAbsOrigin( item.origin );
			spawn->SetAbsAngles( item.angles );
			spawn->AddSpawnFlags( 0x8000 );
			spawn->SetItemCount( item.count );
			if ( type == 21 )
			{
				spawn->SetWeaponID( item.weaponOrMeleeID );
				if ( director_debug_revisit.GetBool() )
					DevMsg( "REVISIT: ( weaponid = %d )\n", item.weaponOrMeleeID );
				if ( TerrorGameRules()->IsCoopMode() || TerrorGameRules()->IsRealismMode() )
					spawn->SetItemCount( 5 );
			}
			else if ( type == 22 )
			{
				CWeaponMeleeSpawn *melee = dynamic_cast< CWeaponMeleeSpawn * >( spawn );
				if ( melee )
				{
					if ( director_debug_revisit.GetBool() )
						DevMsg( "REVISIT: ( melee info handle = %d )\n", item.weaponOrMeleeID );
					melee->SetMeleeWeaponInfoHandle( (MeleeWeaponInfoHandle_t)item.weaponOrMeleeID );
					melee->AddSpawnFlags( item.spawnFlags );
				}
			}
			switch ( spawn->GetWeaponID() )
			{
			case 13: case 14: case 15: case 23: case 24: case 25:
				++healingItems;
				break;
			}
			DispatchSpawn( spawn );
		}
	}
	m_healingItems.RemoveAll();
	m_healingItemsCollected = true;
	m_firstHealingItem = 0;
	// Original sets collected=true before calling CollectHealingItems; its
	// empty healing vector after restoration is intentional, not repaired.
	CollectHealingItems();
	CollectItems();
	L4DGameStats().SetInitialHealthItemCount( healingItems );
}
#pragma float_control( precise, on, push )
void CDirectorItemManager::PopulateMap()
{
	float walkableArea = (float)GetScavengeArea();
	if ( director_debug_scavenge_items.GetBool() )
		DevMsg( "Total walkable area = %f sq 100 yards\n", walkableArea );
	CMapInfo *map = GetMapInfo();
	if ( !map )
		return;
	RevisitMap *revisit = GetRevisitSource();
	if ( revisit )
	{
		RestoreRevisit( revisit );
		return;
	}
	// This 1-square fallback is in original S283C40. It is not evidence that
	// the native campaign endpoint/flow producer has been integrated.
	if ( walkableArea <= 0.0f )
		walkableArea = 1.0f;
	int desired = CountFromDensity( map->GetParameter( CMapInfo::DEFIBRILLATOR_DENSITY ), walkableArea );
	if ( ( TerrorGameRules()->IsVersusMode() || TerrorGameRules()->IsScavengeMode() ) && desired >= 1 )
		desired = 1;
	int healthItems = PopulateItems( m_itemSets[DEFIBRILLATOR].encounters, desired, 1 );
	healthItems += PopulateItems( m_itemSets[PILLS].encounters, CountFromDensity( map->GetParameter( CMapInfo::PAIN_PILL_DENSITY ), walkableArea ), 1 );
	healthItems += PopulateItems( m_itemSets[MOLOTOV].encounters, CountFromDensity( map->GetParameter( CMapInfo::MOLOTOV_DENSITY ), walkableArea ), 1 );
	healthItems += PopulateItems( m_itemSets[PIPE_BOMB].encounters, CountFromDensity( map->GetParameter( CMapInfo::PIPE_BOMB_DENSITY ), walkableArea ), 1 );
	healthItems += PopulateItems( m_itemSets[ADRENALINE].encounters, CountFromDensity( map->GetParameter( CMapInfo::ADRENALINE_DENSITY ), walkableArea ), 1 );
	healthItems += PopulateItems( m_itemSets[VOMITJAR].encounters, CountFromDensity( map->GetParameter( CMapInfo::VOMITJAR_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[MELEE].encounters, CountFromDensity( map->GetParameter( CMapInfo::MELEE_WEAPON_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[AMMO].encounters, CountFromDensity( map->GetParameter( CMapInfo::AMMO_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[PISTOL].encounters, CountFromDensity( map->GetParameter( CMapInfo::PISTOL_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[GAS_CAN].encounters, CountFromDensity( map->GetParameter( CMapInfo::GAS_CAN_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[PROPANE_TANK].encounters, CountFromDensity( map->GetParameter( CMapInfo::PROPANE_TANK_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[OXYGEN_TANK].encounters, CountFromDensity( map->GetParameter( CMapInfo::OXYGEN_TANK_DENSITY ), walkableArea ), 1 );
	PopulateItems( m_itemSets[UPGRADEPACK].encounters, CountFromDensity( map->GetParameter( CMapInfo::UPGRADEPACK_DENSITY ), walkableArea ), 1 );
	if ( map->GetParameter( CMapInfo::CONFIGURABLE_WEAPON_DENSITY ) >= 0.0f )
		PopulateItems( m_itemSets[CONFIGURABLE].encounters, CountFromDensity( map->GetParameter( CMapInfo::CONFIGURABLE_WEAPON_DENSITY ), walkableArea ), 1 );
	if ( map->GetParameter( CMapInfo::MAGNUM_DENSITY ) >= 0.0f )
		PopulateItems( m_itemSets[MAGNUM].encounters, CountFromDensity( map->GetParameter( CMapInfo::MAGNUM_DENSITY ), walkableArea ), 1 );
	const double superWeaponsWide = (double)map->GetParameter( CMapInfo::SUPER_WEAPON_DENSITY ) * (double)walkableArea;
	const float superWeapons = (float)superWeaponsWide;
	int superCount = _mm_cvttss_si32( _mm_set_ss( superWeapons ) );
	if ( superWeaponsWide > 0.0 && superWeapons < 1.0f && superWeapons > RandomFloat( 0.0f, 1.0f ) )
		superCount = 1;
	int counts[3] = { 0, 0, 0 };
	int available[3] = { m_itemSets[CHAINSAW].encounters.Count(), m_itemSets[GRENADE_LAUNCHER].encounters.Count(), m_itemSets[M60].encounters.Count() };
	superCount = MIN( superCount, available[0] + available[1] + available[2] );
	// The singleton draw is consumed even when there are no super weapons.
	for ( int type = RandomInt( 0, 2 ); superCount > 0; type = ( type + 1 ) % 3 )
	{
		if ( counts[type] < available[type] )
		{
			++counts[type];
			--superCount;
		}
	}
	PopulateItems( m_itemSets[CHAINSAW].encounters, counts[0], 0 );
	PopulateItems( m_itemSets[GRENADE_LAUNCHER].encounters, counts[1], 0 );
	PopulateItems( m_itemSets[M60].encounters, counts[2], 0 );
	PopulateItems( m_itemSets[UPGRADE].encounters, director_per_map_weapon_upgrade_chance.GetFloat() > RandomFloat( 0.0f, 1.0f ), 0 );
	healthItems += PopulateItems( m_finaleEncounters, map->GetFinaleItemClusterCount(), 0 );
	L4DGameStats().SetInitialHealthItemCount( healthItems );
	if ( TerrorGameRules()->IsScavengeMode() || TerrorGameRules()->IsSurvivalMode() )
	{
		const char *classes[] = { "weapon_spawn", "weapon_first_aid_kit_spawn" };
		for ( int type = 0; type < ARRAYSIZE( classes ); ++type )
		{
			for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, classes[type] ); entity;
				entity = gEntList.FindEntityByClassname( entity, classes[type] ) )
				UTIL_Remove( entity );
		}
	}
	float ratio;
	if ( !TerrorGameRules()->IsPlayerControlledZombies() &&
		TheDirector->GetScriptedMode()->QueryOptionBool( "cm_AllowPillConversion", true ) &&
		GetHealingItemConversion( &ratio ) == 2 )
	{
		ConvertPillsToKits( ratio );
		m_healingUpdateTimer.Invalidate();
	}
}
#pragma float_control( pop )
void CDirectorItemManager::FireGameEvent( IGameEvent *event )
{
	if ( !V_strcmp( event->GetName(), "round_start_post_nav" ) )
	{
		SelectConfigurableWeapons();
		CollectItems();
		PopulateMap();
		if ( TerrorGameRules()->IsVersusMode() && !FindRevisit( STRING( gpGlobals->mapname ) ) )
		{
			for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "prop_health_cabinet" ); entity;
				entity = gEntList.FindEntityByClassname( entity, "prop_health_cabinet" ) )
			{
				CPropHealthCabinet *cabinet = dynamic_cast< CPropHealthCabinet * >( entity );
				if ( cabinet )
					cabinet->Populate();
			}
			SaveRevisit();
		}
	}
}

void CDirectorItemManager::DrawItemSet( const ItemSet &set, const char *label ) const
{
	// S27C090 intentionally redraws the shared finale list for EACH category.
	for ( int pass = 0; pass < 2; ++pass )
	{
		const CUtlVector< Item * > &encounters = pass ? m_finaleEncounters : set.encounters;
		FOR_EACH_VEC( encounters, i )
		{
			const Item *first = encounters[i];
			const Item *item = first;
			if ( !item ) continue;
			do
			{
				NDebugOverlay::Text( item->origin, label, false, 0.01023f );
				NDebugOverlay::Axis( item->origin, item->angles, 5.0f, true, 0.01023f );
				if ( item->next )
					NDebugOverlay::Line( item->origin, item->next->origin, 255, pass ? 0 : 255, pass ? 255 : 0, true, 0.01023f );
				if ( item->entity.Get() )
					NDebugOverlay::VertArrow( item->origin + Vector( 0.0f, 0.0f, 25.0f ), item->origin, 5.0f,
						pass ? 0 : 255, pass ? 255 : 100, pass ? 255 : 0, 255, true, 0.01023f );
				item = item->next;
			}
			while ( item && item != first );
		}
	}
}

static void director_report_scavenge_items( const CCommand &args )
{
	Msg( "\nREPORTING ALL SCAVENGE ITEMS\n" );
	for ( int type = 0; type < ARRAYSIZE( s_revisitClasses ) + ARRAYSIZE( s_revisitModels ); ++type )
	{
		const bool model = type >= ARRAYSIZE( s_revisitClasses );
		const char *name = model ? s_revisitModels[type - ARRAYSIZE( s_revisitClasses )] : s_revisitClasses[type];
		const char *classname = model ? "prop_physics" : name;
		for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, classname ); entity; )
		{
			CBaseEntity *next = gEntList.FindEntityByClassname( entity, classname );
			if ( !entity->IsMarkedForDeletion() && ( !model || !Q_stricmp( STRING( entity->GetModelName() ), name ) ) )
			{
				const Vector &origin = entity->GetAbsOrigin();
				Msg( "%s - %2.2f %2.2f %2.2f\n", name, origin.x, origin.y, origin.z );
			}
			entity = next;
		}
	}
}

#pragma float_control( precise, on, push )
static void director_solve_item_density( const CCommand &args )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "USAGE: director_solve_item_density <number of items desired in map>\n" );
		return;
	}
	const float desired = static_cast< float >( V_atod( args[1] ) );
	const double area = GetScavengeArea();
	const float areaStored = (float)area;
	Msg( "Map covers %6.4f square 100 yards\n", area );
	Msg( "Set your map density for this item to %4.3f\n", (double)desired / (double)areaStored * (double)1.1f );
}

namespace
{
const char *DescribeScavengeItem( CBaseEntity *entity )
{
	if ( !entity ) return "UNKNOWN";
	if ( FClassnameIs( entity, "prop_physics" ) )
	{
		const char *model = STRING( entity->GetModelName() );
		if ( !Q_stricmp( model, s_revisitModels[0] ) ) return "GAS CAN";
		if ( !Q_stricmp( model, s_revisitModels[1] ) ) return "OXYGEN TANK";
		if ( !Q_stricmp( model, s_revisitModels[2] ) ) return "PROPANE CANISTER";
	}
	return entity->GetClassname();
}


}

void CDirectorItemManager::LogScavengeItems( const char *filename ) const
{
	ConVarRef logDirectory( "sv_logsdir" );
	FileHandle_t file = filesystem->Open( filename, "a", "LOGDIR" );
	if ( file == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "Unable to log scavenge items to %s\n", filename );
		return;
	}
	struct tm now;
	VCRHook_LocalTime( &now );
	filesystem->FPrintf( file, "\n===============================================================================\n\n" );
	filesystem->FPrintf( file, "%02i/%02i/%04i - %02i:%02i:%02i\n",
		now.tm_mon + 1, now.tm_mday, now.tm_year + 1900, now.tm_hour, now.tm_min, now.tm_sec );
	filesystem->FPrintf( file, "Logging scavenge items for %s ver %d\n", STRING( gpGlobals->mapname ), gpGlobals->mapversion );
	ConVarRef mode( "mp_gamemode" ), difficulty( "z_difficulty" );
	filesystem->FPrintf( file, "Game Mode: %s\n", mode.GetString() );
	filesystem->FPrintf( file, "Difficulty: %s\n", difficulty.GetString() );
	if ( TerrorGameRules()->IsVersusMode() )
	{
		if ( TerrorGameRules()->IsScavengeSecondHalf() )
			filesystem->FPrintf( file, "Game is in the second half of versus mode\n" );
		if ( TheDirector->AreTeamsFlipped() )
			filesystem->FPrintf( file, "Versus Teams are flipped\n" );
	}
	for ( int team = TEAM_SURVIVOR; team <= TEAM_INFECTED; ++team )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() &&
				!player->IsBot() && player->GetTeamNumber() == team )
				filesystem->FPrintf( file, "[%s] Player %-2d: %s\n", team == TEAM_SURVIVOR ? "SURVIVOR" : "ZOMBIE  ", i, player->GetPlayerName() );
		}
	}
	const double areaWide = GetScavengeArea();
	const float area = (float)areaWide;
	filesystem->FPrintf( file, "\nTotal scavenge area = %f sq 100 yards\n", areaWide );
	CMapInfo *map = GetMapInfo();
	filesystem->FPrintf( file, "\n" );
	static const char *const labels[ITEM_TYPE_COUNT] =
	{
		"Pain Pills", "Molotov", "Ammo", "Pipe Bombs", "Pistols", "Magnums", "Gas Cans",
		"Propane Tanks", "Oxygen Tanks", "Melee Weapons", "Adrenaline", "Defibs", "Vomit Jars",
		"Upgrades", "Upgrade Packs", "Chainsaws", "Grenade Launchers", "M60s", "Configurable Weapon"
	};
	static const CMapInfo::Parameter densities[ITEM_TYPE_COUNT] =
	{
		CMapInfo::PAIN_PILL_DENSITY, CMapInfo::MOLOTOV_DENSITY, CMapInfo::AMMO_DENSITY,
		CMapInfo::PIPE_BOMB_DENSITY, CMapInfo::PISTOL_DENSITY, CMapInfo::MAGNUM_DENSITY,
		CMapInfo::GAS_CAN_DENSITY, CMapInfo::PROPANE_TANK_DENSITY, CMapInfo::OXYGEN_TANK_DENSITY,
		CMapInfo::MELEE_WEAPON_DENSITY, CMapInfo::ADRENALINE_DENSITY, CMapInfo::DEFIBRILLATOR_DENSITY,
		CMapInfo::VOMITJAR_DENSITY, CMapInfo::UPGRADEPACK_DENSITY, CMapInfo::UPGRADEPACK_DENSITY,
		CMapInfo::SUPER_WEAPON_DENSITY, CMapInfo::SUPER_WEAPON_DENSITY, CMapInfo::SUPER_WEAPON_DENSITY,
		CMapInfo::CONFIGURABLE_WEAPON_DENSITY
	};
	// Missing native map-info factory: skip ONLY the undefined density reads.
	if ( map )
	{
		for ( int type = 0; type < ITEM_TYPE_COUNT; ++type )
		{
			const ItemSet &set = m_itemSets[type];
			int active = 0;
			FOR_EACH_VEC( set.items, i )
				if ( set.items[i].entity.Get() ) ++active;
			const float density = map->GetParameter( densities[type] );
			const int desired = _mm_cvttss_si32( _mm_set_ss( density * area ) );
			filesystem->FPrintf( file, "%-20s: %3d out of %3d, desired %3d based on density %3.3f\n",
				labels[type], active, set.items.Count(), desired, density );
		}
	}
	CUtlVector< Item > items;
	for ( int type = 0; type < ITEM_TYPE_COUNT; ++type )
	{
		const ItemSet &set = m_itemSets[type];
		FOR_EACH_VEC( set.items, i )
			if ( set.items[i].entity.Get() ) items.AddToTail( set.items[i] );
	}
	TerrorNativeQSort( items.Base(), items.Count(), sizeof( Item ),
		[]( const void *a, const void *b ) -> int
		{
			CBaseEntity *first = static_cast< const Item * >( a )->entity.Get();
			CBaseEntity *second = static_cast< const Item * >( b )->entity.Get();
			if ( !first || !second ) return 0; // S27CF40 invalid-handle dereference only.
			TerrorNavArea *firstArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( first->GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY ) );
			TerrorNavArea *secondArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( second->GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY ) );
			if ( firstArea == secondArea ) return 0;
			const float firstFlow = firstArea ? firstArea->GetFlowDistance() : -1.0f;
			const float secondFlow = secondArea ? secondArea->GetFlowDistance() : -1.0f;
			if ( secondFlow > firstFlow ) return -1;
			return firstFlow > secondFlow;
		} );
	filesystem->FPrintf( file, "\nPrinting placement info for %d active scavenge items\n", items.Count() );
	FOR_EACH_VEC( items, i )
	{
		CBaseEntity *entity = items[i].entity.Get();
		if ( !entity ) continue;
		TerrorNavArea *nav = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( entity->GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY ) );
		filesystem->FPrintf( file, "%-54s %-6.1f\n", STRING( entity->GetModelName() ), nav ? nav->GetFlowDistance() : -1.0f );
	}
	filesystem->FPrintf( file, "\nPrinting Finale items:\n" );
	FOR_EACH_VEC( m_finaleEncounters, i )
	{
		CBaseEntity *entity = m_finaleEncounters[i]->entity.Get();
		if ( !entity ) continue;
		TerrorNavArea *nav = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( entity->GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY ) );
		filesystem->FPrintf( file, "%-16s %-6.1f\n", DescribeScavengeItem( entity ), nav ? nav->GetFlowDistance() : -1.0f );
	}
	filesystem->FPrintf( file, "\nPrinting survivor items:\n" );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player || !player->edict() || !player->entindex() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		filesystem->FPrintf( file, "[%d] %s: ", i, player->GetPlayerName() );
		for ( int slot = 0; slot < MAX_WEAPONS; ++slot )
		{
			CBaseCombatWeapon *weapon = player->GetWeapon( slot );
			if ( !weapon ) continue;
			if ( weapon->GetEntityName() != NULL_STRING )
				filesystem->FPrintf( file, "%s %s, ", weapon->GetClassname(), STRING( weapon->GetEntityName() ) );
			else
				filesystem->FPrintf( file, "%s, ", weapon->GetClassname() );
		}
		filesystem->FPrintf( file, "\n" );
	}
	CUtlVector< CBaseEntity * > checkpointItems;
	for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
		if ( dynamic_cast< CBaseCombatWeapon * >( entity ) || dynamic_cast< CItem * >( entity ) )
			checkpointItems.AddToTail( entity );
	filesystem->FPrintf( file, "\nPrinting ALL checkpoint items (including non-scavenge):\n" );
	const CUtlVector< CTerrorCheckpoint * > &checkpoints = TheTerrorNavMesh()->GetCheckpoints();
	FOR_EACH_VEC( checkpoints, i )
	{
		TheNavMesh->ClearSelectedSet();
		CTerrorCheckpoint *checkpoint = checkpoints[i];
		if ( !checkpoint ) continue;
		TerrorNavArea *seed = checkpoint->GetLargestSpawnArea();
		if ( !seed ) continue;
		const char *name = seed->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) || seed->GetFlowDistance() < 1000.0f ? "start" : "finish";
		filesystem->FPrintf( file, "Checkpoint %d %s \n", seed->GetID(), name );
		TheTerrorNavMesh()->SelectNativeRegion( seed, TERROR_NAV_CHECKPOINT, 0 );
		TheNavMesh->AddToSelectedSet( seed );
		const NavAreaVector &selected = TheNavMesh->GetSelectedSet();
		FOR_EACH_VEC( selected, j )
		{
			for ( int item = 0; item < checkpointItems.Count(); )
			{
				CBaseEntity *entity = checkpointItems[item];
				CBaseCombatWeapon *weapon = dynamic_cast< CBaseCombatWeapon * >( entity );
				if ( !entity || ( weapon && weapon->GetOwner() ) ||
					( !selected[j]->IsOverlapping( entity->GetAbsOrigin() ) &&
					  !( selected[j]->GetDistanceSquaredToPoint( entity->GetAbsOrigin() ) < 10000.0f ) ) )
				{
					++item;
					continue;
				}
				const Vector &origin = entity->GetAbsOrigin();
				if ( entity->GetEntityName() != NULL_STRING )
					filesystem->FPrintf( file, "%s %s (%f,%f,%f) \n", entity->GetClassname(),
						STRING( entity->GetEntityName() ), origin.x, origin.y, origin.z );
				else
					filesystem->FPrintf( file, "%s (%f,%f,%f)\n", entity->GetClassname(), origin.x, origin.y, origin.z );
				checkpointItems.Remove( item );
			}
		}
	}
	filesystem->Close( file );
	Msg( "Logged scavenge item info to %s\n", filename );
}
#pragma float_control( pop )

static void director_log_scavenge_items( const CCommand &args )
{
	if ( UTIL_IsCommandIssuedByServerAdmin() )
		TheDirector->GetItemManager()->LogScavengeItems( args.ArgC() == 2 ? args.ArgS() : "scavenge_items.log" );
}

#include "l4d2/director/director_item_manager_console_server_registrations.inc"
