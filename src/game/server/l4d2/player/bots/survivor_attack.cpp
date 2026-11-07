//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_intention.h"
#include "l4d2/player/bots/survivor_team_situation.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_body.h"
#include "l4d2/player/bots/survivor_vision.h"
#include "l4d2/player/bots/survivor_locomotion.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch.h"
#include "nav_mesh.h"
#include "NextBot/NextBotKnownEntity.h"
#include "NextBot/NextBotDebug.h"
#include "NextBot/NextBotUtil.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier0/vprof.h"
#include "tier0/memdbgon.h"

ConVar sb_dont_shoot( "sb_dont_shoot", "0", 0x00004004, "Force SurvivorBots to not fire their weapons" );
ConVar sb_sidestep_for_horde( "sb_sidestep_for_horde", "0", 0x00004004, "Allow sidestepping left/right to acquire common infected targets" );
ConVar sb_vomit_blind_time( "sb_vomit_blind_time", "5", 0x00004004, "How long Boomer vomit/explosion gore blinds us" );
ConVar sb_allow_shoot_through_survivors( "sb_allow_shoot_through_survivors", "1", 0x00004004, "Allow survivorbots to shoot through fellow survivors" );
ConVar sb_melee_approach_victim( "sb_melee_approach_victim", "0", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );
ConVar sb_close_threat_range( "sb_close_threat_range", "200", 0x00004004 );

extern ConVar z_exploding_outer_radius;
extern ConVar z_witch_wander_hear_radius;
extern ConVar car_alarm_distance;

namespace
{
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
	double NativeCombatNormalize( Vector &vector )
	{
		// S4EA090 returns the wide length, but normalizes with its F32 copy.
		const double length = sqrt( ( (double)vector.x * vector.x + (double)vector.y * vector.y ) + (double)vector.z * vector.z );
		const float roundedLength = (float)length;
		const float inverse = 1.0f / ( roundedLength + FLT_EPSILON );
		vector.x *= inverse;
		vector.y *= inverse;
		vector.z *= inverse;
		return length;
	}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

	bool IsMeleeWeapon( SurvivorBot *me )
	{
		return me->IsUsingWeaponType( TERROR_WEAPONTYPE_MELEE ) || me->IsUsingWeaponType( TERROR_WEAPONTYPE_CHAINSAW );
	}

	bool IsVomitBlinded( SurvivorBot *me )
	{
		return me->IsIT() && sb_vomit_blind_time.GetFloat() > me->m_itTimer.GetElapsedTime();
	}


	class BoomerFilter : public INextBotEntityFilter
	{
	public:
		virtual bool IsAllowed( CBaseEntity *entity ) const OVERRIDE
		{
			return entity && entity->IsPlayer() && entity->IsAlive() && entity->GetTeamNumber() == TEAM_INFECTED &&
				static_cast< CTerrorPlayer * >( entity )->GetZombieClass() == 2;
		}
	};


	bool NotFiring( SurvivorBot *me, const char *reason )
	{
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s: NOT FIRING because %s\n", gpGlobals->curtime, me->GetDebugIdentifier(), reason );
		return false;
	}
}

SurvivorAttack::SurvivorAttack()
{
	m_triggerDown = false;
	m_targetFrame = 0;
	m_sidestepLeft = false;
	m_enemyInjuryCount = 0;
	m_isApproaching = false;
}

ActionResult< SurvivorBot > SurvivorAttack::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_targetFrame = 0;
	m_isApproaching = false;
	SelectWeapon( me );
	m_enemyInjuryCount = 0;
	m_enemyInjuryDecayTimer.Start( 0.9f );
	m_triggerDown = false;
	m_target = NULL;
	m_targetFrame = 0;
	return Continue();
}

void SurvivorAttack::StopApproaching( SurvivorBot *me )
{
	if ( m_isApproaching )
	{
		me->GetIntentionInterface()->PopLegsAction();
		m_isApproaching = false;
	}
}

void SurvivorAttack::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	StopApproaching( me );
}

ActionResult< SurvivorBot > SurvivorAttack::OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	StopApproaching( me );
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorAttack::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED )
	{
		++m_enemyInjuryCount;
		m_enemyInjuryDecayTimer.Reset();
	}
	return TryToSustain( RESULT_TRY );
}

