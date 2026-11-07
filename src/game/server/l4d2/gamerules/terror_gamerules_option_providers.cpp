//========= Copyright Valve Corporation, All rights reserved. ============//
// Native Director options and the sole item/spawn-policy adapters.
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "tier0/memdbgon.h"

bool CTerrorGameRules::IsTankRun() const { return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_TankRun", false ); }
bool CTerrorGameRules::IsHeadshotOnly() const { return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_HeadshotOnly", false ); }
void CTerrorGameRules::SetScriptedEventCooldown( float time )
{
	// Native2DFE00 uses floating equality: equal signed zeros do not dirty, NaNs do.
	if ( m_flHoldoutCooldownEndTime.Get() != time )
		m_flHoldoutCooldownEndTime.SetDirect( time );
}
bool CTerrorGameRules::AllowSurvivorRescue() const { return TheDirector->AllowSurvivorRescue( IsCoopMode() ); }
bool CTerrorGameRules::CanPickupObject( CTerrorPlayer *, CBaseEntity *object ) const { return TheDirector->GetScriptedMode()->CanPickupObject( object ); }
bool CTerrorGameRules::ShouldApplyVomitJarBile() const { return IsRoundInProgress() || !IsSurvivalMode(); }

void CTerrorGameRules::RegisterWeaponSpawn( CWeaponSpawnConfigurable *spawn )
{
	TheDirector->GetItemManager()->RegisterWeaponSpawn( spawn );
}
void CTerrorGameRules::RemoveItemFromSpawnManager( CBaseEntity *item )
{
	TheDirector->GetItemManager()->RemoveItemFromSpawnManager( item );
}
bool CTerrorGameRules::CanSpawnCSWeapon( int weaponID, bool ignoreChance ) const
{
	return TheDirector->GetItemManager()->CanSpawnCSWeapon( weaponID, ignoreChance );
}
bool CTerrorGameRules::CanSpawnMeleeWeapon( const char *name ) const
{
	return TheDirector->GetItemManager()->CanSpawnMeleeWeapon( name );
}
void CTerrorGameRules::OnHealthCabinetOpened( CPropHealthCabinet *cabinet )
{
	TheDirector->GetItemManager()->OnHealthCabinetOpened( cabinet );
}
bool CTerrorGameRules::IsHealthCabinetPopulationSuppressed() const
{
	return TheDirector->IsInSecondHalfOfRound(); // Native Director1253, not rules1173.
}
bool CTerrorGameRules::GetHealthCabinetCheckpoint( const Vector &origin, Extent *bounds, bool *checkpointFlag ) const
{
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetCheckpoint( origin ); // S2578D0.
	if ( !checkpoint )
		return false;
	*bounds = checkpoint->GetBounds();
	*checkpointFlag = checkpoint->IsOutward(); // Native checkpoint95 / S2579E0.
	return true;
}
bool CTerrorGameRules::ShouldOpenHealthCabinetsInCheckpoint() const
{
	return TheDirector->IsChangingMapOrMission() && !g_bTerrorRestartAfterDeath && !TheDirector->IsRestoringByCharacter();
}
bool CTerrorGameRules::AllowWeaponSpawn( int weaponID ) const
{
	return TheDirector->GetScriptedMode()->AllowWeaponSpawn( weaponID );
}
bool CTerrorGameRules::GetWeaponSpawnReplacement( int weaponID, char *destination, int size ) const
{
	return TheDirector->GetScriptedMode()->ConvertWeaponSpawn( weaponID, destination, size );
}

bool CDirectorScriptedMode::AllowWeaponSpawn( int weaponID ) const
{
	if ( !m_bEnabled )
		return true;
	HSCRIPT scope = TheDirector->GetOptionsScope();
	HSCRIPT function = g_pScriptVM->LookupFunction( "AllowWeaponSpawn", scope );
	g_pScriptVM->ReleaseFunction( function );
	if ( !function )
		return true;
	// S279D20 probes/releases first, then S279450 performs a NEW lookup.
	// Native tests only the returned payload's low byte, not its type/status.
	ScriptVariant_t result;
	function = g_pScriptVM->LookupFunction( "AllowWeaponSpawn", scope );
	if ( function )
	{
		g_pScriptVM->Call( function, scope, true, &result, WeaponIDToClassname( weaponID ) );
		g_pScriptVM->ReleaseFunction( function );
	}
	const bool allowed = *reinterpret_cast< const unsigned char * >( &result ) != 0;
	result.Free();
	return allowed;
}

bool CDirectorScriptedMode::ConvertWeaponSpawn( int weaponID, char *destination, int size ) const
{
	if ( !m_bEnabled )
		return false;
	HSCRIPT scope = TheDirector->GetOptionsScope();
	HSCRIPT function = g_pScriptVM->LookupFunction( "ConvertWeaponSpawn", scope );
	g_pScriptVM->ReleaseFunction( function );
	if ( !function )
		return false;
	ScriptVariant_t result;
	function = g_pScriptVM->LookupFunction( "ConvertWeaponSpawn", scope );
	if ( function )
	{
		g_pScriptVM->Call( function, scope, true, &result, WeaponIDToClassname( weaponID ) );
		g_pScriptVM->ReleaseFunction( function );
	}
	const bool converted = result.GetType() == FIELD_CSTRING && (const char *)result != NULL;
	if ( converted )
		V_strncpy( destination, (const char *)result, size );
	result.Free();
	return converted;
}
