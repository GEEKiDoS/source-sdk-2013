//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "BasePropDoor.h"
#include "NextBot/NextBotVisionInterface.h"
#include "ispatialpartition.h"
#include "usermessages.h"

#include "tier0/memdbgon.h"

extern ConVar z_witch_attack_range;
extern ConVar z_witch_always_kills;

namespace
{
// S:0x41B300. Partition order is observable: at most the first eligible
// survivor is damaged, while every eligible non-player continues enumeration.
class WitchSlashEnumerator : public IPartitionEnumerator
{
public:
	WitchSlashEnumerator( Witch *witch, CBaseEntity *victim )
		: m_witch( witch ), m_victim( victim ), m_hit( false ), m_hitSurvivor( false )
	{
		AngleVectors( witch->GetAbsAngles(), &m_forward );
	}

	virtual IterationRetval_t EnumElement( IHandleEntity *handle ) OVERRIDE
	{
		CBaseEntity *entity = gEntList.GetBaseEntity( handle->GetRefEHandle() );
		if ( !entity || entity == m_witch )
			return ITERATION_CONTINUE;
		CTerrorPlayer *player = ToTerrorPlayer( entity );
		if ( entity->IsPlayer() )
		{
			if ( !m_victim || entity->entindex() != m_victim->entindex() || player->IsIncapacitated() )
				return ITERATION_CONTINUE;
			if ( DotProduct( m_forward, player->GetAbsOrigin() - m_witch->WorldSpaceCenter() ) < 0.0f )
				return ITERATION_CONTINUE;
		}
		if ( !m_witch->IsRangeLessThan( entity, z_witch_attack_range.GetFloat() ) ||
			!m_witch->GetVisionInterface()->IsLineOfSightClearToEntity( entity ) )
			return ITERATION_CONTINUE;

		float damage = 100.0f;
		if ( entity->MyCombatCharacterPointer() )
		{
			if ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			{
				if ( m_hitSurvivor )
					return ITERATION_CONTINUE;
				if ( player )
					player->SetAttackingWitch( m_witch );
				m_hitSurvivor = true;
			}
			entity->EmitSound( "WitchZombie.ShredVictim" );
			CPVSFilter filter( m_witch->WorldSpaceCenter() );
			filter.MakeReliable();
			UserMessageBegin( filter, "WitchBloodSplatter" );
				WRITE_VEC3COORD( m_witch->WorldSpaceCenter() );
			MessageEnd();
		}
		else if ( entity->ClassMatches( "func_door_rotating" ) || entity->ClassMatches( "prop_door*" ) )
		{
			CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( entity );
			if ( door )
				door->PlayPoundSound();
			else
				entity->EmitSound( "WoodenDoor.Pound" );
			damage = 1000.0f;
		}
		else
		{
			m_witch->EmitSound( "Zombie.ClawScrape" );
		}

		CTakeDamageInfo info( m_witch, m_witch, damage, DMG_SLASH );
		if ( z_witch_always_kills.GetBool() && TerrorGameRules()->GetDifficultyLevel() != 0 )
			info.AddDamageType( 0x8000 );
		Vector direction = entity->WorldSpaceCenter() - m_witch->EyePosition();
		VectorNormalize( direction );
		CalculateMeleeDamageForce( &info, direction, m_witch->EyePosition() );
		entity->TakeDamage( info );
		m_hit = true;
		return ITERATION_CONTINUE;
	}

	bool DidHit() const { return m_hit; }

private:
	Witch *m_witch;
	CBaseEntity *m_victim;
	Vector m_forward;
	bool m_hit;
	bool m_hitSurvivor;
};
}

// S:0x41B6C0. This is a sphere enumeration, not a replacement single hull trace.
bool Witch::Slash( CBaseEntity *victim )
{
	WitchSlashEnumerator slash( this, victim );
	partition->EnumerateElementsInSphere( PARTITION_ENGINE_SOLID_EDICTS,
		WorldSpaceCenter(), z_witch_attack_range.GetFloat(), false, &slash );
	if ( slash.DidHit() )
		return true;
	EmitSound( "Zombie.AttackMiss" );
	return false;
}
