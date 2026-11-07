#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_path.h"
#include "l4d2/effects/survivor_death_model.h"
#include "tier0/memdbgon.h"

bool CTerrorPlayer::IsOnASurvivorTeam() const // S00EDD0 -> S350A70
{
	return GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR;
}

void CTerrorPlayer::ScriptGivePrimaryAmmo( int count ) // S2FB080
{
	CBaseCombatWeapon *weapon = Weapon_GetSlot( 0 );
	if ( weapon )
		GiveAmmo( count, weapon->GetPrimaryAmmoType(), false );
}

void CTerrorPlayer::ScriptGiveNamedItem( const char *name ) // S2FA370
{
	GiveNamedItemInternal( name, 0, true, NULL );
}

void CTerrorPlayer::ScriptGiveNamedItemWithSkin( const char *name, int skin ) // S30C260
{
	CBaseEntity *item = GiveNamedItemInternal( name, 0, true, NULL );
	if ( skin >= 0 && item )
	{
		CBaseAnimating *animating = item->GetBaseAnimating();
		if ( animating )
		{
			animating->SetSkin( skin );
			CBaseCombatWeapon *weapon = dynamic_cast< CBaseCombatWeapon * >( item );
			if ( weapon )
				weapon->SetViewModel();
		}
	}
}

// The native descriptors say "by name", but both arguments really are integers.
void CTerrorPlayer::ScriptAddUpgrade( int upgrade ) // S309930
{
	GiveUpgrade( upgrade );
}

void CTerrorPlayer::ScriptRemoveUpgrade( int upgrade ) // S2FDE40
{
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( Weapon_GetSlot( 0 ) );
	if ( weapon )
		weapon->RemoveUpgrade( upgrade );
	// Ledger306: a non-Terror weapon has no native Terror upgrade virtual.
}

int CTerrorPlayer::ScriptGetButtons() const // S2FA390
{
	return m_nButtons;
}

int CTerrorPlayer::GetSurvivorSlot() const // S301E60
{
	int slot = -1;
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( index );
		if ( !player || FNullEnt( player->edict() ) || !player->IsPlayer() || !player->IsConnected() )
			continue;
		if ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			++slot;
		// This is deliberately outside the survivor-team test.
		if ( player == this )
			break;
	}
	return slot;
}

int CTerrorPlayer::ScriptGetPlayerUserId() const // S304F50
{
	return engine->GetPlayerUserId( edict() );
}

HSCRIPT CTerrorPlayer::ScriptGetActiveWeapon() const // S2FDDE0
{
	CBaseCombatWeapon *weapon = GetActiveWeapon();
	return weapon ? weapon->GetScriptInstance() : NULL;
}

void CTerrorPlayer::ScriptReviveByDefibrillator() // S31F820
{
	CSurvivorDeathModel *body = CSurvivorDeathModel::FindForPlayer( this );
	if ( body )
		body->Defibrillate( this );
}

// S2D2730: the script's native callback returns class9 for either survivor team.
// The game's existing virtual GetZombieClass is the raw class owner; do not
// change that gameplay virtual to manufacture script-facing type metadata.
int CTerrorPlayer::ScriptGetZombieType() const
{
	return IsOnASurvivorTeam() ? 9 : GetZombieClass();
}

bool CTerrorPlayer::IsDying() const // S16B900
{
	return m_lifeState == LIFE_DYING;
}

