//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_variants.h"
#include "l4d2/content/terror_variant_assets.h"
#include "networkstringtabledefs.h"
#include "igamesystem.h"

#include "tier0/memdbgon.h"

extern INetworkStringTableContainer *networkstringtable;

namespace
{
using TerrorVariantAssets::Variant;
CUtlVector<Variant> s_Variants;

INetworkStringTable *GetVariantTable()
{
	return networkstringtable ? networkstringtable->FindTable( WEAPON_VARIANTS_TABLE ) : NULL;
}

const int MAX_VARIANT_INDEX = ( 1 << WEAPON_VARIANT_NETWORK_BITS ) - 1;

#ifdef CLIENT_DLL
INetworkStringTable *s_pMappedTable;
CUtlVector<const Variant *> s_NetworkVariants;

const Variant *FindNetworkVariant( WeaponVariantIndex_t index )
{
	if ( index <= 0 || index > MAX_VARIANT_INDEX || !s_NetworkVariants.IsValidIndex( index ) )
		return NULL;
	INetworkStringTable *table = GetVariantTable();
	const Variant *variant = s_NetworkVariants[index];
	// A connection can replace the string table; never use an old row for a new key.
	return variant && table && table == s_pMappedTable && index <= table->GetNumStrings() &&
		variant->key == table->GetString( index - 1 ) ? variant : NULL;
}
#endif

// Map overrides are cleared by the sound emitter at level shutdown. Register
// again on each level rather than assuming a script survives across maps.
class CWeaponVariantSounds : public CAutoGameSystem
{
public:
	CWeaponVariantSounds() : CAutoGameSystem( "WeaponVariantSounds" ) {}
	void LevelInitPreEntity() OVERRIDE
	{
		TerrorVariantAssets::RegisterSoundOverrides( s_Variants );
	}
};
CWeaponVariantSounds s_WeaponVariantSounds;
}

void WeaponVariants_RebuildRegistry()
{
#ifdef CLIENT_DLL
	s_NetworkVariants.RemoveAll();
#endif
	TerrorVariantAssets::Rebuild( s_Variants );
	WeaponVariants_SyncNetworkTable();
}

void WeaponVariants_SyncNetworkTable()
{
	INetworkStringTable *table = GetVariantTable();
#ifdef CLIENT_DLL
	s_pMappedTable = table;
	s_NetworkVariants.RemoveAll();
	if ( !table )
		return;
	const int count = MIN( table->GetNumStrings(), MAX_VARIANT_INDEX );
	s_NetworkVariants.SetCount( count + 1 );
	s_NetworkVariants.FillWithValue( NULL );
	for ( int row = 0; row < count; ++row )
	{
		const char *key = table->GetString( row );
		if ( !TerrorVariantAssets::IsKey( key ) )
			continue;
		for ( const Variant &variant : s_Variants )
		{
			if ( variant.key == key )
			{
				s_NetworkVariants[row + 1] = &variant;
				break;
			}
		}
	}
#else
	if ( !table || s_Variants.IsEmpty() )
		return;
	const bool wasLocked = engine->LockNetworkStringTables( false );
	const int capacity = MIN( table->GetMaxStrings(), MAX_VARIANT_INDEX );
	for ( const Variant &variant : s_Variants )
	{
		if ( table->GetNumStrings() >= capacity )
			break;
		if ( table->FindStringIndex( variant.key ) == INVALID_STRING_INDEX )
			table->AddString( true, variant.key );
	}
	engine->LockNetworkStringTables( wasLocked );
#endif
}

WeaponVariantIndex_t WeaponVariants_ChooseForSpawn( const char *weaponName )
{
#ifndef CLIENT_DLL
	if ( weaponName && GetVariantTable() )
	{
		WeaponVariantIndex_t choice = WEAPON_VARIANT_NONE;
		int count = 0;
		for ( const Variant &variant : s_Variants )
		{
			if ( Q_stricmp( weaponName, variant.weapon ) )
				continue;
			const WeaponVariantIndex_t index = WeaponVariants_FindIndexByKey( variant.key );
			if ( index && RandomInt( 1, ++count ) == 1 )
				choice = index;
		}
		return count >= 2 ? choice : WEAPON_VARIANT_NONE;
	}
#endif
	return WEAPON_VARIANT_NONE;
}

const char *WeaponVariants_ResolveModel( WeaponVariantIndex_t index, const char *role, const char *vanillaPath )
{
#ifdef CLIENT_DLL
	if ( role && vanillaPath )
	{
		const Variant *variant = FindNetworkVariant( index );
		if ( variant )
		{
			char path[MAX_PATH];
			Q_strncpy( path, vanillaPath, sizeof( path ) );
			V_FixSlashes( path, '/' );
			for ( const TerrorVariantAssets::Model &model : variant->models )
			{
				if ( !Q_stricmp( model.role, role ) && !Q_stricmp( model.vanilla, path ) )
					return model.relocated.String();
			}
		}
	}
#endif
	return vanillaPath ? vanillaPath : "";
}

const char *WeaponVariants_ResolveSound( WeaponVariantIndex_t index, const char *vanillaName )
{
#ifdef CLIENT_DLL
	const Variant *variant = FindNetworkVariant( index );
	if ( variant && vanillaName )
	{
		for ( const TerrorVariantAssets::Sound &sound : variant->sounds )
		{
			if ( !Q_stricmp( sound.vanilla, vanillaName ) )
				return sound.relocated.String();
		}
	}
#endif
	return vanillaName;
}

const char *WeaponVariants_GetKey( WeaponVariantIndex_t index )
{
	INetworkStringTable *table = GetVariantTable();
	if ( !table || index <= 0 || index > MAX_VARIANT_INDEX || index > table->GetNumStrings() )
		return NULL;
	const char *key = table->GetString( index - 1 );
	return TerrorVariantAssets::IsKey( key ) ? key : NULL;
}

WeaponVariantIndex_t WeaponVariants_FindIndexByKey( const char *key )
{
	INetworkStringTable *table = GetVariantTable();
	if ( !table || !TerrorVariantAssets::IsKey( key ) )
		return WEAPON_VARIANT_NONE;
	const int row = table->FindStringIndex( key );
	return row >= 0 && row < table->GetNumStrings() && row < MAX_VARIANT_INDEX ? row + 1 : WEAPON_VARIANT_NONE;
}
