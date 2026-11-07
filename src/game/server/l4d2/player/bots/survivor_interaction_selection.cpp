//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "collisionutils.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/upgrade_item.h"
#include "ammodef.h"
#include "tier0/memdbgon.h"

extern ConVar sb_perf_crawl;
ConVar sb_temp_health_consider_factor( "sb_temp_health_consider_factor", "0.5", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL,
	"Temporary health is multiplied by this when SurvivorBots consider who needs healing" );
ConVar sb_toughness_buffer( "sb_toughness_buffer", "15", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL,
	"How much more SurvivorBots must be hurt to conider themselves equally valid as a healing target" );
ConVar sb_use_button_range( "sb_use_button_range", "1000", 0x00004004 );
ConVar sb_max_scavenge_separation( "sb_max_scavenge_separation", "750", 0x00004004,
	"SurvivorBots won't scavenge items farther away from the group than this" );
ConVar sb_max_team_melee_weapons( "sb_max_team_melee_weapons", "0", 0x00004004,
	"The total number of melee weapons allowed on the team. 0 = bots never use melee" );
ConVar sb_use_upgrades( "sb_use_upgrades", "1", 0x00004004 );

// S:39C6A0/39C870. These are two native searches with the same ordering and
// navigation score, differing only in slot versus the cached weapon-ID lookup.
// The raw decompiler omits the collector callback and incorrectly prints a
// constant NULL result; the original instructions contain this complete scan.
static CTerrorPlayer *FindNearestFriendWithItem( CBaseEntity *subject, int item, bool bySlot, float *outRange )
{
	CTerrorPlayer *nearest = NULL;
	float nearestRange = FLT_MAX;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ||
			!player->IsAlive() || player->IsGhost() || player->entindex() == subject->entindex() )
			continue;
		if ( bySlot ? !player->Weapon_GetSlot( item ) : !player->GetWeaponOfType( item ) )
			continue;
		if ( !player->CanPerformBackpackAction() )
			continue;
		CBaseCombatCharacter *actor = subject->MyCombatCharacterPointer();
		CNavArea *subjectArea = actor ? actor->GetLastKnownArea() :
			TheNavMesh->GetNearestNavArea( subject->GetAbsOrigin(), false, 10000.0f, false, true );
		ShortestPathCost cost;
		float range = NavAreaTravelDistance( player->GetLastKnownArea(), subjectArea, cost, 0.0f );
		// Native adds the raw survivor character, not health, to path length.
		// In particular an unreachable -1 path result is NOT filtered here.
		range += (float)player->GetSurvivorCharacter();
		if ( nearestRange > range )
		{
			nearest = player;
			nearestRange = range;
		}
	}
	if ( outRange )
		*outRange = nearestRange;
	return nearest;
}

static bool MayGivePills( SurvivorBot *me, CTerrorPlayer *patient, float maxRange )
{
	if ( patient->Weapon_GetSlot( 4 ) )
		return false;
	float range;
	CTerrorPlayer *giver = FindNearestFriendWithItem( patient, 4, true, &range );
	return giver && giver == me && maxRange > range && !patient->Weapon_GetSlot( 5 );
}


