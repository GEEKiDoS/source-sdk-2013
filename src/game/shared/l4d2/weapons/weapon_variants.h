//========= Copyright Valve Corporation, All rights reserved. ============//
// Weapon variants: when two or more mounted add-ons replace the same weapon,
// each add-on's cosmetic assets are relocated under a unique cache path and
// registered as a variant. The server only networks a variant index; clients
// resolve it to local relocated assets, or fall back to the normal path.
//=======================================================================//
#ifndef WEAPON_VARIANTS_H
#define WEAPON_VARIANTS_H
#pragma once

// Network value carried by weapons and spawn entities. 0 = no variant;
// otherwise the WEAPON_VARIANTS_TABLE string index + 1. Session-local only.
typedef int WeaponVariantIndex_t;
#define WEAPON_VARIANT_NONE			0
#define WEAPON_VARIANT_NETWORK_BITS	12

// Network string table: one string per active variant, the lowercase hex
// SHA-1 content key. Created by the server, read by clients.
#define WEAPON_VARIANTS_TABLE		"WeaponVariants"
#define WEAPON_VARIANTS_TABLE_MAX	4096

// Model roles passed to WeaponVariants_ResolveModel.
#define WEAPON_VARIANT_ROLE_VIEW	"view"
#define WEAPON_VARIANT_ROLE_WORLD	"world"
#define WEAPON_VARIANT_ROLE_PLAYER	"player"		// third-person carried model when it differs from world (dual pistols, defib paddles)

// Weapon names are weapon classnames ("weapon_rifle") or, for melee,
// "melee:<script name>" ("melee:katana").

// Scans mounted add-ons, builds/reuses the relocation cache, mounts it, and
// rebuilds the local registry. Call after every TerrorMountAddons().
void WeaponVariants_RebuildRegistry();

// Server: publishes local variant keys into the network string table.
// Client: rebuilds the network-index -> local variant map from the table.
void WeaponVariants_SyncNetworkTable();

// Server: random variant for a newly placed spawn of this weapon, or
// WEAPON_VARIANT_NONE when fewer than two add-ons replace it.
WeaponVariantIndex_t WeaponVariants_ChooseForSpawn( const char *weaponName );

// Client: relocated model path for this variant/role, or vanillaPath when the
// variant is unknown locally or does not replace that role. Never NULL.
const char *WeaponVariants_ResolveModel( WeaponVariantIndex_t variant, const char *role, const char *vanillaPath );

// Client: generated soundscript name for this variant, or vanillaName.
const char *WeaponVariants_ResolveSound( WeaponVariantIndex_t variant, const char *vanillaName );

// Session-independent identity for saving across map transitions: the
// variant's content key, or NULL for WEAPON_VARIANT_NONE/unknown indices.
const char *WeaponVariants_GetKey( WeaponVariantIndex_t variant );

// Server: current network index for a saved key, or WEAPON_VARIANT_NONE.
WeaponVariantIndex_t WeaponVariants_FindIndexByKey( const char *key );

#endif // WEAPON_VARIANTS_H