EventDesiredResult< SurvivorBot > SurvivorAttack::OnThreatChanged( SurvivorBot *me, CBaseCombatCharacter *threat )
{
	return TryToSustain( RESULT_TRY );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
CBaseCombatCharacter *SurvivorAttack::SelectTarget( SurvivorBot *me )
{
	if ( IsVomitBlinded( me ) )
		return NULL;

	if ( m_targetFrame != gpGlobals->framecount )
	{
		Vector forward;
		me->EyeVectors( &forward );
		CBaseCombatCharacter *selected = NULL;
		CBaseCombatCharacter *farTarget = NULL;
		float bestScore = FLT_MAX;
		const CUtlVector< SurvivorKnownEntity > &known = me->GetIntentionInterface()->GetKnownThreats();
		for ( int i = 0; i < known.Count(); ++i )
		{
			CBaseCombatCharacter *candidate = known[i].GetEntity();
			if ( !candidate || !candidate->IsAlive() )
				continue;
			if ( me->IsRangeLessThan( candidate, sb_close_threat_range.GetFloat() ) )
			{
				Vector direction = candidate->EyePosition() - me->EyePosition();
				direction.z = 0.0f;
				NativeCombatNormalize( direction );
				float score = 1.0f - ( DotProduct( forward, direction ) + 1.0f ) * 0.5f;
				if ( score < bestScore && me->GetVisionInterface()->IsLineOfSightClearToEntity( candidate ) )
				{
					selected = candidate;
					bestScore = score;
				}
			}
			else if ( !farTarget && me->GetVisionInterface()->IsLineOfSightClearToEntity( candidate ) )
			{
				farTarget = candidate;
			}
		}
		CBaseCombatCharacter *previous = m_target.Get();
		m_target = selected ? selected : farTarget;
		if ( m_isApproaching && previous != m_target.Get() )
			StopApproaching( me );
		m_targetFrame = gpGlobals->framecount;
	}
	return m_target.Get();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void SurvivorAttack::AimAtTarget( SurvivorBot *me )
{
	CBaseCombatCharacter *target = SelectTarget( me );
	if ( target )
	{
		IBody::LookAtPriorityType priority = me->GetIntentionInterface()->IsImmediateThreat( me, target ) ? IBody::IMPORTANT : IBody::INTERESTING;
		me->GetBodyInterface()->AimHeadTowards( target, priority, 3.0f, NULL, "Aiming at selected target", true, 0.25f );
	}
}

void SurvivorAttack::SelectWeapon( SurvivorBot *me )
{
	CBaseCombatWeapon *active = me->GetActiveWeapon();
	if ( !active || ( me->GetWeaponSwitchAttemptTimer().HasStarted() && me->GetWeaponSwitchAttemptTimer().GetElapsedTime() < 5.0f ) )
		return;

	CBaseCombatWeapon *primary = me->Weapon_GetSlot( 0 );
	CBaseCombatWeapon *secondary = me->Weapon_GetSlot( 1 );
	CBaseCombatWeapon *pistol = NULL;
	CBaseCombatWeapon *melee = NULL;
	if ( secondary )
	{
		TerrorWeaponType_t type = static_cast< CWeaponCSBase * >( secondary )->GetCSWpnData().m_WeaponType;
		if ( type == TERROR_WEAPONTYPE_PISTOL )
			pistol = secondary;
		else if ( type == TERROR_WEAPONTYPE_MELEE || type == TERROR_WEAPONTYPE_CHAINSAW )
			melee = secondary;
	}
	bool allowMelee = !TheDirector->IsFinale();
	if ( !primary || !primary->HasAnyAmmo() )
	{
		me->Weapon_Switch( secondary, 0 );
		return;
	}

	CBaseCombatCharacter *target = SelectTarget( me );
	if ( target )
	{
		float range = me->GetRangeTo( target );
		TerrorWeaponType_t type = static_cast< CWeaponCSBase * >( primary )->GetCSWpnData().m_WeaponType;
		if ( type == TERROR_WEAPONTYPE_SHOTGUN )
		{
			if ( GetTerrorEntityZombieClass( target ) != 8 && GetTerrorEntityZombieClass( target ) != 7 )
			{
				if ( range > 750.0f && pistol )
				{
					me->Weapon_Switch( pistol, 0 );
					return;
				}
				if ( range < 180.0f && melee && allowMelee )
				{
					me->Weapon_Switch( melee, 0 );
					return;
				}
				if ( range >= 500.0f )
					return;
			}
			me->Weapon_Switch( primary, 0 );
			return;
		}
		if ( type == TERROR_WEAPONTYPE_SNIPER_RIFLE )
		{
			if ( GetTerrorEntityZombieClass( target ) == 8 || GetTerrorEntityZombieClass( target ) == 7 )
			{
				me->Weapon_Switch( primary, 0 );
				return;
			}
			if ( range < 300.0f && pistol )
			{
				me->Weapon_Switch( secondary, 0 );
				return;
			}
			if ( range < 180.0f && melee && allowMelee )
			{
				me->Weapon_Switch( melee, 0 );
				return;
			}
			if ( range > 500.0f )
				me->Weapon_Switch( primary, 0 );
			return;
		}
	}
	if ( active != primary && !active->m_bInReload )
		me->Weapon_Switch( primary, 0 );
}

void SurvivorAttack::Sidestep( SurvivorBot *me )
{
	SurvivorTeamSituation &team = me->GetTeamSituation();
	CBaseCombatCharacter *target = m_target.Get();
	if ( GetTerrorEntityZombieClass( target ) == 0 && !sb_sidestep_for_horde.GetBool() && !team.GetDownedHuman() && !team.GetDownedFriend() )
		return;
	if ( m_sidestepTimer.GetRemainingTime() <= 0.0f )
	{
		m_sidestepLeft = RandomInt( 0, 100 ) < 50;
		m_sidestepTimer.Start( RandomFloat( 2.0f, 3.0f ) );
	}
	SurvivorLocomotion *locomotion = me->GetLocomotionInterface();
	const Vector &feet = locomotion->GetFeet();
	const Vector &motion = locomotion->GetMotionVector();
	Vector side( -motion.y, motion.x, motion.z );
	Vector goal = m_sidestepLeft ? feet + side * 25.0f : feet - side * 25.0f;
	if ( !locomotion->HasPotentialGap( feet, goal ) )
	{
		if ( m_sidestepLeft )
			me->PressLeftButton();
		else
			me->PressRightButton();
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
bool SurvivorAttack::FireWeapon( SurvivorBot *me )
{
	VPROF_BUDGET( "SurvivorAttack::FireWeapon", "SurvivorAttack::FireWeapon" );
	SurvivorTeamSituation &team = me->GetTeamSituation();
	if ( sb_dont_shoot.GetBool() || me->m_reviveTarget.Get() || ( TerrorGameRules()->IsPlayerControlledZombies() && me->GetTongueOwner() ) )
		return false;
	CBaseCombatCharacter *target = SelectTarget( me );
	if ( !target || !target->IsAlive() )
		return NotFiring( me, "target is dead/gone" );
	int zombieClass = GetTerrorEntityZombieClass( target );
	if ( zombieClass == 7 )
	{
		Infected *witch = dynamic_cast< Infected * >( target );
		if ( !witch->GetBodyInterface()->IsArousal( IBody::INTENSE ) &&
			( !team.AreAllTeammatesBots() || me->GetBodyInterface()->GetHeadSteadyDuration() < 10.0f || TheDirector->GetWitchCount() > 1 ) )
			return NotFiring( me, "target Witch is calm" );
	}
	else if ( TheDirector->GetWitchCount() > 0 && IsNearLivingWitch( me, z_witch_wander_hear_radius.GetFloat() + 24.0f ) )
	{
		// The original emits this particular diagnostic without the debug gate.
		DevMsg( "%3.2f: %s: NOT FIRING because I am very close to a calm witch.\n", gpGlobals->curtime, me->GetDebugIdentifier() );
		return false;
	}

	Vector eye = me->EyePosition();
	Vector direction = target->WorldSpaceCenter() - eye;
	if ( zombieClass == 0 && TerrorGameRules()->IsHeadshotOnly() )
		direction = target->EyePosition() - eye;
	const double targetRange = NativeCombatNormalize( direction );
	float range = (float)targetRange;
	if ( targetRange > 80.0 && IsMeleeWeapon( me ) )
		return false;
	float steadyTime;
	if ( range < 500.0f )
		steadyTime = 0.3f;
	else if ( range > 3000.0f )
		steadyTime = 1.0f;
	else
		steadyTime = ( range - 500.0f ) * 0.7f * 0.0004f + 0.3f;
	bool closeTarget = range < 150.0f && me->GetVisionInterface()->IsInFieldOfView( target );
	if ( !closeTarget && ( !me->GetBodyInterface()->IsHeadSteady() || steadyTime > me->GetBodyInterface()->GetHeadSteadyDuration() ) )
		return NotFiring( me, "our head hasn't been still long enough" );
	const Vector &size = target->CollisionProp()->OBBSize();
	float width = size.x > size.y ? size.x : size.y;
	if ( !closeTarget )
	{
		const Vector &view = me->GetBodyInterface()->GetViewVector();
		// S3B4FCA..4FF8 keeps both the dot product and width/range in x87.
		const double aimDot = ( (double)view.y * direction.y + (double)view.x * direction.x ) + (double)view.z * direction.z;
		if ( cos( atan2( (double)width / (double)range, 1.0 ) ) > aimDot )
			return NotFiring( me, "our aim is off" );
	}

	if ( !me->IsIncapacitated() )
	{
		CTraceFilterSimple filter( me, COLLISION_GROUP_NONE );
		float hullSize = 2.0f;
		if ( me->IsUsingWeaponType( TERROR_WEAPONTYPE_SHOTGUN ) )
			hullSize = 4.0f;
		else if ( me->IsUsingWeaponType( TERROR_WEAPONTYPE_SNIPER_RIFLE ) )
		{
			hullSize = 1.0f;
			if ( range < 2000.0f )
				range = 2000.0f;
		}
		Vector forward;
		me->EyeVectors( &forward );
		trace_t trace;
		UTIL_TraceHull( me->EyePosition(), eye + forward * range, Vector( -hullSize, -hullSize, -hullSize ), Vector( hullSize, hullSize, hullSize ), 0x46004003, &filter, &trace );
		bool safe = true;
		if ( !sb_allow_shoot_through_survivors.GetBool() && trace.DidHitNonWorldEntity() && trace.m_pEnt->IsPlayer() && trace.m_pEnt->IsAlive() )
		{
			CTerrorPlayer *friendPlayer = static_cast< CTerrorPlayer * >( trace.m_pEnt );
			if ( friendPlayer->GetTeamNumber() == TEAM_SURVIVOR || friendPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			{
				// These are independent native pin/ledge states, not a generic
				// "helpless" predicate. Preserve the original exceptions.
				safe = ( friendPlayer->IsIncapacitated() && !friendPlayer->m_isHangingFromLedge && !friendPlayer->m_isHangingFromTongue ) ||
					friendPlayer->m_jockeyAttacker.Get() || friendPlayer->m_pummelAttacker.Get() || friendPlayer->m_pounceAttacker.Get() ||
					friendPlayer->GetTongueOwner() || friendPlayer->m_isHangingFromLedge;
			}
		}
		float alarmTolerance = me->IsUsingWeaponType( TERROR_WEAPONTYPE_SHOTGUN ) ? 0.5f : 0.8f;
		const CUtlVector< EHANDLE > &alarms = TheDirector->GetCarAlarms();
		for ( int i = 0; i < alarms.Count() && safe; ++i )
		{
			CBaseEntity *alarm = alarms[i].Get();
			if ( alarm && me->IsRangeLessThan( alarm, car_alarm_distance.GetFloat() * 1.1f ) )
			{
				Vector toAlarm = alarm->WorldSpaceCenter() - me->EyePosition();
				NativeCombatNormalize( toAlarm );
				if ( DotProduct( toAlarm, forward ) > alarmTolerance )
				{
					CNavArea *alarmArea = TheNavMesh->GetNearestNavArea( alarm, 5, 10000.0f );
					CNavArea *myArea = me->GetLastKnownArea();
					if ( alarmArea && myArea && myArea->IsPotentiallyVisible( alarmArea ) )
					{
						safe = false;
						NotFiring( me, "we might hit the car alarm" );
					}
				}
			}
		}
		if ( !me->IsIT() )
		{
			BoomerFilter boomerFilter;
			CTerrorPlayer *boomer = ToTerrorPlayer( me->GetVisionInterface()->GetClosestKnown( boomerFilter ) );
			if ( boomer && me->GetVisionInterface()->IsLookingAt( boomer, alarmTolerance ) && !IsSafeToPopBoomer( boomer ) )
			{
				NotFiring( me, "we would pop the Boomer on a teammate" );
				Sidestep( me );
				return false;
			}
		}
		if ( !safe )
		{
			Sidestep( me );
			return false;
		}
	}
	if ( !me->GetVisionInterface()->IsLineOfSightClearToEntity( target ) )
	{
		Sidestep( me );
		return false;
	}
	if ( me->IsUsingWeaponType( TERROR_WEAPONTYPE_SMG ) || me->IsUsingWeaponType( TERROR_WEAPONTYPE_RIFLE ) || me->IsUsingWeaponType( TERROR_WEAPONTYPE_CHAINSAW ) )
	{
		me->PressFireButton( RandomFloat( 0.1f, 0.15f ) );
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s: CONTINUOUS FIRE!\n", gpGlobals->curtime, me->GetDebugIdentifier() );
	}
	else
	{
		if ( m_triggerDown )
			me->ReleaseFireButton();
		else
			me->PressFireButton();
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s: %s\n", gpGlobals->curtime, me->GetDebugIdentifier(), m_triggerDown ? "Releasing trigger" : "FIRE!" );
		m_triggerDown = !m_triggerDown;
	}
	return true;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
ActionResult< SurvivorBot > SurvivorAttack::Update( SurvivorBot *me, float interval )
{
	if ( me->GetLocomotionInterface()->IsUsingLadder() )
	{
		if ( me->GetTimeSinceEnemyInjury() < 1.0f )
			me->PressJumpButton();
		return Continue();
	}
	if ( IsVomitBlinded( me ) )
	{
		if ( m_shoveTimer.GetRemainingTime() <= 0.0f )
		{
			m_shoveTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			me->PressAltFireButton();
			me->LookAround();
		}
		return Continue();
	}
	if ( sb_melee_approach_victim.GetBool() )
	{
		if ( IsMeleeWeapon( me ) && m_target.Get() )
		{
			if ( !m_isApproaching )
			{
				me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsApproach( m_target.Get() ) );
				m_isApproaching = true;
			}
		}
		else
		{
			StopApproaching( me );
		}
	}
	SurvivorTeamSituation &team = me->GetTeamSituation();
	CBaseCombatCharacter *target = SelectTarget( me );
	if ( target && me->IsDebugging( NEXTBOT_BEHAVIOR | NEXTBOT_LOOK_AT ) )
		NDebugOverlay::HorzArrow( me->WorldSpaceCenter(), target->WorldSpaceCenter(), 2.0f, 255, 0, 0, 255, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	SelectWeapon( me );
	AimAtTarget( me );
	if ( m_enemyInjuryDecayTimer.GetRemainingTime() <= 0.0f )
	{
		if ( m_enemyInjuryCount )
			--m_enemyInjuryCount;
		m_enemyInjuryDecayTimer.Reset();
	}
	if ( !target )
		return Continue();
	bool dangerous = me->GetIntentionInterface()->IsThreatDangerous( me, target );
	for ( int i = 0; !dangerous && i < team.GetTeammateCount(); ++i )
		dangerous = me->GetIntentionInterface()->IsThreatDangerous( team.GetTeammate( i ), target );
	if ( !dangerous )
	{
		NotFiring( me, "threat is not dangerous" );
		return Continue();
	}

	int zombieClass = GetTerrorEntityZombieClass( target );
	CTerrorPlayer *targetPlayer = ToTerrorPlayer( target );
	if ( zombieClass == 2 && targetPlayer && !targetPlayer->IsNearShoveLimit() && targetPlayer->m_stunTimer.IsElapsed() &&
		( me->GetAbsOrigin() - target->GetAbsOrigin() ).LengthSqr() < Square( z_exploding_outer_radius.GetFloat() * 0.8f ) && !IsMeleeWeapon( me ) )
		return SuspendFor( new SurvivorAmbushBoomer( targetPlayer ), "Ambushing a nearby Boomer" );

	bool canShove = me->GetVisionInterface()->IsInFieldOfView( target ) && zombieClass != 8 && zombieClass != 7 && !me->IsIncapacitated() && !me->m_reviveTarget.Get();
	bool considerShove = true;
	switch ( zombieClass )
	{
	case 2:
		considerShove = !targetPlayer->IsNearShoveLimit();
		break;
	case 3:
		if ( targetPlayer->IsPouncing() )
			considerShove = RandomFloat( 0.0f, 100.0f ) <= 25.0f;
		break;
	case 5:
		considerShove = RandomFloat( 0.0f, 100.0f ) <= 25.0f;
		break;
	case 6:
		considerShove = false;
		break;
	case 0:
		if ( m_shoveTimer.GetRemainingTime() > 0.0f )
		{
			CBaseCombatWeapon *active = me->GetActiveWeapon();
			if ( !active || !active->m_bInReload )
				considerShove = false;
		}
		break;
	}
	if ( considerShove && canShove && ( me->GetAbsOrigin() - target->GetAbsOrigin() ).LengthSqr() < 50.0f * 50.0f )
	{
		if ( zombieClass != 2 && IsMeleeWeapon( me ) )
			me->PressFireButton();
		else
			me->PressAltFireButton();
		me->GetBodyInterface()->AimHeadTowards( target, IBody::CRITICAL, 0.5f, NULL, "Looking at melee shove target", true, 0.25f );
		m_shoveTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		if ( m_enemyInjuryCount > 2 && team.GetTeammateCount() && me->GetVisionInterface()->GetKnownCount( TEAM_INFECTED, -1.0f ) > 3 )
			me->SpeakConcept( "SurvivorBotHelpOverwhelmed", 2, RandomFloat( 0.0f, 1.0f ), NULL );
	}
	FireWeapon( me );
	return Continue();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

QueryResultType SurvivorAttack::ShouldHurry( const INextBot *me ) const
{
	SurvivorBot *actor = static_cast< SurvivorBot * >( me->GetEntity() );
	const SurvivorKnownEntity *known = actor->GetIntentionInterface()->GetPrimaryKnownThreat();
	if ( !known || !known->GetEntity() || !known->GetEntity()->IsAlive() || !known->WasEverSeen() )
		return ANSWER_UNDEFINED;
	CBaseCombatCharacter *target = known->GetEntity();
	int zombieClass = GetTerrorEntityZombieClass( target );
	if ( zombieClass == 2 || zombieClass == 8 )
		return ANSWER_YES;
	if ( zombieClass == 7 )
	{
		Infected *witch = dynamic_cast< Infected * >( target );
		if ( witch && witch->GetBodyInterface()->IsArousal( IBody::INTENSE ) )
			return ANSWER_YES;
	}
	return ANSWER_UNDEFINED;
}

QueryResultType SurvivorAttack::ShouldAttack( const INextBot *me, CBaseCombatCharacter *threat ) const
{
	SurvivorBot *actor = static_cast< SurvivorBot * >( me->GetEntity() );
	const SurvivorKnownEntity *known = actor->GetIntentionInterface()->GetPrimaryKnownThreat();
	if ( known && known->GetEntity() && known->GetEntity()->IsAlive() && known->WasEverSeen() )
	{
		int zombieClass = GetTerrorEntityZombieClass( known->GetEntity() );
		if ( zombieClass == 2 || zombieClass == 8 || zombieClass == 7 )
			return ANSWER_NO;
	}
	return ANSWER_UNDEFINED;
}
	bool IsNearLivingWitch( SurvivorBot *me, float range )
	{
		const CUtlVector< Witch * > &witches = GetWitchList();
		for ( int i = 0; i < witches.Count(); ++i )
		{
			// The native list contains all Witches. There is deliberately no
			// arousal test here, despite the calling diagnostic saying "calm".
			Witch *witch = witches[i];
			if ( ( witch->GetAbsOrigin() - me->GetAbsOrigin() ).LengthSqr() < range * range && witch->IsAlive() )
				return true;
		}
		return false;
	}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
	bool IsSafeToPopBoomer( CTerrorPlayer *boomer )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !player || !player->edict() || !player->entindex() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR )
				continue;
			if ( !player->IsAlive() || player->IsIncapacitated() )
				continue;
			Vector direction = player->GetAbsOrigin() - boomer->GetAbsOrigin();
			const double range = NativeCombatNormalize( direction );
			if ( (double)z_exploding_outer_radius.GetFloat() * (double)1.1f <= range || direction.z < -0.7071f || !player->IsLineOfSightClear( boomer, CBaseCombatCharacter::IGNORE_ACTORS ) )
				continue;
			return false;
		}
		return true;
	}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
