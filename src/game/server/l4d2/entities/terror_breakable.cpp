//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "obstacle_pushaway.h"
#include "func_break.h"
#include "func_breakablesurf.h"
#include "props.h"
#include "tier0/memdbgon.h"

// Native 12A440. This replaces the L4D2 body in obstacle_pushaway.cpp;
// it is not a second breakable policy alongside that function.
bool IsBreakableEntity( CBaseEntity *entity, bool allowInteraction17, bool allowInteraction18, const CTakeDamageInfo *damageInfo )
{
	if ( !entity || entity->m_takedamage != DAMAGE_YES )
		return false;
	int collisionGroup = entity->GetCollisionGroup();
	if ( collisionGroup != COLLISION_GROUP_PUSHAWAY && collisionGroup != COLLISION_GROUP_BREAKABLE_GLASS && collisionGroup != COLLISION_GROUP_NONE )
		return false;
	if ( entity->GetHealth() > 200 || entity->GetHealth() <= 0 || entity->IsSolidFlagSet( FSOLID_NOT_SOLID ) ||
		FClassnameIs( entity, "func_door" ) || FClassnameIs( entity, "func_door_rotating" ) ||
		( !allowInteraction17 && !allowInteraction18 && FClassnameIs( entity, "prop_fuel_barrel" ) ) )
		return false;
	if ( damageInfo && !entity->PassesDamageFilter( *damageInfo ) )
		return false;

	IMultiplayerPhysics *physics = dynamic_cast< IMultiplayerPhysics * >( entity );
	if ( physics )
	{
		if ( physics->GetMultiplayerPhysicsMode() != PHYSICS_MULTIPLAYER_SOLID )
			return false;
	}
	else if ( FClassnameIs( entity, "func_breakable" ) || FClassnameIs( entity, "func_breakable_surf" ) )
	{
		if ( FClassnameIs( entity, "func_breakable_surf" ) && static_cast< CBreakableSurface * >( entity )->m_bIsBroken )
			return false;
	}
	else if ( entity->PhysicsSolidMaskForEntity() & CONTENTS_PLAYERCLIP )
	{
		return false;
	}

	IBreakableWithPropData *propData = dynamic_cast< IBreakableWithPropData * >( entity );
	if ( propData )
	{
		// Native17/18 are authored only_breakable_by tank/mob model properties.
		if ( propData->GetDmgModBullet() <= 0.0f ||
			( !allowInteraction17 && propData->HasInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_TANK ) ) ||
			( !allowInteraction18 && propData->HasInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_MOB ) ) )
			return false;
		if ( damageInfo )
		{
			CTakeDamageInfo scaledInfo( *damageInfo );
			if ( scaledInfo.GetAttacker() )
				scaledInfo.ScaleDamage( scaledInfo.GetAttacker()->GetAttackDamageScale( entity ) );
			scaledInfo.ScaleDamage( entity->GetReceivedDamageScale( scaledInfo.GetAttacker() ) );

			// The SDK's two real server propdata implementers own the same
			// minimum-damage value as native IBreakableWithPropData v152.
			CBreakableProp *prop = dynamic_cast< CBreakableProp * >( entity );
			int minimumDamage;
			if ( prop )
				minimumDamage = prop->m_iMinHealthDmg;
			else
			{
				CBreakable *brush = dynamic_cast< CBreakable * >( entity );
				Assert( brush );
				minimumDamage = brush->GetMinHealthDamage();
			}
			if ( static_cast< float >( minimumDamage ) > scaledInfo.GetDamage() )
				return false;
		}
	}

	CBreakableProp *prop = dynamic_cast< CBreakableProp * >( entity );
	return prop == NULL || prop->m_iMinHealthDmg < 50;
}
