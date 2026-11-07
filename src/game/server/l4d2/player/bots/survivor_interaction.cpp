//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_intention.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/NextBotUtil.h"
#include "ammodef.h"
#include "tier0/memdbgon.h"

extern ConVar player_use_radius;
extern ConVar sb_revive_friend_distance;
ConVar survivor_give_attract_timeout( "survivor_give_attract_timeout", "12", 0x00004004,
	"How long to try and attract a recipient for an item give before giving up" );

static bool IsInteractionSurvivorTeam( int team )
{
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}

static SurvivorIntention *InteractionIntention( SurvivorBot *me )
{
	return static_cast< SurvivorIntention * >( me->GetIntentionInterface() );
}

static void SpeakAboutPatient( SurvivorBot *me, const char *concept, CTerrorPlayer *patient )
{
	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "Subject:%s", patient ? GetSurvivorCharacterName( patient->GetSurvivorCharacter() ) : "" );
	me->SpeakIfAllowed( concept, 1, modifiers, NULL, 0, NULL );
}

// S:38BBF0. This is deliberately not an auto-switch/previous-weapon policy.
static void RestoreGun( SurvivorBot *me )
{
	CBaseCombatWeapon *primary = me->Weapon_GetSlot( 0 );
	if ( primary && primary->HasAnyAmmo() )
		me->Weapon_Switch( primary );
	else
		me->Weapon_Switch( me->Weapon_GetSlot( 1 ) );
}

static bool IsHoldingHealingItem( CTerrorPlayer *player )
{
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( player->GetActiveWeapon() );
	return weapon && weapon->GetTerrorWpnData().m_WeaponType == TERROR_WEAPONTYPE_HEALING;
}

// S:3B2EB0/3B3020. The zero vector, including an explicitly supplied zero
// position, selects the entity-following legs action in the original.
SurvivorUseObject::SurvivorUseObject( CBaseEntity *object ) : m_object( object ), m_usePosition( vec3_origin )
{
	m_usedTimer.Invalidate();
}

SurvivorUseObject::SurvivorUseObject( CBaseEntity *object, const Vector &usePosition ) : m_object( object ), m_usePosition( usePosition )
{
	m_usedTimer.Invalidate();
}

float SurvivorUseObject::GetUseRange( SurvivorBot *me, CBaseEntity *object ) const
{
	return 75.0f;
}

const Vector &SurvivorUseObject::GetUsePosition( SurvivorBot *me, CBaseEntity *object )
{
	// S:3B25D0 uses the collision OBB's nearest point, not entity origin.
	static Vector position;
	object->CollisionProp()->CalcNearestPoint( me->WorldSpaceCenter(), &position );
	return position;
}

bool SurvivorUseObject::HasUsedObject( SurvivorBot *me ) const
{
	return m_usedTimer.HasStarted();
}

bool SurvivorUseObject::IsUseComplete( SurvivorBot *me ) const
{
	return m_usedTimer.IsElapsed();
}

bool SurvivorUseObject::ShouldGiveUp( SurvivorBot *me )
{
	const SurvivorKnownEntity *threat = InteractionIntention( me )->GetPrimaryKnownThreat();
	if ( threat && threat->GetEntity() && me->IsImmediateThreat( threat->GetEntity() ) )
		return true;

	if ( m_reachabilityTimer.IsElapsed() )
	{
		m_reachabilityTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		if ( !me->IsReachable( m_object.Get() ) )
			return true;
	}
	return false;
}

ActionResult< SurvivorBot > SurvivorUseObject::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	if ( m_object.Get() )
	{
		Action< SurvivorBot > *legs = m_usePosition == vec3_origin ?
			new SurvivorLegsApproach( m_object.Get() ) : new SurvivorLegsApproach( m_usePosition );
		InteractionIntention( me )->PushLegsAction( legs );
	}
	return Continue();
}

void SurvivorUseObject::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	InteractionIntention( me )->PopLegsAction();
}

