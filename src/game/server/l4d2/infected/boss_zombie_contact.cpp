//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss_zombie_player_bot.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "world.h"
#include "props.h"
#include "obstacle_pushaway.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

void BossZombieContact( BossZombiePlayerBot *me, CBaseEntity *other, CGameTrace *trace, float lastStuckTime, bool replyForActor )
{
	static PressAltFireButtonReply reply;
	if ( !other ) return;
	bool door = IsBreakableEntity( other, me->GetTeamNumber() == TEAM_INFECTED && me->GetZombieClass() == 8, true, NULL ) ||
		FClassnameIs( other, "prop_door*" ) || FClassnameIs( other, "func_door*" );
	if ( !door && trace && other == GetWorldEntity() && trace->fraction < 0.6f && gpGlobals->curtime - lastStuckTime < 1.0f )
	{
		// The original expands XY by36, not the character hull or Z.
		Vector mins = me->GetAbsOrigin() - me->WorldAlignMins() - Vector( 36.0f, 36.0f, 0.0f );
		Vector maxs = me->GetAbsOrigin() + me->WorldAlignMaxs() + Vector( 36.0f, 36.0f, 0.0f );
		CBaseEntity *entities[128];
		int count = UTIL_EntitiesInBox( entities, ARRAYSIZE( entities ), mins, maxs, 0 );
		for ( int i = 0; i < count; ++i )
		{
			const char *name = entities[i]->GetClassname();
			if ( name && ( name[0] == 'p' || name[0] == 'f' ) &&
				( FClassnameIs( entities[i], "prop_door*" ) || FClassnameIs( entities[i], "func_door*" ) ) )
			{
				other = entities[i];
				door = true;
				break;
			}
		}
	}
	if ( door )
	{
		if ( !trace || trace->plane.normal.z < 0.8f )
			me->GetBodyInterface()->AimHeadTowards( other->WorldSpaceCenter(), IBody::CRITICAL, 1.0f, &reply, "Looking at door I'm trying to break", true, 0.25f );
	}
	else if ( other->MyCombatCharacterPointer() )
	{
		me->GetBodyInterface()->AimHeadTowards( other->WorldSpaceCenter(), IBody::IMPORTANT, 1.0f, replyForActor ? &reply : NULL, "Looking at actor I'm trying to attack", true, 0.25f );
	}
}

bool IsLowHealthBossBreakable( CBaseEntity *entity ) // S351C10
{
	const int collisionGroup = entity->GetCollisionGroup();
	return ( collisionGroup == COLLISION_GROUP_BREAKABLE_GLASS || collisionGroup == COLLISION_GROUP_NONE ) &&
		entity->m_takedamage == DAMAGE_YES && (float)entity->GetHealth() <= 10.0f &&
		( FClassnameIs( entity, "func_breakable_surf" ) || FClassnameIs( entity, "func_breakable" ) ||
		  dynamic_cast< CBreakableProp * >( entity ) != NULL );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