#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:39CFB0. Friend first, then own kit, then own belt item; no defibrillator
// action is selected by this original helper.
ActionResult< SurvivorBot > SurvivorBot::TryHealFriends()
{
	if ( !m_healFriendTimer.IsElapsed() )
		return ActionResult< SurvivorBot >();
	m_healFriendTimer.Start( m_healFriendTimer.GetCountdownDuration() );
	if ( !IsSafeForCombatAction() || TheDirector->GetActivePipeBombCount() > 0 )
		return ActionResult< SurvivorBot >();

	SurvivorTeamSituation &situation = GetTeamSituation();
	bool mayUseKit = TheDirector->GetTankCount() <= 0 && !IsNearCheckpoint() && !IsInCombat( false );
	const float ownFactor = sb_temp_health_consider_factor.GetFloat();
	const int ownPermanentHealth = GetHealth();
	const float ownHealth = (float)( (double)GetHealthBuffer() * ownFactor + ownPermanentHealth );
	CTerrorPlayer *friendPlayer = situation.GetWeakestFriend();
	if ( friendPlayer && IsImmediateThreat( friendPlayer ) )
		friendPlayer = NULL;
	CTerrorPlayer *patient = situation.GetWeakestHuman();
	if ( patient && friendPlayer && !patient->IsOnThirdStrike() )
	{
		// S39D107..14F captures both health integers before either buffer
		// getter. Only the friend's weighted result is rounded before compare.
		const float factor = sb_temp_health_consider_factor.GetFloat();
		const int friendPermanentHealth = friendPlayer->GetHealth();
		const int humanPermanentHealth = patient->GetHealth();
		const float friendHealth = (float)( (double)friendPlayer->GetHealthBuffer() * factor + friendPermanentHealth );
		const double humanHealth = (double)patient->GetHealthBuffer() * factor + humanPermanentHealth;
		if ( humanHealth > friendHealth )
			patient = friendPlayer;
	}
	else if ( !patient )
		patient = friendPlayer;
	if ( patient && ( patient->IsInCombat( false ) || patient->GetMoveType() == MOVETYPE_LADDER || patient->m_usingMountedWeapon ) )
		patient = NULL;

	const float toughness = sb_toughness_buffer.GetFloat();
	if ( patient )
	{
		const float factor = sb_temp_health_consider_factor.GetFloat();
		const int permanentHealth = patient->GetHealth();
		const double weightedHealth = (double)patient->GetHealthBuffer() * factor + permanentHealth;
		const float patientHealth = (float)weightedHealth;
		// S39D1BE stores F32 without popping the unrounded x87 comparison.
		if ( weightedHealth < 50.0 && patientHealth < toughness + ownHealth )
		{
			if ( patientHealth >= 30.0f )
			{
				// S39D346 never falls through to kit selection on failure.
				if ( MayGivePills( this, patient, 1000.0f ) )
					return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorGivePillsToFriend( patient ), "Giving pills to a friend" );
			}
			else
			{
				if ( mayUseKit )
				{
					float range;
					CTerrorPlayer *giver = FindNearestFriendWithItem( patient, 12, false, &range );
					if ( giver == this && range < 1000.0f )
					{
						CTerrorWeapon *useWeapon = dynamic_cast< CTerrorWeapon * >( patient->GetActiveWeapon() );
						if ( patient->IsBot() || !useWeapon || useWeapon->GetWeaponID() != 12 )
							return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorHealFriend( patient ), "Trying to heal a friend" );
					}
				}
				if ( MayGivePills( this, patient, 1000.0f ) )
					return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorGivePillsToFriend( patient ), "Giving pills to a friend" );
			}
		}
	}

	CTerrorPlayer *nearest = situation.GetNearestFriend();
	if ( nearest && situation.GetRangeToFriend( nearest ) > 400.0f )
		mayUseKit = false;
	if ( ownHealth < 30.0f && mayUseKit && GetWeaponOfType( 12 ) )
		return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorHealSelf );
	const int permanentHealth = GetHealth();
	if ( (double)GetHealthBuffer() + permanentHealth < 50.0 )
	{
		TerrorNavArea *area = static_cast< TerrorNavArea * >( GetLastKnownArea() );
		if ( !( area && area->HasSpawnAttributes( 0x800 ) ) &&
			( GetWeaponOfType( 15 ) || ( GetWeaponOfType( 23 ) && GetTimeSinceEnemyInjury() > 2.0f ) ) )
			return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorTakePills );
	}
	return ActionResult< SurvivorBot >();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

static void SpeakReviveSelection( SurvivorBot *me, const char *concept, CTerrorPlayer *patient )
{
	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "Subject:%s", GetSurvivorCharacterName( patient->GetSurvivorCharacter() ) );
	me->SpeakIfAllowed( concept, 1, modifiers, NULL, 0, NULL );
}