ActionResult< SurvivorBot > SurvivorUseObject::OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	return Done();
}

void SurvivorUseObject::OnObjectUsed( SurvivorBot *me )
{
	m_usedTimer.Start( 1.0f );
	InteractionIntention( me )->ChangeLegsAction( new SurvivorLegsWait( 0.0f ) );
}

ActionResult< SurvivorBot > SurvivorUseObject::Update( SurvivorBot *me, float interval )
{
	CBaseEntity *object = m_object.Get();
	if ( !object )
		return Done( "The object we wanted to use is gone" );
	if ( object->GetOwnerEntity() && object->GetOwnerEntity()->IsPlayer() )
		return Done( "The object we wanted to use is owned by someone" );
	if ( !( object->ObjectCaps() & FCAP_IMPULSE_USE ) && dynamic_cast< CWeaponSpawn * >( object ) )
		return Done( "The object we wanted to use is no longer usable" );
	if ( ShouldGiveUp( me ) )
		return Done( "Situation changed - giving up" );
	if ( HasUsedObject( me ) )
		return IsUseComplete( me ) ? Done( "Used object" ) : Continue();

	const Vector &position = GetUsePosition( me, object );
	float range = GetUseRange( me, object );
	if ( ( ( me->EyePosition() - position ).LengthSqr() >= range * range &&
		( me->GetAbsOrigin() - position ).LengthSqr() >= range * range ) || me->GetLocomotionInterface()->IsScrambling() )
	{
		me->UpdateAreaClear();
		me->ReloadCombatWeapon();
		return Continue();
	}

	IBody *body = me->GetBodyInterface();
	body->AimHeadTowards( position, IBody::MANDATORY, 1.0f, NULL, "Look at object to use", true, 0.25f );
	Vector direction = position - me->EyePosition();
	VectorNormalize( direction );
	if ( DotProduct( body->GetViewVector(), direction ) > 0.7f )
	{
		// S:3B2830 calls the real object's Use and then native player accounting.
		// In particular, do not replace this with an instant revive/pickup.
		object->Use( me, me, USE_ON, 0.0f );
		me->OnPlayerUsed( object, USE_TOGGLE );
		OnObjectUsed( me );
	}
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorUseObject::OnStuck( SurvivorBot *me )
{
	return TryDone( RESULT_CRITICAL, "Giving up because I'm stuck" );
}

EventDesiredResult< SurvivorBot > SurvivorUseObject::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( !attacker )
		return TryContinue();
	me->GetBodyInterface()->AimHeadTowards( attacker->WorldSpaceCenter(), IBody::IMPORTANT, 2.0f, NULL, "Looking at attacker", true, 0.25f );
	return TryDone( RESULT_CRITICAL );
}

Action< SurvivorBot > *SurvivorCollectObject::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttack;
}

bool SurvivorCollectObject::ShouldGiveUp( SurvivorBot *me )
{
	const SurvivorTeamSituation &situation = me->GetTeamSituation();
	// S:3AD2D0 omits jockey; do not broaden this into IsAnyoneDominated().
	if ( situation.GetTonguedFriend() || situation.GetPouncedFriend() || situation.GetPummeledFriend() || situation.GetDownedFriend() )
		return true;
	if ( TheDirector->GetTankCount() <= 0 && me->GetTimeSinceEnemyInjury() >= 2.0f )
		return SurvivorUseObject::ShouldGiveUp( me );

	CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( m_object.Get() );
	CBaseCombatWeapon *active = me->GetActiveWeapon();
	if ( spawn && spawn->GetWeaponID() == 54 && active )
	{
		int ammoType = active->GetPrimaryAmmoType();
		if ( (float)me->GetAmmoCount( ammoType ) < (float)GetAmmoDef()->MaxCarry( ammoType, me ) * 0.4f )
			return false;
	}
	return true;
}

