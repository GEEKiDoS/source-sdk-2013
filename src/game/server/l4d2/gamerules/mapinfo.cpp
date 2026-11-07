//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/mapinfo.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_map_parameters, CMapInfo );
LINK_ENTITY_TO_CLASS( info_map_parameters_versus, CMapInfoVersus );
BEGIN_DATADESC( CMapInfo )
	DEFINE_INPUTFUNC( FIELD_VOID, "UpdateCvars", InputUpdateCvars ),
END_DATADESC()

ConVar director_scavenge_item_override( "director_scavenge_item_override", "0", 0x00004004, "Override map-specified item densities with cvar values for tuning" );
ConVar director_item_cluster_range( "director_item_cluster_range", "50", 0x00004004, "Scavenge items of the same kind that are this close to each other are considered a single 'cluster' for population purposes" );
ConVar director_weapon_cluster_range( "director_weapon_cluster_range", "100", 0x00004004, "Scavenge weapons within this range are selected to be of the same tier, and not contain duplicate types" );
ConVar director_finale_item_cluster_count( "director_finale_item_cluster_count", "3", 0x00004004, "How many clusters of items will be populated in the finale" );
static ConVar director_pain_pill_density( "director_pain_pill_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_molotov_density( "director_molotov_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_ammo_density( "director_ammo_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_pipe_bomb_density( "director_pipe_bomb_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_pistol_density( "director_pistol_density", "4", 0x00004004, "Items per 100 yards square" );
static ConVar director_gas_can_density( "director_gas_can_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_propane_tank_density( "director_propane_tank_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_oxygen_tank_density( "director_oxygen_tank_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_melee_weapon_density( "director_melee_weapon_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_adrenaline_density( "director_adrenaline_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_defibrillator_density( "director_defibrillator_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_vomitjar_density( "director_vomitjar_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_upgradepack_density( "director_upgradepack_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_super_weapon_density( "director_super_weapon_density", "6.48", 0x00004004, "Items per 100 yards square" );
static ConVar director_configurable_weapon_spawn_density( "director_configurable_weapon_spawn_density", "-1.0", 0x00004004, "Items per 100 yards square (-1 to spawn all)" );
static ConVar director_magnum_spawn_density( "director_magnum_spawn_density", "-1.0", 0x00004004, "Items per 100 yards square (-1 to spawn all)" );

static ConVar *s_parameterCvars[CMapInfo::PARAMETER_COUNT] =
{
	&director_pain_pill_density, &director_molotov_density, &director_ammo_density,
	&director_pipe_bomb_density, &director_pistol_density, &director_gas_can_density,
	&director_propane_tank_density, &director_oxygen_tank_density,
	&director_melee_weapon_density, &director_adrenaline_density, &director_defibrillator_density,
	&director_vomitjar_density, &director_upgradepack_density, &director_super_weapon_density,
	&director_configurable_weapon_spawn_density, &director_weapon_cluster_range,
	&director_magnum_spawn_density, &director_item_cluster_range
};
static const char *s_parameterKeys[CMapInfo::PARAMETER_COUNT] =
{
	"PainPillDensity", "MolotovDensity", "AmmoDensity", "PipeBombDensity", "PistolDensity",
	"GasCanDensity", "PropaneTankDensity", "OxygenTankDensity", "MeleeWeaponDensity",
	"AdrenalineDensity", "DefibrillatorDensity", "VomitJarDensity", "UpgradepackDensity",
	"ChainsawDensity", "ConfigurableWeaponDensity", "ConfigurableWeaponClusterRange",
	"MagnumDensity", "ItemClusterRange"
};
static CMapInfo *s_pMapInfo;
static CMapInfoVersus *s_pVersusMapInfo;

CMapInfo::CMapInfo()
{
	for ( int i = 0; i < PARAMETER_COUNT; ++i )
		m_parameters[i] = s_parameterCvars[i]->GetFloat();
	m_finaleItemClusterCount = director_finale_item_cluster_count.GetInt();
}
CMapInfo::~CMapInfo()
{
	if ( s_pMapInfo == this )
		s_pMapInfo = NULL;
}
CMapInfoVersus::~CMapInfoVersus()
{
	if ( s_pVersusMapInfo == this )
		s_pVersusMapInfo = NULL;
}
void CMapInfo::Spawn()
{
	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_NONE );
	AddEffects( EF_NODRAW );
	if ( !IsVersusMapInfo() )
	{
		if ( s_pMapInfo )
			Warning( "Warning: Multiple info_map_parameters entities in map!\n" );
		else
			s_pMapInfo = this;
	}
}
void CMapInfoVersus::Spawn()
{
	BaseClass::Spawn();
	// Native virtual800 is true here, so the base does NOT publish the
	// ordinary map-info pointer. Duplicate versus entities retain the FIRST.
	if ( s_pVersusMapInfo )
		Warning( "Warning: Multiple info_map_parameters_versus entities in map!\n" );
	else
		s_pVersusMapInfo = this;
}
bool CMapInfo::KeyValue( const char *key, const char *value )
{
	for ( int i = 0; i < PARAMETER_COUNT; ++i )
	{
		if ( !V_stricmp( key, s_parameterKeys[i] ) )
		{
			m_parameters[i] = atof( value );
			return true;
		}
	}
	if ( !V_stricmp( key, "FinaleItemClusterCount" ) )
	{
		m_finaleItemClusterCount = _mm_cvttsd_si32( _mm_set_sd( atof( value ) ) );
		return true;
	}
	return BaseClass::KeyValue( key, value );
}
float CMapInfo::GetParameter( Parameter parameter ) const
{
	return director_scavenge_item_override.GetBool() ? s_parameterCvars[parameter]->GetFloat() : m_parameters[parameter];
}
int CMapInfo::GetFinaleItemClusterCount() const
{
	return director_scavenge_item_override.GetBool() ? director_finale_item_cluster_count.GetInt() : m_finaleItemClusterCount;
}
void CMapInfo::InputUpdateCvars( inputdata_t &inputdata )
{
	director_scavenge_item_override.SetValue( 1 );
	for ( int i = 0; i < PARAMETER_COUNT; ++i )
		s_parameterCvars[i]->SetValue( m_parameters[i] );
	director_finale_item_cluster_count.SetValue( m_finaleItemClusterCount );
}
CMapInfo *GetMapInfo()
{
	if ( TerrorGameRules()->IsVersusMode() )
	{
		if ( s_pVersusMapInfo )
			return s_pVersusMapInfo;
		if ( s_pMapInfo )
			return s_pMapInfo;
		CBaseEntity::Create( "info_map_parameters_versus", vec3_origin, vec3_angle );
		return s_pVersusMapInfo;
	}
	if ( !s_pMapInfo )
		CBaseEntity::Create( "info_map_parameters", vec3_origin, vec3_angle );
	return s_pMapInfo;
}
