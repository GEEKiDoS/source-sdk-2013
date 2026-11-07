//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/infected/boss/witch.h"
#include "NextBot/NextBotDebug.h"
#include "tier0/memdbgon.h"

ConVar sb_friend_immobilized_reaction_time_normal( "sb_friend_immobilized_reaction_time_normal", "2.0", 0x00004004, "How quickly a SurvivorBot realizes a friend has been Pounced or Tongued" );
ConVar sb_friend_immobilized_reaction_time_hard( "sb_friend_immobilized_reaction_time_hard", "1.0", 0x00004004, "How quickly a SurvivorBot realizes a friend has been Pounced or Tongued" );
ConVar sb_friend_immobilized_reaction_time_expert( "sb_friend_immobilized_reaction_time_expert", "0.5", 0x00004004, "How quickly a SurvivorBot realizes a friend has been Pounced or Tongued" );
ConVar sb_friend_immobilized_reaction_time_vs( "sb_friend_immobilized_reaction_time_vs", "0.5", 0x00004004, "How quickly a SurvivorBot realizes a friend has been Pounced or Tongued" );

namespace
{
	void SpeakCombatRescue( SurvivorBot *me, const char *concept, CTerrorPlayer *subject )
	{
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "Subject:%s", subject ? GetSurvivorCharacterName( subject->GetSurvivorCharacter() ) : "" );
		me->SpeakIfAllowed( concept, 1, modifiers, NULL, 0, NULL );
	}
}

ActionResult< SurvivorBot > SurvivorBot::TrySaveFriendsInImmediateTrouble()
{
	SurvivorTeamSituation &team = GetTeamSituation();
	if ( !IsSafeForCombatAction() )
	{
		if ( IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s: SaveFriendsInImmediateTrouble: Not available at the moment\n", gpGlobals->curtime, GetDebugIdentifier() );
		return ActionResult< SurvivorBot >();
	}
	float reactionTime = sb_friend_immobilized_reaction_time_normal.GetFloat();
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
		reactionTime = sb_friend_immobilized_reaction_time_vs.GetFloat();
	else if ( TerrorGameRules()->GetDifficultyLevel() == 3 )
		reactionTime = sb_friend_immobilized_reaction_time_expert.GetFloat();
	else if ( TerrorGameRules()->GetDifficultyLevel() == 2 )
		reactionTime = sb_friend_immobilized_reaction_time_hard.GetFloat();
	if ( IsRespondingToSurvivorDistress() && reactionTime >= 1.0f )
		reactionTime = 1.0f;

	CTerrorPlayer *downedHuman = team.GetDownedHuman();
	CTerrorPlayer *downedFriend = team.GetDownedFriend();
	if ( TheDirector->GetTankCount() <= 0 && ( downedHuman || downedFriend ) )
	{
		CTerrorPlayer *patient = downedHuman ? downedHuman : downedFriend;
		Witch *witch = patient->GetAttackingWitch();
		if ( patient->IsNearAttackingWitch() )
		{
			// The original formats the HUMAN handle's subject even when the
			// chosen patient is the fallback bot. Do not repair that mismatch.
			SpeakCombatRescue( this, "SurvivorBotMovingToReviveFriend", downedHuman );
			return ActionResult< SurvivorBot >( SUSPEND_FOR,
				new SurvivorLiberateBesiegedFriend( patient, new SurvivorDispatchEnemy( witch ) ), "Rescuing a downed friend from witch" );
		}
	}

	CTerrorPlayer *pouncedFriend = team.GetPouncedFriend();
	if ( pouncedFriend && !IsSelf( pouncedFriend ) && pouncedFriend->m_pounceStartTimer.GetElapsedTime() > reactionTime && CanReachSurvivor( pouncedFriend ) )
	{
		SpeakCombatRescue( this, "SurvivorBotMoveToSavePouncedFriend", pouncedFriend );
		return ActionResult< SurvivorBot >( SUSPEND_FOR,
			new SurvivorLiberateBesiegedFriend( pouncedFriend, new SurvivorDislodgeVictim( pouncedFriend, true ) ), "Trying to dislodge Pounced friend!" );
	}
	CTerrorPlayer *tonguedFriend = team.GetTonguedFriend();
	if ( tonguedFriend && !IsSelf( tonguedFriend ) && gpGlobals->curtime - tonguedFriend->GetTongueVictimTimerStartTime() > reactionTime && CanReachSurvivor( tonguedFriend ) )
	{
		SpeakCombatRescue( this, "SurvivorBotMoveToSaveTonguedFriend", tonguedFriend );
		if ( tonguedFriend->m_isHangingFromLedge )
			return ActionResult< SurvivorBot >( SUSPEND_FOR,
				new SurvivorLiberateBesiegedFriend( tonguedFriend, new SurvivorReviveFriend( tonguedFriend ) ), "Rescuing friend ledge-hanging from a Smoker Tongue!" );
		return ActionResult< SurvivorBot >( SUSPEND_FOR,
			new SurvivorLiberateBesiegedFriend( tonguedFriend, new SurvivorDislodgeVictim( tonguedFriend, true ) ), "Trying to dislodge Smoker-tongued friend!" );
	}
	CTerrorPlayer *pummeledFriend = team.GetPummeledFriend();
	if ( pummeledFriend && !IsSelf( pummeledFriend ) && CanReachSurvivor( pummeledFriend ) )
	{
		SpeakCombatRescue( this, "SurvivorBotMoveToSavePummeledFriend", pummeledFriend );
		return ActionResult< SurvivorBot >( SUSPEND_FOR,
			new SurvivorLiberateBesiegedFriend( pummeledFriend, new SurvivorDispatchEnemy( pummeledFriend->GetPummelAttacker() ) ), "Trying to dispatch Pummeler!" );
	}
	CTerrorPlayer *jockeyedFriend = team.GetJockeyedFriend();
	if ( jockeyedFriend && !IsSelf( jockeyedFriend ) && CanReachSurvivor( jockeyedFriend ) )
		return ActionResult< SurvivorBot >( SUSPEND_FOR,
			new SurvivorLiberateBesiegedFriend( jockeyedFriend, new SurvivorDislodgeVictim( jockeyedFriend, true ) ), "Trying to dislodge Jockeyed friend!" );

	bool mayRescueBot = true;
	if ( downedFriend && TheDirector->IsFinale() )
		mayRescueBot = CountHumansNearRescueVehicle() <= 0;
	if ( downedHuman && TheDirector->GetTankCount() <= 0 )
	{
		SpeakCombatRescue( this, "SurvivorBotMovingToReviveFriend", downedHuman );
		return ActionResult< SurvivorBot >( SUSPEND_FOR,
			new SurvivorLiberateBesiegedFriend( downedHuman, new SurvivorReviveFriend( downedHuman ) ), "Rescuing a downed human friend in big trouble" );
	}
	if ( !downedFriend || !mayRescueBot || IsSelf( downedFriend ) || ( team.GetDownedFriendHealth() >= 150.0f && downedFriend->GetTimeSinceEnemyInjury() <= 1.5f ) )
		return ActionResult< SurvivorBot >();
	SpeakCombatRescue( this, "SurvivorBotMovingToReviveFriend", downedFriend );
	return ActionResult< SurvivorBot >( SUSPEND_FOR,
		new SurvivorLiberateBesiegedFriend( downedFriend, new SurvivorReviveFriend( downedFriend ) ), "Rescuing a downed friend in big trouble" );
}