EventDesiredResult< SurvivorBot > SurvivorCollectObject::OnMoveToFailure( SurvivorBot *me, const Path *path, MoveToFailureType reason )
{
	// S:3AD4B0 intentionally collects even when the path fails. This is an
	// original selection/reach bug, not a permission/range fallback to fix.
	CBaseEntity *object = m_object.Get();
	if ( object )
	{
		object->Use( me, me, USE_ON, 0.0f );
		me->OnPlayerUsed( object, USE_TOGGLE );
		return TryDone( RESULT_CRITICAL, "Stuck trying to collect an item" );
	}
	return TryDone( RESULT_CRITICAL, "Stuck trying to collect NULL item" );
}

SurvivorRescueBarricadedFriend::SurvivorRescueBarricadedFriend( CSurvivorRescue *rescue ) : SurvivorUseObject( rescue )
{
}

Action< SurvivorBot > *SurvivorRescueBarricadedFriend::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttack;
}

float SurvivorRescueBarricadedFriend::GetUseRange( SurvivorBot *me, CBaseEntity *object ) const
{
	return 0.0f;
}

bool SurvivorRescueBarricadedFriend::ShouldGiveUp( SurvivorBot *me )
{
	CSurvivorRescue *rescue = static_cast< CSurvivorRescue * >( m_object.Get() );
	return !rescue || !rescue->IsCallingForRescue() || SurvivorUseObject::ShouldGiveUp( me );
}

SurvivorReviveFriend::SurvivorReviveFriend( CTerrorPlayer *patient ) : SurvivorUseObject( patient )
{
}

Action< SurvivorBot > *SurvivorReviveFriend::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttack;
}

float SurvivorReviveFriend::GetUseRange( SurvivorBot *me, CBaseEntity *object ) const
{
	return sb_revive_friend_distance.GetFloat();
}

const Vector &SurvivorReviveFriend::GetUsePosition( SurvivorBot *me, CBaseEntity *object )
{
	CTerrorPlayer *patient = ToTerrorPlayer( object );
	if ( !patient || !patient->IsHangingFromLedge() )
		return object->WorldSpaceCenter();
	m_ledgeUsePosition = patient->m_hangPos.Get();
	m_ledgeUsePosition.x -= patient->m_hangNormal.Get().x * 32.0f;
	m_ledgeUsePosition.y -= patient->m_hangNormal.Get().y * 32.0f;
	m_ledgeUsePosition += TerrorGameRules()->GetViewVectors()->m_vView;
	return m_ledgeUsePosition;
}

ActionResult< SurvivorBot > SurvivorReviveFriend::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	if ( m_object.Get() )
		InteractionIntention( me )->PushLegsAction( new SurvivorLegsApproach( GetUsePosition( me, m_object.Get() ) ) );
	return Continue();
}

bool SurvivorReviveFriend::IsUseComplete( SurvivorBot *me ) const
{
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	return patient && patient->IsAlive() && !patient->IsIncapacitated();
}

bool SurvivorReviveFriend::ShouldGiveUp( SurvivorBot *me )
{
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	if ( !patient )
		return true;
	if ( !patient->IsAlive() )
	{
		SpeakAboutPatient( me, "SurvivorBotMovingToReviveFriendWhoDied", patient );
		return true;
	}
	if ( patient->IsBeingPounced() || patient->IsPummelVictim() || ( patient->IsTongueVictim() && !patient->IsHangingFromLedge() ) )
		return true;
	if ( patient->m_reviveOwner.Get() && me->m_reviveTarget.Get() != patient )
	{
		char modifiers[256];
		CTerrorPlayer *savior = ToTerrorPlayer( patient->m_reviveOwner.Get() );
		Q_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,Savior:%s", GetSurvivorCharacterName( patient->GetSurvivorCharacter() ), savior ? GetSurvivorCharacterName( savior->GetSurvivorCharacter() ) : "" );
		me->SpeakIfAllowed( "SurvivorBotSomeoneElseReviveFriend", 1, modifiers, NULL, 0, NULL );
		return true;
	}
	return HasUsedObject( me ) && !patient->m_reviveOwner.Get();
}

