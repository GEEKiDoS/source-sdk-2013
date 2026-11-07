//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/terror_transition_entities.h"
#include "l4d2/entities/info_changelevel.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "props.h"
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"

namespace
{
const Color transitionColor( 0, 255, 192, 255 );
void SaveVector( KeyValues *data, const char *key, const Vector &value )
{
	data->SetString( key, CFmtStr( "%f %f %f", value.x, value.y, value.z ) );
}
Vector LoadVector( KeyValues *data, const char *key )
{
	Vector result = vec3_origin;
	sscanf( data->GetString( key, "" ), "%f %f %f", &result.x, &result.y, &result.z );
	return result;
}
class SavedEntity
{
public:
	explicit SavedEntity( CBaseEntity *entity )
	{
		m_data = new KeyValues( "item" );
		m_data->SetString( "classname", entity->GetClassname() );
		SaveVector( m_data, "pos", entity->GetAbsOrigin() );
		const QAngle &angles = entity->GetAbsAngles();
		SaveVector( m_data, "angles", Vector( angles.x, angles.y, angles.z ) );
		SaveVector( m_data, "vel", entity->GetAbsVelocity() );
	}
	SavedEntity( const SavedEntity &other ) { m_data = other.m_data->MakeCopy(); }
	SavedEntity &operator=( const SavedEntity &other )
	{
		if ( this != &other ) { m_data->deleteThis(); m_data = other.m_data->MakeCopy(); }
		return *this;
	}
	virtual ~SavedEntity() { m_data->deleteThis(); }
	virtual void BeforeSpawn( CBaseEntity *entity )
	{
		entity->SetAbsOrigin( LoadVector( m_data, "pos" ) );
		Vector a = LoadVector( m_data, "angles" );
		entity->SetAbsAngles( QAngle( a.x, a.y, a.z ) );
		entity->SetAbsVelocity( LoadVector( m_data, "vel" ) );
	}
	virtual void AfterSpawn( CBaseEntity *entity ) {} // S:253910, real empty base callback
	void Restore()
	{
		Vector offset = GetTerrorTransitionOffset();
		Vector origin = LoadVector( m_data, "pos" ) + offset;
		Vector a = LoadVector( m_data, "angles" );
		QAngle angles( a.x, a.y, a.z );
		if ( !m_data->GetInt( "positionFixedUp", 0 ) )
		{
			FixupTerrorTransitionPosition( origin, angles );
			SaveVector( m_data, "angles", Vector( angles.x, angles.y, angles.z ) );
			m_data->SetInt( "positionFixedUp", 1 );
		}
		SaveVector( m_data, "pos", origin );
		const char *classname = m_data->GetString( "classname", "" );
		ConColorMsg( transitionColor, "Recreating %s at %.0f, %.0f, %.0f\n", classname, origin.x, origin.y, origin.z );
		CBaseEntity *entity = CreateEntityByName( classname );
		// Main-approved S254320/S254390 factory-failure adaptation: skip only
		// this record's entity restore; outer loops/counts/order are unchanged.
		if ( !entity ) return;
		BeforeSpawn( entity );
		DispatchSpawn( entity, true ); // Native S209580 explicit bRunVScripts=1.
		AfterSpawn( entity );
	}
	const char *GetModel() const { return m_data->GetString( "model", "" ); }
protected:
	KeyValues *m_data;
};
class SavedPropPhysics : public SavedEntity
{
public:
	explicit SavedPropPhysics( CPhysicsProp *entity ) : SavedEntity( entity )
	{
		m_data->SetString( "model", STRING( entity->GetModelName() ) );
	}
	virtual void BeforeSpawn( CBaseEntity *entity ) OVERRIDE
	{
		SavedEntity::BeforeSpawn( entity );
		ConColorMsg( transitionColor, "Restoring prop_physics with model %s\n", GetModel() );
		entity->SetModelName( AllocPooledString( GetModel() ) );
	}
};
class SavedWeapon : public SavedEntity
{
public:
	explicit SavedWeapon( CWeaponCSBase *weapon ) : SavedEntity( weapon )
	{
		const Vector &origin = weapon->GetAbsOrigin();
		ConColorMsg( transitionColor, "Saving %s at %.0f, %.0f, %.0f\n", weapon->GetClassname(), origin.x, origin.y, origin.z );
		m_data->SetInt( "currentMagazine", weapon->Clip1() );
		m_data->SetInt( "extraAmmo", weapon->GetExtraPrimaryAmmo() );
		m_data->SetInt( "weaponskin", weapon->m_nSkin );
	}
	virtual void AfterSpawn( CBaseEntity *entity ) OVERRIDE
	{
		CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( entity );
		if ( weapon )
		{
			weapon->m_iClip1 = m_data->GetInt( "currentMagazine", 0 );
			weapon->SetExtraPrimaryAmmo( m_data->GetInt( "extraAmmo", 0 ) );
			weapon->m_nSkin = m_data->GetInt( "weaponskin", 0 );
		}
		else
		{
			Vector origin = LoadVector( m_data, "pos" );
			ConColorMsg( transitionColor, "Removing bad weapon %s at %.0f, %.0f, %.0f\n", m_data->GetString( "classname", "" ), origin.x, origin.y, origin.z );
			UTIL_Remove( entity );
		}
	}
};
class SavedTerrorGun : public SavedWeapon
{
public:
	explicit SavedTerrorGun( CTerrorGun *gun ) : SavedWeapon( gun )
	{
		m_data->SetInt( "dual", gun->IsDualWielding() );
		m_data->SetInt( "reloaded", !gun->m_inInitialPickup );
		m_upgradeBits = gun->m_upgradeBitVec;
		// Retail saves byte6024, not a bool and not the whole target int.
		m_upgradedAmmo = static_cast< unsigned char >( gun->m_nUpgradedPrimaryAmmoLoaded.Get() );
	}
	virtual void BeforeSpawn( CBaseEntity *entity ) OVERRIDE
	{
		SavedEntity::BeforeSpawn( entity );
		CTerrorGun *gun = dynamic_cast< CTerrorGun * >( entity );
		if ( gun && !m_data->GetInt( "reloaded", 0 ) ) gun->m_inInitialPickup = true;
	}
	virtual void AfterSpawn( CBaseEntity *entity ) OVERRIDE
	{
		CTerrorGun *gun = dynamic_cast< CTerrorGun * >( entity );
		if ( gun )
		{
			if ( m_data->GetInt( "dual", 0 ) ) gun->AcquireDualWeapons();
			gun->m_upgradeBitVec = m_upgradeBits;
			gun->m_nUpgradedPrimaryAmmoLoaded = m_upgradedAmmo;
		}
		SavedWeapon::AfterSpawn( entity );
	}
private:
	int m_upgradedAmmo;
	int m_upgradeBits;
};
class SavedTerrorMeleeWeapon : public SavedWeapon
{
public:
	explicit SavedTerrorMeleeWeapon( CTerrorMeleeWeapon *weapon ) : SavedWeapon( weapon )
	{
		m_data->SetInt( "melee_weapon", weapon->GetMeleeWeaponInfoHandle() );
	}
	virtual void BeforeSpawn( CBaseEntity *entity ) OVERRIDE
	{
		CTerrorMeleeWeapon *weapon = dynamic_cast< CTerrorMeleeWeapon * >( entity );
		MeleeWeaponInfoHandle_t handle = m_data->GetInt( "melee_weapon", 0 );
		if ( weapon && handle != INVALID_MELEE_WEAPON_INFO_HANDLE ) weapon->SetMeleeWeaponInfo( handle );
		SavedEntity::BeforeSpawn( entity );
	}
};
class SavedWeaponSpawn : public SavedEntity
{
public:
	explicit SavedWeaponSpawn( CWeaponSpawn *spawn ) : SavedEntity( spawn )
	{
		const Vector &origin = spawn->GetAbsOrigin();
		ConColorMsg( transitionColor, "Saving %s at %.0f, %.0f, %.0f (model %s)\n", spawn->GetClassname(), origin.x, origin.y, origin.z, STRING( spawn->GetModelName() ) );
		m_data->SetInt( "itemCount", spawn->GetItemCount() );
		m_data->SetInt( "shadow", !spawn->IsEffectActive( EF_NOSHADOW ) );
		m_data->SetString( "model", STRING( spawn->GetModelName() ) );
		m_data->SetInt( "weaponID", spawn->GetWeaponID() );
		m_data->SetInt( "weaponskin", spawn->GetWeaponSkin() );
	}
	virtual void BeforeSpawn( CBaseEntity *entity ) OVERRIDE
	{
		SavedEntity::BeforeSpawn( entity );
		CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( entity );
		if ( spawn )
		{
			spawn->SetItemCount( m_data->GetInt( "itemCount", 0 ) );
			spawn->SetModelName( AllocPooledString( GetModel() ) );
			spawn->SetWeaponID( m_data->GetInt( "weaponID", 0 ) );
			spawn->SetWeaponSkin( m_data->GetInt( "weaponskin", 0 ) );
		}
		else
		{
			Vector origin = LoadVector( m_data, "pos" );
			ConColorMsg( transitionColor, "Removing bad weapon spawn %s at %.0f, %.0f, %.0f\n", m_data->GetString( "classname", "" ), origin.x, origin.y, origin.z );
			UTIL_Remove( entity );
		}
	}
	virtual void AfterSpawn( CBaseEntity *entity ) OVERRIDE
	{
		if ( !m_data->GetInt( "shadow", 0 ) ) entity->AddEffects( EF_NOSHADOW );
	}
};
class SavedMeleeWeaponSpawn : public SavedWeaponSpawn
{
public:
	explicit SavedMeleeWeaponSpawn( CWeaponMeleeSpawn *spawn ) : SavedWeaponSpawn( spawn )
	{
		m_data->SetInt( "melee_weapon", spawn->GetMeleeWeaponInfoHandle() );
	}
	virtual void BeforeSpawn( CBaseEntity *entity ) OVERRIDE
	{
		SavedWeaponSpawn::BeforeSpawn( entity );
		CWeaponMeleeSpawn *spawn = dynamic_cast< CWeaponMeleeSpawn * >( entity );
		MeleeWeaponInfoHandle_t handle = m_data->GetInt( "melee_weapon", 0 );
		if ( spawn && handle != INVALID_MELEE_WEAPON_INFO_HANDLE ) spawn->SetMeleeWeaponInfoHandle( handle );
	}
};
CUtlVector< SavedWeaponSpawn * > s_spawns;
CUtlVector< SavedWeapon * > s_weapons;
CUtlVector< SavedPropPhysics > s_props;
bool s_restorePending;

bool IsTransitionEntity( CBaseEntity *entity )
{
	int caps = entity->ObjectCaps();
	if ( caps < 0 || entity->IsPlayer() || entity->GetRootMoveParent()->IsPlayer() ) return false;
	if ( caps & FCAP_ACROSS_TRANSITION ) return true;
	if ( entity->m_iGlobalname != NULL_STRING && !entity->IsDormant() ) return true;
	// The original CBaseBackpackItem RTTI result is discarded on this path.
	return false;
}
bool IsCarriedPropModel( CBaseEntity *entity )
{
	static const char *models[] = { "models/props_junk/gascan001a.mdl", "models/props_equipment/oxygentank01.mdl", "models/props_junk/propanecanister001a.mdl", "models/props_junk/gnome.mdl" };
	for ( int i = 0; i < ARRAYSIZE( models ); ++i ) if ( !Q_stricmp( STRING( entity->GetModelName() ), models[i] ) ) return true;
	return false;
}
class WeaponSpawnEnum : public CFlaggedEntitiesEnum
{
public:
	WeaponSpawnEnum( CBaseEntity **entities, int count ) : CFlaggedEntitiesEnum( entities, count, 0 ) {}
	virtual IterationRetval_t EnumElement( IHandleEntity *handle ) OVERRIDE
{
	IHandleEntity *live = handle->GetRefEHandle().Get();
	if ( !live )
		return ITERATION_CONTINUE;
	CBaseEntity *entity = EntityFromEntityHandle( live );
	return dynamic_cast< CWeaponSpawn * >( entity ) ? CFlaggedEntitiesEnum::EnumElement( handle ) : ITERATION_CONTINUE;
}
};
}
void SetTerrorTransitionEntityRestorePending( bool pending )
{
	s_restorePending = pending;
}
void ClearTerrorTransitionEntities()
{
	s_weapons.PurgeAndDeleteElements();
	s_spawns.PurgeAndDeleteElements();
	s_props.Purge();
	s_restorePending = false;
	ClearTerrorTransitionLandmark();
}
void SaveTerrorTransitionEntities( InfoChangelevel *changelevel )
{
	// Native clear order is weapons, spawns, props; it does NOT clear landmark.
	s_weapons.PurgeAndDeleteElements();
	s_spawns.PurgeAndDeleteElements();
	s_props.Purge();
	s_restorePending = true;
	CBaseEntity *landmark = gEntList.FindEntityByName( NULL, changelevel->GetLandmarkName() );
	for ( CBaseEntity *entity = UTIL_EntitiesInPVS( landmark, NULL ); entity; entity = UTIL_EntitiesInPVS( landmark, entity ) )
	{
		if ( entity->IsMarkedForDeletion() || !IsTransitionEntity( entity ) ) continue;
		CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( entity );
		if ( weapon )
		{
			CTerrorGun *gun = dynamic_cast< CTerrorGun * >( weapon );
			if ( gun ) s_weapons.AddToTail( new SavedTerrorGun( gun ) );
			else
			{
				CTerrorMeleeWeapon *melee = dynamic_cast< CTerrorMeleeWeapon * >( weapon );
				if ( melee ) s_weapons.AddToTail( new SavedTerrorMeleeWeapon( melee ) );
				else s_weapons.AddToTail( new SavedWeapon( weapon ) );
			}
			continue;
		}
		CWeaponMeleeSpawn *meleeSpawn = dynamic_cast< CWeaponMeleeSpawn * >( entity );
		if ( meleeSpawn ) { s_spawns.AddToTail( new SavedMeleeWeaponSpawn( meleeSpawn ) ); continue; }
		CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( entity );
		if ( spawn ) { s_spawns.AddToTail( new SavedWeaponSpawn( spawn ) ); continue; }
		CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( entity );
		if ( prop && changelevel->IsTouching( prop ) && IsCarriedPropModel( prop ) )
		{
			SavedPropPhysics saved( prop );
			s_props.AddToTail( saved );
		}
	}
}
void PrecacheTerrorTransitionEntities()
{
	FOR_EACH_VEC( s_spawns, i ) if ( s_spawns[i] ) CBaseEntity::PrecacheModel( s_spawns[i]->GetModel() );
}
void RestoreTerrorTransitionEntities()
{
	if ( !s_restorePending || !( s_spawns.Count() || s_weapons.Count() || s_props.Count() ) ) return;
	s_restorePending = false;
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
	if ( checkpoint )
	{
		Vector lo = checkpoint->GetBounds().lo - Vector( 150, 150, 150 );
		Vector hi = checkpoint->GetBounds().hi + Vector( 150, 150, 150 );
		CBaseEntity *entities[128];
		WeaponSpawnEnum enumerator( entities, ARRAYSIZE( entities ) );
		int count = UTIL_EntitiesInBox( lo, hi, &enumerator );
		ConColorMsg( transitionColor, "Checkpoint bounds: %f %f %f --> %f %f %f and has %d weapon spawns\n", lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, count );
		for ( int i = 0; i < count; ++i )
		{
			ConColorMsg( transitionColor, "Removing %s\n", entities[i]->GetClassname() );
			UTIL_Remove( entities[i] );
		}
	}
	else ConColorMsg( transitionColor, "Failed to find checkpoint\n" );
	ConColorMsg( transitionColor, "Restoring %d transitioned weapon spawns\n", s_spawns.Count() );
	FOR_EACH_VEC( s_spawns, i ) s_spawns[i]->Restore();
	ConColorMsg( transitionColor, "Restoring %d transitioned weapons\n", s_weapons.Count() );
	FOR_EACH_VEC( s_weapons, i ) s_weapons[i]->Restore();
	ConColorMsg( transitionColor, "Restoring %d transitioned prop_physics\n", s_props.Count() );
	FOR_EACH_VEC( s_props, i ) s_props[i].Restore();
}