// S:39B640. Keep human precedence and the distinct closet path. The contained
// Combat action owns liberation; the following action performs real use/revive.
ActionResult< SurvivorBot > SurvivorBot::TryReviveFriends()
{
	if ( sb_perf_crawl.GetBool() || !IsSafeForCombatAction() )
		return ActionResult< SurvivorBot >();
	SurvivorTeamSituation &situation = GetTeamSituation();
	CTerrorPlayer *human = situation.GetDownedHuman();
	CTerrorPlayer *friendPlayer = situation.GetDownedFriend();
	if ( human && !human->IsNearAttackingWitch() )
	{
		if ( human->GetTimeSinceEnemyInjury() > 1.5f || IsRespondingToSurvivorDistress() )
		{
			SpeakReviveSelection( this, "SurvivorBotMovingToReviveFriend", human );
			return ActionResult< SurvivorBot >( SUSPEND_FOR,
				new SurvivorLiberateBesiegedFriend( human, new SurvivorReviveFriend( human ) ), "Rescuing a human friend" );
		}
	}
	else if ( friendPlayer && !IsImmediateThreat( friendPlayer ) && !friendPlayer->IsNearAttackingWitch() &&
		!TheDirector->IsRescueVehicleReady() && friendPlayer->GetTimeSinceEnemyInjury() > 1.5f )
	{
		SpeakReviveSelection( this, "SurvivorBotMovingToReviveFriend", friendPlayer );
		return ActionResult< SurvivorBot >( SUSPEND_FOR,
			new SurvivorLiberateBesiegedFriend( friendPlayer, new SurvivorReviveFriend( friendPlayer ) ), "Rescuing a bot friend" );
	}

	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) ) != NULL )
	{
		CSurvivorRescue *rescue = static_cast< CSurvivorRescue * >( entity );
		if ( !rescue->IsCallingForRescue() || situation.GetClosestFriendTo( rescue, 0 ) != this )
			continue;
		CNavArea *area = TheNavMesh->GetNearestNavArea( rescue->WorldSpaceCenter(), true, 200.0f, true, false );
		if ( !CanReachArea( GetLastKnownArea(), area ) )
			continue;
		SpeakReviveSelection( this, "SurvivorBotMoveToRescueFriendFromCloset", rescue->GetRescueTarget() );
		return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorRescueBarricadedFriend( rescue ), "Rescuing a trapped friend" );
	}
	return ActionResult< SurvivorBot >();
}

static bool HasPrimaryTier( SurvivorBot *me, int tier )
{
	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( me->Weapon_GetSlot( 0 ) );
	return weapon && weapon->GetTerrorWpnData().m_iTier == tier;
}

static bool HasPrimaryType( SurvivorBot *me, TerrorWeaponType_t type )
{
	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( me->Weapon_GetSlot( 0 ) );
	return weapon && weapon->GetTerrorWpnData().m_WeaponType == type;
}

// S:3A4660. The preference is a survivor-character rule, not a weapon score.
static bool IsPreferredPrimary( SurvivorBot *me, const CTerrorWeaponInfo &info )
{
	bool tierTwo = info.m_iTier == 2;
	switch ( GetTerrorSurvivorDisplayCharacter( me->GetSurvivorCharacter() ) )
	{
	case SurvivorCharacter_Coach:
		if ( tierTwo )
		{
			if ( info.m_WeaponType != TERROR_WEAPONTYPE_SHOTGUN )
				return false;
			if ( !HasPrimaryType( me, TERROR_WEAPONTYPE_SHOTGUN ) )
				return true;
		}
		else if ( info.m_WeaponType != TERROR_WEAPONTYPE_SHOTGUN || HasPrimaryType( me, TERROR_WEAPONTYPE_SHOTGUN ) )
		{
			return false;
		}
		return !HasPrimaryTier( me, 2 );
	case SurvivorCharacter_Mechanic:
	case SurvivorCharacter_TeenGirl:
		if ( !tierTwo )
			return false;
		if ( !TerrorGameRules()->IsPlayerControlledZombies() && !TerrorGameRules()->IsSurvivalMode() )
			return info.m_WeaponType == TERROR_WEAPONTYPE_SNIPER_RIFLE && !HasPrimaryType( me, TERROR_WEAPONTYPE_SNIPER_RIFLE );
		// The original multiplayer/survival branch shares Francis's shotgun.
	case SurvivorCharacter_Biker:
		if ( !tierTwo || info.m_WeaponType != TERROR_WEAPONTYPE_SHOTGUN )
			return false;
		return !HasPrimaryType( me, TERROR_WEAPONTYPE_SHOTGUN ) || !HasPrimaryTier( me, 2 );
	default:
		if ( !tierTwo || info.m_WeaponType != TERROR_WEAPONTYPE_RIFLE )
			return false;
		return !HasPrimaryType( me, TERROR_WEAPONTYPE_RIFLE ) || !HasPrimaryTier( me, 2 );
	}
}