ActionResult< SurvivorBot > SurvivorReviveFriend::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	if ( patient )
	{
		if ( HasUsedObject( me ) )
			me->GetBodyInterface()->AimHeadTowards( patient->WorldSpaceCenter(), IBody::CRITICAL, 0.1f, NULL, "Looking at friend we are rescuing", true, 0.25f );
		if ( ( me->GetAbsOrigin() - GetUsePosition( me, patient ) ).LengthSqr() < 22500.0f )
			me->PressWalkButton();
	}
	return SurvivorUseObject::Update( me, interval );
}

void SurvivorReviveFriend::OnObjectUsed( SurvivorBot *me )
{
	SurvivorUseObject::OnObjectUsed( me );
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	if ( patient && IsInteractionSurvivorTeam( patient->GetTeamNumber() ) && (unsigned int)patient->GetSurvivorCharacter() <= 7 )
		me->GetFriendActionTimer( patient->GetSurvivorCharacter() ).Start( 10.0f );
}

EventDesiredResult< SurvivorBot > SurvivorReviveFriend::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	if ( info.GetAttacker() && info.GetAttacker()->GetTeamNumber() == TEAM_INFECTED && patient &&
		( patient->IsBot() || patient->GetHealth() >= 50 ) && me->GetProgressBarProgress() < 0.5f )
	{
		SpeakAboutPatient( me, "SurvivorBotAbandonedReviveDueToAttack", patient );
		return TryDone( RESULT_IMPORTANT, "Attacked while reviving friend" );
	}
	return SurvivorUseObject::OnInjured( me, info );
}

EventDesiredResult< SurvivorBot > SurvivorReviveFriend::OnSound( SurvivorBot *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	return TryToSustain( RESULT_IMPORTANT );
}

ActionResult< SurvivorBot > SurvivorHealSelf::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_waitTimer.Start( 10.0f );
	InteractionIntention( me )->PushLegsAction( new SurvivorLegsWait( 0.0f ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorHealSelf::Update( SurvivorBot *me, float interval )
{
	CTerrorWeapon *kit = me->GetWeaponOfType( 12 );
	if ( !kit )
		return Done( "I don't have a first aid kit" );
	if ( m_waitTimer.IsElapsed() )
		return Done( "Done waiting" );
	if ( !me->IsAlive() || me->IsIncapacitated() || me->IsBeingPounced() || me->IsTongueVictim() || me->IsIT() || me->GetTimeSinceEnemyInjury() < 3.0f )
		return Done( "I am dead, dying, or at the mercy of the infected" );
	if ( me->GetHealth() > 75 )
		return Done( "I've been healed already" );
	if ( me->IsUseActionTarget() && !me->IsPerformingSelfUseAction() )
		return Done( "Someone is healing me or giving me ammo" );
	if ( me->GetActiveWeapon() == kit )
		me->PressFireButton();
	else
		me->Weapon_Switch( kit );
	return Continue();
}

void SurvivorHealSelf::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	InteractionIntention( me )->PopLegsAction();
}

EventDesiredResult< SurvivorBot > SurvivorHealSelf::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	if ( info.GetAttacker() && IsInteractionSurvivorTeam( info.GetAttacker()->GetTeamNumber() ) )
		return TryContinue();
	return TryDone( RESULT_CRITICAL, "Injured while healing myself" );
}

EventDesiredResult< SurvivorBot > SurvivorHealSelf::OnShoved( SurvivorBot *me, CBaseEntity *pusher )
{
	return TryDone( RESULT_CRITICAL, "Shoved while healing myself" );
}

