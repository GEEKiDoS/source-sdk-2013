//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Original L4D2 ammunition registry and game-system initialization.
//
//=============================================================================//

#include "cbase.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "l4d2/weapons/terror_ammodef_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar ammo_pistol_max( "ammo_pistol_max", "-2", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_assaultrifle_max( "ammo_assaultrifle_max", "360", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_minigun_max( "ammo_minigun_max", "800", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_smg_max( "ammo_smg_max", "650", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_m60_max( "ammo_m60_max", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_shotgun_max( "ammo_shotgun_max", "72", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_autoshotgun_max( "ammo_autoshotgun_max", "90", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_huntingrifle_max( "ammo_huntingrifle_max", "150", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_sniperrifle_max( "ammo_sniperrifle_max", "180", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_turret_max( "ammo_turret_max", "300", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_pipebomb_max( "ammo_pipebomb_max", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_molotov_max( "ammo_molotov_max", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_vomitjar_max( "ammo_vomitjar_max", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_painpills_max( "ammo_painpills_max", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_firstaid_max( "ammo_firstaid_max", "-1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_grenadelauncher_max( "ammo_grenadelauncher_max", "30", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
ConVar ammo_adrenaline_max( "ammo_adrenaline_max", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );

static CAmmoDef s_TerrorAmmoDef;

class CTerrorAmmoDefSystem : public CAutoGameSystem
{
public:
	CTerrorAmmoDefSystem() : CAutoGameSystem( "CTerrorAmmoDefSystem" ) {}
	virtual bool Init() OVERRIDE;
};

static CTerrorAmmoDefSystem s_TerrorAmmoDefSystem;

// S2CFEE0 is this real game-system Init, not a lazy per-weapon parser.
bool CTerrorAmmoDefSystem::Init()
{
	CAmmoDef &def = s_TerrorAmmoDef;
	const uint32 nPistolDamageType = 0x00000002u;
	const uint32 nMagnumRifleDamageType = 0x80000002u;
	const uint32 nMachineGunDamageType = 0x81000002u;
	const uint32 nShotgunDamageType = 0xA0000002u;

	// Ammo index zero remains reserved; preserve this exact order, 1..20.
	def.AddAmmoType( "AMMO_TYPE_PISTOL", static_cast<int>( nPistolDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_pistol_max", 2400.0f, 0, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_PISTOL_MAGNUM", static_cast<int>( nMagnumRifleDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_pistol_max", 2400.0f, 8, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_ASSAULTRIFLE", static_cast<int>( nMagnumRifleDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_assaultrifle_max", 2400.0f, 0, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_MINIGUN", static_cast<int>( nMachineGunDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_minigun_max", 2400.0f, 0, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_SMG", static_cast<int>( nPistolDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_smg_max", 2000.0f, 0, 5, 10 );
	def.AddAmmoType( "AMMO_TYPE_M60", static_cast<int>( nMachineGunDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_m60_max", 2400.0f, 0, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_SHOTGUN", static_cast<int>( nShotgunDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_shotgun_max", 800.0f, 0, 3, 6 );
	def.AddAmmoType( "AMMO_TYPE_AUTOSHOTGUN", static_cast<int>( nShotgunDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_autoshotgun_max", 800.0f, 0, 3, 6 );
	def.AddAmmoType( "AMMO_TYPE_HUNTINGRIFLE", static_cast<int>( nMagnumRifleDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_huntingrifle_max", 2400.0f, 4, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_SNIPERRIFLE", static_cast<int>( nMagnumRifleDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_sniperrifle_max", 2400.0f, 4, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_TURRET", static_cast<int>( nPistolDamageType ), TRACER_LINE,
		NULL, NULL, "ammo_turret_max", 2400.0f, 0, 10, 14 );
	def.AddAmmoType( "AMMO_TYPE_PIPEBOMB", 0x40, TRACER_LINE,
		NULL, NULL, "ammo_pipebomb_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_MOLOTOV", 0x40, TRACER_LINE,
		NULL, NULL, "ammo_molotov_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_VOMITJAR", 0x40, TRACER_LINE,
		NULL, NULL, "ammo_vomitjar_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_PAINPILLS", 0, TRACER_LINE,
		NULL, NULL, "ammo_painpills_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_FIRSTAID", 0, TRACER_LINE,
		NULL, NULL, "ammo_firstaid_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_GRENADELAUNCHER", 0x40, TRACER_NONE,
		NULL, NULL, "ammo_grenadelauncher_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_ADRENALINE", 0, TRACER_LINE,
		NULL, NULL, "ammo_adrenaline_max", 1.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_TYPE_CHAINSAW", 0, TRACER_NONE, 0, 0, 1, 0.0f, 0, 4, 8 );
	def.AddAmmoType( "AMMO_CARRIED_ITEM", 0, TRACER_NONE, 0, 0, 1, 0.0f, 0, 4, 8 );
	return true;
}

CAmmoDef *GetAmmoDef()
{
	return &s_TerrorAmmoDef;
}