// S:3A44E0/3A4120/3A40C0. Living, standing humans reserve map stock.
static bool CanScavengeItem( CBaseEntity *entity, int weaponID )
{
	if ( weaponID == 54 || ( weaponID == 55 && sb_use_upgrades.GetBool() ) )
		return true;
	const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weaponID );
	if ( !info || weaponID == 24 || weaponID == 31 || weaponID == 30 )
		return false;
	if ( ( TerrorGameRules()->IsScavengeMode() || TerrorGameRules()->IsSurvivalMode() ) &&
		( weaponID == 12 || weaponID == 15 || weaponID == 23 ) )
		return true;
	if ( info->iSlot == 0 || info->iSlot == 1 )
		return !entity->GetOwnerEntity() || !entity->GetOwnerEntity()->IsAlive();
	if ( info->iSlot != 3 && info->iSlot != 4 )
		return false;

	int missing = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ||
			player->IsBot() || player->IsIncapacitated() || !player->IsAlive() )
			continue;
		if ( info->iSlot == 3 ? !player->GetWeaponOfType( weaponID ) : !player->Weapon_GetSlot( 4 ) )
			++missing;
	}
	return missing < TheDirector->GetItemManager()->CountTrackedWeapons( weaponID );
}

// S:3A4780. True means the candidate reaches the common original acquisition
// checks; false does not mean the item itself is unusable by a human.
static bool WantsScavengeItem( SurvivorBot *me, CBaseEntity *entity, int weaponID )
{
	if ( TheDirector->GetScriptedMode()->ShouldAvoidItem( weaponID ) || !me->IsWeaponAllowedForTeam( weaponID ) ||
		me->GetIntentionInterface()->ShouldPickUp( me, entity ) == ANSWER_NO )
		return false;

	const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weaponID );
	if ( info && info->m_iTier == 1 && info->iSlot == 0 )
	{
		if ( ( HasPrimaryTier( me, 1 ) || HasPrimaryTier( me, 2 ) ) && !me->IsLowOnAmmo() )
			return false;
	}
	else if ( info && info->m_iTier == 2 && info->iSlot == 0 )
	{
		if ( HasPrimaryTier( me, 2 ) )
		{
			if ( me->GetHumanSpectatorUserID() > 0 || ( !me->IsLowOnAmmo() && !IsPreferredPrimary( me, *info ) ) )
				return false;
		}
	}
	else if ( info && info->m_WeaponType == TERROR_WEAPONTYPE_GRENADE )
	{
		if ( me->Weapon_GetSlot( 2 ) )
			return false;
	}
	else if ( weaponID == 15 || weaponID == 23 )
	{
		if ( me->Weapon_GetSlot( 4 ) )
			return false;
	}
	else if ( weaponID == 12 )
	{
		CTerrorWeapon *backpack = dynamic_cast< CTerrorWeapon * >( me->Weapon_GetSlot( 3 ) );
		if ( backpack && backpack->GetWeaponID() == 12 )
			return false;
	}
	else if ( weaponID == 32 )
	{
		SurvivorCharacterType character = static_cast< SurvivorCharacterType >( GetTerrorSurvivorDisplayCharacter( me->GetSurvivorCharacter() ) );
		if ( character == SurvivorCharacter_Coach || character == SurvivorCharacter_Mechanic ||
			character == SurvivorCharacter_TeenGirl || character == SurvivorCharacter_Biker )
			return false;
	}
	else if ( info && info->m_WeaponType == TERROR_WEAPONTYPE_PISTOL )
	{
		CTerrorGun *secondary = dynamic_cast< CTerrorGun * >( me->Weapon_GetSlot( 1 ) );
		if ( secondary && ( secondary->IsDualWielding() || !secondary->CanDualWield() ) )
			return false;
	}
	else if ( weaponID == 19 )
	{
		if ( me->GetTeamSituation().GetMeleeWeaponCount() >= sb_max_team_melee_weapons.GetInt() )
			return false;
	}
	else if ( weaponID == 54 )
	{
		CBaseCombatWeapon *primary = me->Weapon_GetSlot( 0 );
		if ( !primary || !primary->UsesPrimaryAmmo() )
			return false;
		int maxCarry = GetAmmoDef()->MaxCarry( primary->GetPrimaryAmmoType(), me );
		if ( (float)me->GetAmmoCount( primary->GetPrimaryAmmoType() ) > (float)maxCarry * 0.8f || maxCarry <= 0 )
			return false;
	}
	else if ( weaponID == 55 )
	{
		CTerrorWeapon *primary = dynamic_cast< CTerrorWeapon * >( me->Weapon_GetSlot( 0 ) );
		CBaseUpgradeItem *upgrade = static_cast< CBaseUpgradeItem * >( entity );
		if ( !primary || !upgrade || primary->HasUpgrade( upgrade->GetUpgradeType() ) ||
			( upgrade->IsLimitedUse() && upgrade->HasBeenUsedBySurvivor( me->GetSurvivorCharacter() ) ) )
			return false;
	}
	else if ( weaponID != 20 || Q_stricmp( TheDirector->GetGameMode(), "mutation7" ) )
	{
		return false;
	}

	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( entity->MyCombatWeaponPointer() );
	if ( weapon )
	{
		if ( weapon->GetSlot() == 0 && me->Weapon_GetSlot( 0 ) &&
			weapon->GetExtraPrimaryAmmo() < GetAmmoDef()->MaxCarry( weapon->GetPrimaryAmmoType(), me ) / 4 )
			return false;
		// The native two tests reject BOTH this previous owner and any other
		// previous owner until the actual weapon transfer timer expires.
		if ( weapon->GetPreviousOwner() )
			return false;
	}
	CNavArea *area = TheNavMesh->GetNearestNavArea( entity, GETNAVAREA_CHECK_LOS, 250.0f );
	return ( !area || ( !area->IsDamaging() && !area->IsBlocked( TEAM_SURVIVOR, false ) ) ) && me->IsReachable( entity );
}