ActionResult< SurvivorBot > SurvivorTakePills::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_waitTimer.Start( 4.0f );
	InteractionIntention( me )->PushLegsAction( new SurvivorLegsWait( 0.0f ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorTakePills::Update( SurvivorBot *me, float interval )
{
	CBaseCombatWeapon *item = me->Weapon_GetSlot( 4 );
	if ( !item )
		return Done( "I don't have any pills or adrenaline" );
	if ( m_waitTimer.IsElapsed() )
		return Done( "Done waiting" );
	if ( me->GetActiveWeapon() == item )
		me->PressFireButton();
	else
		me->Weapon_Switch( item );
	return Continue();
}

void SurvivorTakePills::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	InteractionIntention( me )->PopLegsAction();
}

ActionResult< SurvivorBot > SurvivorHealFriend::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	InteractionIntention( me )->PushLegsAction( new SurvivorLegsApproach( m_patient.Get() ) );
	m_isApproaching = true;
	SpeakAboutPatient( me, "SurvivorBotMovingToHealFriend", m_patient.Get() );
	return Continue();
}

bool SurvivorHealFriend::IsPatientInUseTrace( SurvivorBot *me ) const
{
	CTerrorPlayer *patient = m_patient.Get();
	trace_t trace;
	CTraceFilterSimple filter( me, COLLISION_GROUP_NONE );
	UTIL_TraceLine( me->GetBodyInterface()->GetEyePosition(), patient->EyePosition(), 0x02006041 | patient->PhysicsSolidMaskForEntity(), &filter, &trace );
	return trace.fraction < 1.0f && trace.m_pEnt == patient;
}

ActionResult< SurvivorBot > SurvivorHealFriend::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *patient = m_patient.Get();
	if ( !patient )
		return Done( "Patient is NULL" );
	if ( !patient->IsAlive() || patient->IsIncapacitated() || patient->IsBeingPounced() || patient->IsTongueVictim() || patient->IsIT() || patient->GetTimeSinceEnemyInjury() < 3.0f )
	{
		SpeakAboutPatient( me, "SurvivorBotGiveUpHealingFriendInTrouble", patient );
		return Done( "My patient is dead, dying, or at the mercy of the infected" );
	}
	if ( patient->GetHealth() > 70 )
	{
		SpeakAboutPatient( me, "SurvivorBotGiveUpHealingFriendWhoIsHealthy", patient );
		return Done( "My patient has been healed" );
	}
	bool amHealingOther = me->IsPerformingUseAction( 1 ) && me->m_useActionTarget.Get() != me;
	if ( patient->IsUseActionTarget( 1 ) && !amHealingOther )
	{
		SpeakAboutPatient( me, "SurvivorBotGiveUpHealingFriendWhoSomeoneElseIsHealing", patient );
		return Done( "My patient is being healed by someone else" );
	}
	if ( !patient->IsUseActionTarget( 1 ) && amHealingOther )
	{
		SpeakAboutPatient( me, "SurvivorBotGiveUpHealingWrongFriend", patient );
		return Done( "I started healing the wrong friend!" );
	}
	if ( patient->IsUseActionTarget() && !( me->IsPerformingUseAction() && me->m_useActionTarget.Get() != me ) )
		return Done( "My patient is involved in a use action with someone else" );
	CTerrorWeapon *kit = me->GetWeaponOfType( 12 );
	if ( !kit )
	{
		SpeakAboutPatient( me, "SurvivorBotHealedFriend", patient );
		return Done( "First Aid Kit has been consumed" );
	}
	if ( IsHoldingHealingItem( patient ) )
		return Done( "My patient is holding a healing item, presumably to use on themselves" );
	me->Weapon_Switch( kit );
	float range = player_use_radius.GetFloat() * 0.8f;
	if ( ( me->GetAbsOrigin() - patient->GetAbsOrigin() ).LengthSqr() >= range * range || !IsPatientInUseTrace( me ) )
	{
		if ( !m_isApproaching )
		{
			InteractionIntention( me )->ChangeLegsAction( new SurvivorLegsApproach( patient ) );
			m_isApproaching = true;
		}
		return Continue();
	}
	if ( m_isApproaching )
	{
		InteractionIntention( me )->ChangeLegsAction( new SurvivorLegsWait( 0.0f ) );
		m_isApproaching = false;
	}
	me->GetBodyInterface()->AimHeadTowards( patient->EyePosition(), IBody::CRITICAL, 0.3f, NULL, "Looking at friend we are healing", true, 0.25f );
	me->GetLocomotionInterface()->ClearStuckStatus( "Standing still to heal friend" );
	if ( me->GetVisionInterface()->IsLookingAt( patient->EyePosition(), 0.9f ) )
		me->PressAltFireButton();
	return Continue();
}

