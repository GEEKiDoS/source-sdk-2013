//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "tier0/memdbgon.h"

// S7A9190. The holes are native; index zero is admitted only by melee_weapon.
static const char *s_itemClasses[] =
{
	"weapon_melee_spawn", "weapon_ammo_spawn", "weapon_first_aid_kit_spawn",
	"weapon_molotov_spawn", "weapon_pain_pills_spawn", "weapon_pipe_bomb_spawn",
	"models/props_equipment/oxygentank01.mdl", "models/props_junk/propanecanister001a.mdl",
	"models/props_junk/gascan001a.mdl", NULL, NULL, "weapon_adrenaline_spawn",
	"weapon_defibrillator_spawn", "weapon_vomitjar_spawn", NULL, NULL,
	"weapon_chainsaw_spawn", "weapon_grenade_launcher_spawn", "weapon_rifle_m60_spawn"
};

class CMultiSpawner : public CPointEntity
{
public:
	DECLARE_CLASS( CMultiSpawner, CPointEntity );
	DECLARE_DATADESC();
	CMultiSpawner();
	virtual bool KeyValue( const char *key, const char *value ) OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
private:
	CUtlVector< const char * > m_items;
	string_t m_iszMeleeWeapon;
	int m_nWeaponSkin;
	float m_flGlowRange;
};
LINK_ENTITY_TO_CLASS( weapon_item_spawn, CMultiSpawner );
BEGIN_DATADESC( CMultiSpawner )
	DEFINE_KEYFIELD( m_iszMeleeWeapon, FIELD_STRING, "melee_weapon" ),
	DEFINE_KEYFIELD( m_nWeaponSkin, FIELD_INTEGER, "weaponskin" ),
	DEFINE_KEYFIELD( m_flGlowRange, FIELD_FLOAT, "glowrange" ),
END_DATADESC()

class CUpgradeSpawner : public CPointEntity
{
public:
	DECLARE_CLASS( CUpgradeSpawner, CPointEntity );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
private:
	CUtlVector< const char * > m_items;
	bool m_bLaserSight;
	bool m_bUpgradePackIncendiary;
	bool m_bUpgradePackExplosive;
};
LINK_ENTITY_TO_CLASS( upgrade_spawn, CUpgradeSpawner );
BEGIN_DATADESC( CUpgradeSpawner )
	DEFINE_KEYFIELD( m_bLaserSight, FIELD_BOOLEAN, "laser_sight" ),
	DEFINE_KEYFIELD( m_bUpgradePackIncendiary, FIELD_BOOLEAN, "upgradepack_incendiary" ),
	DEFINE_KEYFIELD( m_bUpgradePackExplosive, FIELD_BOOLEAN, "upgradepack_explosive" ),
END_DATADESC()

CMultiSpawner::CMultiSpawner() : m_iszMeleeWeapon( NULL_STRING ), m_nWeaponSkin( -1 ), m_flGlowRange( 0.0f )
{
}

void CMultiSpawner::Precache()
{
}

// S29ECE0: case-sensitive four-byte prefix; nonzero item keys are consumed
// even when the suffix is invalid. Zero-valued keys still use the base path.
bool CMultiSpawner::KeyValue( const char *key, const char *value )
{
	if ( Q_strncmp( key, "item", 4 ) != 0 || atoi( value ) == 0 )
		return BaseClass::KeyValue( key, value );
	const unsigned int index = static_cast< unsigned int >( atoi( key + 4 ) );
	if ( index > 0 && index < ARRAYSIZE( s_itemClasses ) && s_itemClasses[index] )
		m_items.AddToTail( s_itemClasses[index] );
	return true;
}

// Both original bodies use a point trace, retain the old origin when startsolid,
// and later lift the created entity by origin.z - worldAABB.min.z, if positive.
static void GroundItemSpawner( CBaseEntity *spawner )
{
	Vector end = spawner->GetAbsOrigin();
	end.z -= 120.0f;
	trace_t trace;
	UTIL_TraceLine( spawner->GetAbsOrigin(), end, 0x46004003, spawner, COLLISION_GROUP_NONE, &trace );
	if ( !trace.startsolid )
		spawner->SetAbsOrigin( trace.endpos );
}
static void LiftSpawnedItem( CBaseEntity *item, bool setWeaponCollision )
{
	Vector mins, maxs;
	item->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	if ( setWeaponCollision )
		item->SetCollisionGroup( COLLISION_GROUP_WEAPON );
	const float lift = item->GetAbsOrigin().z - mins.z;
	if ( lift > 0.0f )
	{
		Vector origin = item->GetAbsOrigin();
		origin.z += lift;
		item->SetAbsOrigin( origin );
	}
}