struct SurvivorScavengeCandidate
{
	CBaseEntity *entity;
	int weaponID;
};

CBaseEntity *FindScavengeItem( SurvivorBot *me, float range )
{
	CUtlVectorFixedGrowable< SurvivorScavengeCandidate, 64 > candidates;
	Vector extent( range, range, range );
	Vector mins = me->GetAbsOrigin() - extent;
	Vector maxs = me->GetAbsOrigin() + extent;
	const CUtlVector< CWeaponSpawn * > &spawns = CWeaponSpawn::GetWeaponSpawns();
	FOR_EACH_VEC( spawns, i )
	{
		CWeaponSpawn *spawn = spawns[i];
		if ( !( spawn->ObjectCaps() & FCAP_IMPULSE_USE ) || !IsPointInBox( spawn->GetAbsOrigin(), mins, maxs ) )
			continue;
		SurvivorScavengeCandidate candidate = { spawn, spawn->GetWeaponID() };
		candidates.AddToTail( candidate );
		if ( candidate.weaponID == 12 || candidate.weaponID == 23 || candidate.weaponID == 15 )
			TheDirector->GetItemManager()->GetItemNavArea( spawn );
	}
	CBaseCombatWeapon *weapons[64];
	int weaponCount = CBaseCombatWeapon::GetAvailableWeaponsInBox( weapons, ARRAYSIZE( weapons ), mins, maxs );
	candidates.EnsureCapacity( candidates.Count() + weaponCount );
	for ( int i = 0; i < weaponCount; ++i )
	{
		CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( weapons[i] );
		SurvivorScavengeCandidate candidate = { weapon, weapon->GetWeaponID() };
		candidates.AddToTail( candidate );
		if ( candidate.weaponID == 12 || candidate.weaponID == 23 || candidate.weaponID == 15 )
			TheDirector->GetItemManager()->GetItemNavArea( weapon );
	}

	CBaseEntity *best = NULL;
	float bestRange = FLT_MAX;
	const Vector origin = me->GetAbsOrigin();
	FOR_EACH_VEC( candidates, i )
	{
		const SurvivorScavengeCandidate &candidate = candidates[i];
		if ( !CanScavengeItem( candidate.entity, candidate.weaponID ) )
			continue;
		if ( ( candidate.weaponID == 1 || candidate.weaponID == 32 ) && me->HasWeaponOfType( TERROR_WEAPONTYPE_MELEE ) &&
			me->GetTeamSituation().GetMeleeWeaponCount() <= sb_max_team_melee_weapons.GetInt() )
			continue;
		if ( candidate.weaponID != 1 && me->GetWeaponOfType( candidate.weaponID ) )
		{
			CBaseCombatWeapon *weapon = candidate.entity->MyCombatWeaponPointer();
			if ( !weapon || !me->IsLowOnAmmo() || weapon->GetSlot() != 0 )
				continue;
		}
		Vector delta = candidate.entity->GetAbsOrigin() - origin;
		float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		if ( distance > bestRange )
			continue;
		TerrorNavArea *itemArea = TheDirector->GetItemManager()->GetItemNavArea( candidate.entity );
		TerrorNavArea *myArea = static_cast< TerrorNavArea * >( me->GetLastKnownArea() );
		if ( !myArea || !itemArea )
			continue;
		if ( TheDirector->HasAnySurvivorLeftSafeArea() && itemArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) &&
			!myArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			continue;
		if ( TheDirector->HaveAllSurvivorsLeftSafeArea() )
		{
			if ( !itemArea->IsPotentiallyVisible( myArea, false ) )
				continue;
		}
		else if ( myArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && !itemArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
		{
			continue;
		}
		if ( WantsScavengeItem( me, candidate.entity, candidate.weaponID ) )
		{
			best = candidate.entity;
			bestRange = distance;
		}
	}
	return best;
}

// S:39D480. The candidate check below runs before OnStart, including its
// original reachability RNG; it is not a generic retry/nearest-item policy.
ActionResult< SurvivorBot > SurvivorBot::TryScavenge()
{
	if ( !IsSafeForCombatAction() )
		return ActionResult< SurvivorBot >();
	TerrorNavArea *area = static_cast< TerrorNavArea * >( GetLastKnownArea() );
	if ( ( ( !area || !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) && GetTimeSinceEnemyInjury() < 2.0f ) ||
		GetTeamSituation().IsTeamNearCheckpoint() || !area || ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() ) )
		return ActionResult< SurvivorBot >();
	if ( !m_scavengeTimer.IsElapsed() && !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
		return ActionResult< SurvivorBot >();
	m_scavengeTimer.Start( RandomFloat( 0.5f, 1.0f ) );
	CBaseEntity *item = FindScavengeItem( this, 420.0f );
	if ( !item )
		return ActionResult< SurvivorBot >();
	TerrorNavArea *itemArea = TheDirector->GetItemManager()->GetItemNavArea( item );
	if ( !itemArea )
		return ActionResult< SurvivorBot >();
	if ( !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
	{
		if ( itemArea->GetFlowDistance() > area->GetFlowDistance() + 250.0f ||
			area->GetFlowDistance() - 750.0f > itemArea->GetFlowDistance() )
			return ActionResult< SurvivorBot >();
		CTerrorPlayer *leader = GetTeamSituation().GetLeader();
		float separation = sb_max_scavenge_separation.GetFloat();
		if ( leader && ( item->GetAbsOrigin() - leader->GetAbsOrigin() ).LengthSqr() > separation * separation )
			return ActionResult< SurvivorBot >();
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( item->MyCombatWeaponPointer() );
		if ( weapon && weapon->GetTerrorWpnData().m_WeaponType == TERROR_WEAPONTYPE_HEALING &&
			FindNearestFriendWithItem( item, -1, false, NULL ) != this )
			return ActionResult< SurvivorBot >();
	}
	SurvivorCollectObject *action = new SurvivorCollectObject( item );
	if ( action->ShouldGiveUp( this ) )
	{
		delete action;
		return ActionResult< SurvivorBot >();
	}
	return ActionResult< SurvivorBot >( SUSPEND_FOR, action, "Scavenging something" );
}

// S:2628F0/25BB10. An empty survivor set also returns true in the original.
bool AreAllSurvivorsInFinale()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		TerrorNavArea *area = static_cast< TerrorNavArea * >( player->GetLastKnownArea() );
		if ( !area || !area->HasSpawnAttributes( TERROR_NAV_FINALE ) )
			return false;
	}
	return true;
}