void SurvivorHealFriend::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	InteractionIntention( me )->PopLegsAction();
	RestoreGun( me );
}

EventDesiredResult< SurvivorBot > SurvivorHealFriend::OnStuck( SurvivorBot *me )
{
	return TryDone( RESULT_CRITICAL, "Giving up because I'm stuck" );
}

EventDesiredResult< SurvivorBot > SurvivorHealFriend::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	if ( info.GetAttacker() && info.GetAttacker()->GetTeamNumber() == TEAM_INFECTED && me->GetProgressBarProgress() < 0.5f )
	{
		SpeakAboutPatient( me, "SurvivorBotAttackedWhileHealingFriend", m_patient.Get() );
		return TryDone( RESULT_IMPORTANT, "Attacked while healing friend" );
	}
	return TryContinue();
}

SurvivorGivePillsToFriend::SurvivorGivePillsToFriend( CTerrorPlayer *patient ) : SurvivorUseObject( patient )
{
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
ActionResult< SurvivorBot > SurvivorGivePillsToFriend::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	if ( !patient || !patient->IsAlive() )
		return Done( "Friend is dead and/or gone" );
	CBaseCombatWeapon *item = me->Weapon_GetSlot( 4 );
	if ( !item )
		return Done( "Our Pills/adrenaline we are trying to give away have vanished" );
	if ( patient->Weapon_GetSlot( 4 ) )
		return Done( "Our friend already has a pills slot item" );
	if ( patient->Weapon_GetSlot( 5 ) )
		return Done( "Our friend is carrying a prop and we don't want him to drop it" );
	if ( patient->IsInCombat( false ) )
		return Done( "Our friend is in combat" );
	if ( patient->IsIncapacitated() || patient->IsBeingPounced() || patient->IsPummelVictim() || patient->IsTongueVictim() || patient->IsIT() || patient->GetTimeSinceEnemyInjury() < 3.0f )
		return Done( "My patient is at the mercy of the infected" );
	// S3B09BC captures permanent health before the eventful buffer query.
	const int health = patient->GetHealth();
	if ( (double)patient->GetHealthBuffer() + (double)health > 70.0 )
		return Done( "My patient has been healed" );
	if ( patient->IsUseActionTarget( 1 ) || patient->IsPerformingSelfUseAction( 1 ) )
		return Done( "My patient is being healed by someone else" );
	if ( IsHoldingHealingItem( patient ) )
		return Done( "My patient is holding a healing item, presumably to use on themselves" );
	me->ReleaseWalkButton();
	me->Weapon_Switch( item );
	return SurvivorUseObject::Update( me, interval );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void SurvivorGivePillsToFriend::OnObjectUsed( SurvivorBot *me )
{
	CTerrorPlayer *patient = ToTerrorPlayer( m_object.Get() );
	if ( patient && IsInteractionSurvivorTeam( patient->GetTeamNumber() ) && (unsigned int)patient->GetSurvivorCharacter() <= 7 )
		me->GetFriendActionTimer( patient->GetSurvivorCharacter() ).Start( 10.0f );
	me->PressAltFireButton();
	SurvivorUseObject::OnObjectUsed( me );
}

void SurvivorGivePillsToFriend::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	RestoreGun( me );
	SurvivorUseObject::OnEnd( me, nextAction );
}

// S:242B20 is the original 56-handle scan, not the cached Terror lookup.
static bool HasGiftInventoryItem( CTerrorPlayer *player, int weaponID )
{
	for ( int i = 0; i < 56; ++i )
	{
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( player->GetWeapon( i ) );
		if ( weapon && weapon->GetWeaponID() == weaponID )
			return true;
	}
	return false;
}