void CMultiSpawner::Spawn()
{
	// S2A2880: this removal flag is checked before Precache/base/trace/random.
	if ( HasSpawnFlags( 0x8000 ) )
	{
		UTIL_Remove( this );
		return;
	}
	Precache();
	BaseClass::Spawn();
	GroundItemSpawner( this );
	if ( STRING( m_iszMeleeWeapon )[0] )
		m_items.AddToTail( s_itemClasses[0] );
	if ( m_items.Count() > 0 )
	{
		const char *selection = m_items[RandomInt( 0, m_items.Count() - 1 )];
		if ( selection )
		{
			char classname[256] = {};
			Q_strcpy( classname, Q_strncmp( selection, "models/", 7 ) == 0 ? "prop_physics" : selection );
			CBaseEntity *item = CreateEntityByName( classname );
			if ( !item )
			{
				Msg( "Failed to spawn item by class %s\n", classname );
				UTIL_Remove( this );
				return;
			}
			item->SetAbsOrigin( GetAbsOrigin() );
			item->SetAbsAngles( GetAbsAngles() );
			if ( FClassnameIs( item, "prop_physics" ) )
			{
				item->SetModel( selection );
				Vector origin = item->GetAbsOrigin();
				origin.z += 12.0f;
				item->SetAbsOrigin( origin );
			}
			else if ( FClassnameIs( item, "weapon_melee_spawn" ) )
			{
				CWeaponMeleeSpawn *melee = dynamic_cast< CWeaponMeleeSpawn * >( item );
				if ( melee )
				{
					melee->SetMeleeWeaponName( m_iszMeleeWeapon );
					QAngle angles = GetLocalAngles();
					angles.z -= 90.0f;
					melee->SetLocalAngles( angles );
				}
			}
			CWeaponSpawn *weapon = dynamic_cast< CWeaponSpawn * >( item );
			if ( weapon )
			{
				if ( HasSpawnFlags( 2 ) ) weapon->AddSpawnFlags( 2 );
				if ( HasSpawnFlags( 1 ) ) weapon->AddSpawnFlags( 1 );
				if ( HasSpawnFlags( 8 ) ) weapon->AddSpawnFlags( 8 );
				weapon->SetItemCount( 1 );
				weapon->SetWeaponSkin( m_nWeaponSkin );
				weapon->SetGlowRange( m_flGlowRange );
			}
			item->Precache();
			DispatchSpawn( item, true );
			LiftSpawnedItem( item, true );
		}
	}
	UTIL_Remove( this );
}

void CUpgradeSpawner::Spawn()
{
	// S2A12E0: the vector is not cleared on another explicit Spawn call.
	if ( m_bLaserSight ) m_items.AddToTail( "upgrade_laser_sight" );
	if ( m_bUpgradePackIncendiary ) m_items.AddToTail( "weapon_upgradepack_incendiary_spawn" );
	if ( m_bUpgradePackExplosive && !TerrorGameRules()->IsVersusMode() && !TerrorGameRules()->IsScavengeMode() )
		m_items.AddToTail( "weapon_upgradepack_explosive_spawn" );
	if ( m_items.Count() > 0 )
	{
		BaseClass::Spawn();
		GroundItemSpawner( this );
		const char *selection = m_items[RandomInt( 0, m_items.Count() - 1 )];
		if ( selection )
		{
			CBaseEntity *item = CreateEntityByName( selection );
			if ( !item )
			{
				Msg( "Failed to spawn item by class %s\n", selection );
				UTIL_Remove( this );
				return;
			}
			item->SetAbsOrigin( GetAbsOrigin() );
			item->SetAbsAngles( GetAbsAngles() );
			if ( HasSpawnFlags( 2 ) ) item->AddSpawnFlags( 2 );
			if ( HasSpawnFlags( 1 ) ) item->AddSpawnFlags( 1 );
			item->Precache();
			DispatchSpawn( item, true );
			LiftSpawnedItem( item, false );
		}
	}
	UTIL_Remove( this );
}