// S:39A4F0. Only an all-bot group automatically looks for buttons. There is no
// invented human-idle grace period and no emergency entity-use bypass.
ActionResult< SurvivorBot > SurvivorBot::TryUseObjects()
{
	if ( !IsSafeForCombatAction() || !GetTeamSituation().AreAllTeammatesBots() ||
		TheDirector->GetScenario()->IsFirstPanicEvent() ||
		TheDirector->GetScenario()->GetPanicStage() != 4 ||
		TheDirector->GetTankCount() > 0 || TerrorGameRules()->IsSurvivalMode() || !m_useObjectTimer.IsElapsed() )
		return ActionResult< SurvivorBot >();
	m_useObjectTimer.Start( RandomFloat( 3.0f, 5.0f ) );
	CBaseEntity *object = gEntList.FindEntityByClassnameNearest( "func_button", GetAbsOrigin(), sb_use_button_range.GetFloat() );
	if ( !object )
	{
		if ( !AreAllSurvivorsInFinale() )
			return ActionResult< SurvivorBot >();
		object = gEntList.FindEntityByClassnameNearest( "trigger_finale", GetAbsOrigin(), sb_use_button_range.GetFloat() );
		if ( !object )
			return ActionResult< SurvivorBot >();
	}
	TerrorNavArea *area = static_cast< TerrorNavArea * >( TheNavMesh->GetNearestNavArea( object->WorldSpaceCenter(), false,
		sb_use_button_range.GetFloat() * 2.0f, false, false ) );
	if ( !area )
		return ActionResult< SurvivorBot >();
	TerrorNavArea *currentArea = static_cast< TerrorNavArea * >( GetLastKnownArea() );
	if ( fabsf( area->GetFlowDistance() - currentArea->GetFlowDistance() ) >= 500.0f )
		return ActionResult< SurvivorBot >();
	Vector position;
	area->GetClosestPointOnArea( object->WorldSpaceCenter(), &position );
	CNavArea *approachArea = TheNavMesh->GetNearestNavArea( position, true, 200.0f, true, false );
	if ( !CanReachArea( GetLastKnownArea(), approachArea ) )
		return ActionResult< SurvivorBot >();
	m_useObjectTimer.Start( RandomFloat( 20.0f, 30.0f ) );
	return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorUseObject( object, position ), "Trying to use something to help us along" );
}