// S:3AFFE0. Keep both RNG draws conditional and in their original order.
static int ChooseGiftItem()
{
	int kits = 0;
	int injured = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() )
			continue;
		if ( HasGiftInventoryItem( player, 12 ) )
			++kits;
		if ( player->GetHealth() < 50 )
			++injured;
	}
	if ( injured > kits )
		return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_TempHealthOnly", false ) ? 15 : 12;

	CDirectorItemManager *manager = TheDirector->GetItemManager();
	if ( manager->CanUseWeapon( 20 ) )
	{
		int choice = RandomInt( 0, 100 );
		if ( choice < 2 )
		{
			manager->OnWeaponUsed( 20 );
			return 20;
		}
		if ( choice < 4 )
		{
			// Original checks 20 but consumes 37 on this branch.
			manager->OnWeaponUsed( 37 );
			return 37;
		}
	}
	switch ( RandomInt( 0, 4 ) )
	{
	case 0: return 13;
	case 1: return 25;
	case 2: return 14;
	case 4: return 23;
	default: return 15;
	}
}

SurvivorGiveItem::SurvivorGiveItem( CTerrorPlayer *recipient ) : m_recipient( recipient )
{
	m_item = NULL;
}

ActionResult< SurvivorBot > SurvivorGiveItem::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_weaponID = ChooseGiftItem();
	CBaseEntity *entity = me->GiveNamedItemInternal( WeaponIDToAlias( m_weaponID ), 0, true, NULL );
	CTerrorWeapon *item = dynamic_cast< CTerrorWeapon * >( entity );
	if ( !item )
		return Done( "Unable to give myself the item to give" );
	m_item = item;
	if ( me->GetActiveWeapon() != item && !me->Weapon_Switch( item ) )
		return Done( "Unable to switch to the item we're trying to give." );

	if ( m_recipient.Get() )
	{
		me->GetBodyInterface()->AimHeadTowards( m_recipient.Get(), IBody::IMPORTANT, 1.0f, NULL, "Looking at who I might give an item to", true, 0.25f );
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "GiveTarget:%s", GetSurvivorCharacterName( m_recipient->GetSurvivorCharacter() ) );
		me->SpeakIfAllowed( "SurvivorBotTakeThisItem", 1, modifiers, NULL, 0, NULL );
	}
	else
	{
		me->SpeakIfAllowed( "SurvivorBotTakeThisItem", 1, NULL, NULL, 0, NULL );
	}
	m_waitTimer.Start( 2.0f );
	return Continue();
}

void SurvivorGiveItem::ThrowItem( SurvivorBot *me )
{
	CTerrorWeapon *item = m_item.Get();
	if ( !item )
		return;

	Vector direction;
	if ( m_recipient.Get() )
	{
		direction = m_recipient->GetAbsOrigin() - me->GetAbsOrigin();
		direction.z = 0.0f;
		VectorNormalize( direction );
	}
	else
	{
		AngleVectors( me->EyeAngles(), &direction );
	}

	CBaseEntity *best = NULL;
	float bestDot = -1.0f;
	CBaseEntity *target = NULL;
	while ( ( target = gEntList.FindEntityByClassname( target, "throw_item_target" ) ) != NULL )
	{
		trace_t trace;
		NextBotTraceFilterIgnoreActors filter( NULL, COLLISION_GROUP_NONE );
		UTIL_TraceLine( me->EyePosition(), target->GetAbsOrigin(), 0x02006041, &filter, &trace );
		if ( trace.fraction >= 1.0f && !trace.startsolid )
		{
			Vector targetDirection = trace.endpos - trace.startpos;
			targetDirection.z = 0.0f;
			VectorNormalize( targetDirection );
			float dot = DotProduct( targetDirection, direction );
			if ( dot > bestDot )
			{
				best = target;
				bestDot = dot;
			}
		}
	}
	if ( best )
		me->DropWeapon( item, true, &best->GetAbsOrigin() );
	else
		me->DropWeapon( item, true, NULL );
	item->GiveToPlayer( me, NULL );
	m_item = NULL;
}