void CTerrorPlayer::ScriptHitWithVomit() // S319590
{
	OnVomitedUpon( NULL, false );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S322780. Five replacement samples, maximum HORIZONTAL centre distance;
// failed paths contribute their closest area, not an invented failed result.
Vector CTerrorPlayer::TryGetPathableLocationWithin( float radius )
{
	Vector result = vec3_origin;
	// COMISS(0,radius)/JA rejects negatives but admits unordered radius.
	if ( radius < 0.0f || !TheNavMesh->IsLoaded() )
		return result;
	CNavArea *start = GetLastKnownArea();
	if ( m_potentiallyVisibleAreas.Count() == 0 )
		return result;
	CNavArea *closest = NULL;
	float farthestSquared = 0.0f;
	for ( int sample = 0; sample < 5; ++sample )
	{
		TerrorNavArea *candidate = m_potentiallyVisibleAreas[RandomInt( 0, m_potentiallyVisibleAreas.Count() - 1 )];
		const bool reached = BuildTerrorShortestPath( start, candidate, &closest, radius, TEAM_SURVIVOR, false );
		CNavArea *selected = reached ? candidate : closest;
		// Ledger306: retain the path call and remaining samples; skip only
		// the original NULL centre dereference when no start/closest exists.
		if ( !selected )
			continue;
		const Vector &center = selected->GetCenter();
		const Vector &origin = GetAbsOrigin();
		const float dx = center.x - origin.x;
		const float dy = center.y - origin.y;
		const float distanceSquared = dy * dy + dx * dx;
		if ( distanceSquared > farthestSquared )
		{
			const Vector &currentOrigin = GetAbsOrigin();
			const float currentDX = center.x - currentOrigin.x;
			const float currentDY = center.y - currentOrigin.y;
			farthestSquared = currentDY * currentDY + currentDX * currentDX;
			result = center;
		}
	}
	return result;
}

bool CTerrorPlayer::SuppressFallingDamage() const // S11E790
{
	return m_fallDamageImmunityUntil > gpGlobals->curtime;
}

bool CTerrorPlayer::IsFiringWeapon() const // S3021E0
{
	const float timestamp = m_terrorFireInterval.GetStartTime();
	if ( timestamp > 0.0f )
	{
		const float elapsed = static_cast< float >( static_cast< double >( gpGlobals->curtime ) - static_cast< double >( timestamp ) );
		return elapsed < 0.5f;
	}
	return false;
}

void CTerrorPlayer::ScriptStaggerPlayer( Vector sourcePosition ) // S32D730
{
	if ( sourcePosition.x == 0.0f && sourcePosition.y == 0.0f && sourcePosition.z == 0.0f )
		StaggerPlayer( this, NULL );
	else
		StaggerPlayer( NULL, &sourcePosition );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorPlayer::ScriptDropItem( const char *name ) // S2FBCD0
{
	if ( !name || GetObserverMode() != OBS_MODE_NONE )
		return;
	CBaseCombatWeapon *weapon = Weapon_OwnsThisType( name, 0 );
	if ( weapon )
		Weapon_Drop( weapon, NULL, NULL );
}

bool CTerrorPlayer::ScriptSwitchToItem( const char *name ) // S2FBD10
{
	if ( !name || GetObserverMode() != OBS_MODE_NONE )
		return false;
	CBaseCombatWeapon *weapon = Weapon_OwnsThisType( name, 0 );
	if ( !weapon || !Weapon_ShouldSelectItem( weapon ) )
		return false;
	if ( GetActiveWeapon() )
	{
		CBaseCombatWeapon *active = GetActiveWeapon();
		// Ledger285/306: the second read remains after the first, but only
		// target-dependent calls are skipped if that handle was cleared.
		if ( !active || !active->CanHolster() )
			return false;
		ResetAutoaim();
	}
	return Weapon_Switch( weapon, 0 );
}

bool CTerrorPlayer::ScriptIsInCombat() const // S309920
{
	return IsInCombat( false );
}

HSCRIPT CTerrorPlayer::ScriptGetSpecialInfectedDominatingMe() const // S2FDE70
{
	CBaseEntity *attacker = GetDominatingAttacker();
	return attacker ? attacker->GetScriptInstance() : NULL;
}

// S30BDA0: GetAliveDuration's gameplay return is double, but the native script
// binding stores float32. Keep the canonical virtual and narrow in its thunk.
static bool TerrorAliveDurationBinding( ScriptFunctionBindingStorageType_t function, void *context,
	ScriptVariant_t *arguments, int count, ScriptVariant_t *result )
{
	if ( count != 0 || !result || !context )
		return false;
	*result = static_cast< float >( static_cast< CTerrorPlayer * >( context )->GetAliveDuration() );
	return true;
}

// S304BC0. nil is a NULL vector payload, replaced with loader-zeroed S8819A4.
static bool TerrorStaggerBinding( ScriptFunctionBindingStorageType_t function, void *context,
	ScriptVariant_t *arguments, int count, ScriptVariant_t *result )
{
	if ( count != 1 || result || !context )
		return false;
	Vector position = vec3_origin;
	if ( arguments[0].GetType() == FIELD_VECTOR )
		position = static_cast< const Vector & >( arguments[0] );
	else if ( arguments[0].GetType() != FIELD_VOID )
		return true; // Ledger306: omit only undefined raw-pointer invocation.
	static_cast< CTerrorPlayer * >( context )->ScriptStaggerPlayer( position );
	return true;
}

// A binding may use an already-established C++ owner with a different name;
// only the native descriptor's documentary C++ name is changed, never an alias.
#define DEFINE_TERROR_NATIVE_MEMBER( function, nativeFunction, scriptName, help ) \
	DEFINE_SCRIPTFUNC_NAMED( function, scriptName, help ) \
	pDesc->m_FunctionBindings.Tail().m_desc.m_pszFunction = nativeFunction;

// S330690: SCRIPT inheritance is CBaseFlex, not C++ CCSPlayer inheritance.
BEGIN_ENT_SCRIPTDESC( CTerrorPlayer, CBaseFlex, "Terror Player!" )
	DEFINE_SCRIPTFUNC_NAMED( IsOnASurvivorTeam, "IsSurvivor", "On the survivor team (Otherwise, infected)." )
	DEFINE_SCRIPTFUNC( GetPlayerName, "Get the player's name" )
	DEFINE_SCRIPTFUNC( GetNetworkIDString, "Get the player's network (i.e. Steam) ID." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGivePrimaryAmmo, "GiveAmmo", "Give this much ammo for your primary weapon" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGiveNamedItem, "GiveItem", "Give an item/weapon to the player" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGiveNamedItemWithSkin, "GiveItemWithSkin", "Give an item/weapon to the player with a skin value" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptAddUpgrade, "GiveUpgrade", "Give a weapon upgrade by name" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptRemoveUpgrade, "RemoveUpgrade", "Remove a weapon upgrade by name" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetButtons, "GetButtonMask", "Get the player's mask of pressed buttons" )
	DEFINE_SCRIPTFUNC( GetSurvivorSlot, "Get the player's slot" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetPlayerUserId, "GetPlayerUserId", "Get the player's userID." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetActiveWeapon, "GetActiveWeapon", "Get the player's active weapon" )
	DEFINE_SCRIPTFUNC( GetHealthBuffer, "Get Player Health Buffer - from pills or adrenalin etc" )
	DEFINE_SCRIPTFUNC( SetHealthBuffer, "Set Player Health Buffer - from pills or adrenalin etc" )
	DEFINE_TERROR_NATIVE_MEMBER( SetReviveCount, "SetCurrentReviveCount", "SetReviveCount", "Set the number of times the player has been revived" )
	DEFINE_SCRIPTFUNC_NAMED( OnRevived, "ReviveFromIncap", "Revive an incapped player" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptReviveByDefibrillator, "ReviveByDefib", "Revive a dead player by defibrillator" )
	DEFINE_TERROR_NATIVE_MEMBER( ScriptGetZombieType, "GetZombieClass", "GetZombieType", "If an infected, find out what type" )
	pDesc->m_FunctionBindings.Tail().m_desc.m_ReturnType = FIELD_TYPEUNKNOWN;
	DEFINE_SCRIPTFUNC( IsIncapacitated, "Is the player currently incapacitated" )
	DEFINE_SCRIPTFUNC( IsDying, "Player is dying, but not dead yet" )
	DEFINE_SCRIPTFUNC( IsDead, "Is the player dead" )
	DEFINE_SCRIPTFUNC( IsHangingFromLedge, "Is the player currently hanging from a ledge" )
	DEFINE_SCRIPTFUNC( IsGhost, "Is Player a Ghost" )
	DEFINE_TERROR_NATIVE_MEMBER( UseAdrenaline, "OnAdrenalineUsed", "UseAdrenaline", "Cause the effect of Adrenaline - but no HP (SetHealthBuffer for that) - this is just speed!" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptHitWithVomit, "HitWithVomit", "Cause the vomit effect and make a player IT" )
	DEFINE_SCRIPTFUNC( EyePosition, "Try and get the current eye position" )
	DEFINE_SCRIPTFUNC( EyeAngles, "Try and get the current Eye Angles" )
	DEFINE_SCRIPTFUNC( TryGetPathableLocationWithin, "Get a pathable location within this radius" )
	DEFINE_SCRIPTFUNC( GetSenseFlags, "Get the current bits for the bot sense flags" )
	DEFINE_SCRIPTFUNC( SetSenseFlags, "Set the current bits for the bot sense flags - this is a SET, not an or" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptStaggerPlayer, "Stagger", "Stagger a player, optionally from a vector position" )
	pDesc->m_FunctionBindings.Tail().m_pfnBinding = TerrorStaggerBinding;
	DEFINE_SCRIPTFUNC_NAMED( ScriptDropItem, "DropItem", "Make the player drop an item/weapon" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSwitchToItem, "SwitchToItem", "Make the player switch to an item/weapon" )
	DEFINE_SCRIPTFUNC_NAMED( SuppressFallingDamage, "IsSuppressingFallingDamage", "Returns true if falling damage is currently suppressed" )
	DEFINE_SCRIPTFUNC( SnapEyeAngles, "Sets the view angles." )
	DEFINE_SCRIPTFUNC( IsOnFire, "Returns whether a player is on fire" )
	DEFINE_SCRIPTFUNC( Extinguish, "Extinguish a burning player" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetLastKnownArea, "GetLastKnownArea", "Return the last nav area occupied - NULL if unknown" )
	DEFINE_SCRIPTFUNC( IsIT, "Return true if currently IT from bile" )
	DEFINE_SCRIPTFUNC( IsAdrenalineActive, "Return true if adrenaline is active" )
	DEFINE_TERROR_NATIVE_MEMBER( IsKnockedDown, "IsGettingUp", "IsGettingUp", "Return true if player is currently getting up" )
	DEFINE_TERROR_NATIVE_MEMBER( IsBusy, "IsImmobilized", "IsImmobilized", "Return true if currently immobilized" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptIsInCombat, "IsInCombat", "Return true if currently in combat" )
	DEFINE_SCRIPTFUNC( IsOnThirdStrike, "Return true if on third-strike" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetSpecialInfectedDominatingMe, "GetSpecialInfectedDominatingMe", "Get the Special Infected dominating this Survivor" )
	DEFINE_TERROR_NATIVE_MEMBER( IsDominated, "IsDominatedBySpecialInfected", "IsDominatedBySpecialInfected", "Return true if being dominated by a Special Infected" )
	DEFINE_SCRIPTFUNC( IsFiringWeapon, "Return true if player is firing a weapon" )
	DEFINE_TERROR_NATIVE_MEMBER( IsStaggeringOrPending, "IsStaggering", "IsStaggering", "Return true if player is staggering" )
	DEFINE_SCRIPTFUNC( GetAliveDuration, "Returns the time the character has been alive (only valid when alive)" )
	pDesc->m_FunctionBindings.Tail().m_desc.m_ReturnType = FIELD_FLOAT;
	pDesc->m_FunctionBindings.Tail().m_pfnBinding = TerrorAliveDurationBinding;
	DEFINE_SCRIPTFUNC( HasEverBeenInjured, "Returns true if the character has ever been injured by a member of the given team" )
END_SCRIPTDESC()

#undef DEFINE_TERROR_NATIVE_MEMBER