ActionResult< SurvivorBot > SurvivorGiveItem::Update( SurvivorBot *me, float interval )
{
	if ( !m_waitTimer.IsElapsed() )
		return Continue();
	ThrowItem( me );
	return Done();
}

void SurvivorGiveItem::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	if ( m_item.Get() )
	{
		me->Weapon_Detach( m_item.Get() );
		UTIL_Remove( m_item.Get() );
	}
}

ActionResult< SurvivorBot > SurvivorAttractForItemGive::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_waitTimer.Start( survivor_give_attract_timeout.GetFloat() );
	m_speechTimer.Start( 0.1f );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorAttractForItemGive::Update( SurvivorBot *me, float interval )
{
	if ( m_waitTimer.IsElapsed() )
		return ChangeTo( new SurvivorGiveItem( NULL ), "No-one came to get the item, throwing it anyway" );

	// S:3AF380/3AF620. Selection is in client-slot order; a closer candidate
	// can still lose to the already selected recipient's view dot.
	CTerrorPlayer *recipient = NULL;
	const Vector eye = me->EyePosition();
	float nearestRangeSqr = FLT_MAX;
	float bestDot = -1.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ||
			!player->IsAlive() || player->IsIncapacitated() || player->IsDominated() )
			continue;
		float rangeSqr = ( player->WorldSpaceCenter() - eye ).LengthSqr();
		if ( rangeSqr > 640000.0f || ( rangeSqr > 202500.0f && bestDot > -1.0f ) || nearestRangeSqr <= rangeSqr )
			continue;
		trace_t trace;
		NextBotTraceFilterIgnoreActors filter( NULL, COLLISION_GROUP_NONE );
		UTIL_TraceLine( eye, player->WorldSpaceCenter(), 0x02006041, &filter, &trace );
		if ( trace.fraction < 1.0f || trace.startsolid )
			continue;
		if ( rangeSqr < 202500.0f )
		{
			Vector toBot = eye - player->WorldSpaceCenter();
			VectorNormalize( toBot );
			Vector view;
			AngleVectors( player->EyeAngles(), &view );
			float dot = DotProduct( view, toBot );
			if ( bestDot > dot )
				continue;
			bestDot = dot;
		}
		nearestRangeSqr = rangeSqr;
		recipient = player;
	}
	if ( !recipient )
		return Continue();

	me->GetBodyInterface()->AimHeadTowards( recipient, IBody::INTERESTING, 1.0f, NULL, "Looking at who I might give an item to", true, 0.25f );
	// S:3B0220 compares squared range against 202500 squared again.
	// Preserve the original dimensional bug; do not replace with 450^2.
	if ( nearestRangeSqr < 41006252032.0f && bestDot > 0.9f )
		return ChangeTo( new SurvivorGiveItem( recipient ), "Someone is close enough and looking at me." );
	if ( m_speechTimer.IsElapsed() )
	{
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "GiveTarget:%s", GetSurvivorCharacterName( recipient->GetSurvivorCharacter() ) );
		me->SpeakIfAllowed( "SurvivorBotAttractPlayerForThrow", 1, modifiers, NULL, 0, NULL );
		m_speechTimer.Start( RandomFloat( 3.0f, 5.0f ) );
	}
	return Continue();
}

Action< SurvivorBot > *SurvivorAttractAndGiveItem::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttractForItemGive;
}

ActionResult< SurvivorBot > SurvivorAttractAndGiveItem::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	InteractionIntention( me )->PushLegsAction( new SurvivorLegsWait( 0.0f ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorAttractAndGiveItem::Update( SurvivorBot *me, float interval )
{
	return GetActiveChildAction() ? Continue() : Done( "Child actions completed" );
}

void SurvivorAttractAndGiveItem::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	InteractionIntention( me )->PopLegsAction();
}
