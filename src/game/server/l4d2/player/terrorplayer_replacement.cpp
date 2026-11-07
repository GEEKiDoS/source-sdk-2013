//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/infected/boss_zombie_player_bot.h"
#include "l4d2/infected/special/hunter.h"
#include "l4d2/infected/special/jockey.h"
#include "l4d2/infected/special/smoker.h"
#include "l4d2/infected/special/boomer.h"
#include "l4d2/infected/special/spitter.h"
#include "l4d2/infected/special/charger.h"
#include "l4d2/infected/boss/tank.h"
#include "baseviewmodel.h"
#include "l4d2/entities/survivor_position.h"
#include "l4d2/player/bots/survivor_speech.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/director/zombie_spawn.h"
#include "tier0/memdbgon.h"

// Native globals85D950/8523D8. The actual infected Spawn/model producers consume
// these at their original branch, never by a fabricated ability/fakeclient.
int g_iNextZombieClass = 0;
int g_iNextInfectedVariant = 0;


CTerrorPlayer *CreateTerrorInfectedPlayerBot( int zombieClass )
{
	switch ( zombieClass )
	{
	case 1: return CreateSmokerBot( "Smoker" );
	case 2: return CreateBoomerBot( "Boomer" );
	case 3: return CreateHunterBot( "Hunter" );
	case 4: return CreateSpitterBot( "Spitter" );
	case 5: return CreateJockeyBot( "Jockey" );
	case 6: return CreateChargerBot( "Charger" );
	case 8: return CreateTankBot( "Tank" );
	default: return NULL;
	}
}

namespace
{
void CollectReplacementObservers( CTerrorPlayer *source, CUtlVector< CTerrorPlayer * > &observers )
{
	// S319440 ->30F400: native state6 and observer modes4/5 only. No team,
	// alive, serial, or extra camera eligibility is added to this raw list.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->IsConnected() && player->State_Get() == STATE_OBSERVER_MODE &&
			player->GetObserverTarget() == source && ( player->GetObserverMode() == OBS_MODE_IN_EYE || player->GetObserverMode() == OBS_MODE_CHASE ) ) observers.AddToTail( player );
	}
}
void CaptureWeapons( CTerrorPlayer *source, CUtlVector< CBaseCombatWeapon * > &weapons, int *ammo )
{
	for ( int id = 1; id < 56; ++id )
	{
		CTerrorWeapon *weapon = source->GetWeaponOfType( id );
		if ( weapon ) { source->Weapon_Detach( weapon ); weapons.AddToTail( weapon ); }
	}
	for ( int i = 0; i < 32; ++i ) ammo[i] = source->GetAmmoCount( i );
	CBaseViewModel *viewmodel = source->GetViewModel( 0 );
	if ( viewmodel ) viewmodel->AddEffects( EF_NODRAW );
}
void ApplyWeapons( CTerrorPlayer *target, const CUtlVector< CBaseCombatWeapon * > &weapons, CBaseCombatWeapon *active, const int *ammo )
{
	// S31B7B0/31A960 call the weapons-only purge, not the player item strip.
	target->CBaseCombatCharacter::RemoveAllWeapons();
	FOR_EACH_VEC( weapons, i ) target->Weapon_Equip( weapons[i] );
	if ( active ) target->Weapon_Switch( active, 0 );
	for ( int i = 0; i < 32; ++i ) target->SetAmmoCount( ammo[i], i );
}
void ReplaceCameraPlayer( CBaseEntity *camera, CTerrorPlayer *oldPlayer, CTerrorPlayer *newPlayer )
{
	variant_t value;
	if ( FClassnameIs( camera, "point_deathfall_camera" ) || FClassnameIs( camera, "point_viewcontrol_survivor" ) || FClassnameIs( camera, "point_viewcontrol" ) )
	{
		camera->AcceptInput( "Disable", oldPlayer, NULL, value, -1 );
		camera->AcceptInput( "Enable", newPlayer, NULL, value, -1 );
	}
	else if ( FClassnameIs( camera, "point_viewcontrol_multiplayer" ) )
	{
		camera->AcceptInput( "RemovePlayer", oldPlayer, NULL, value, -1 );
		camera->AcceptInput( "AddPlayer", newPlayer, NULL, value, -1 );
	}
}
void FireReplaceEvent( const char *name, CTerrorPlayer *human, CTerrorPlayer *bot )
{
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( event ) { event->SetInt( "player", human->GetUserID() ); event->SetInt( "bot", bot->GetUserID() ); gameeventmanager->FireEvent( event ); }
}
}

// S325E60/31B7B0. This synchronous destructive transfer is NOT serialized KV
// state. Raw native actor/weapon pointers stay raw; native handles stay handles.
class SurvivorReplacementState
{
public:
	void Capture( CTerrorPlayer *source );
	void Apply( CTerrorPlayer *oldPlayer, CTerrorPlayer *target );
private:
	SurvivorCharacterType m_character;
	Vector m_origin, m_velocity;
	QAngle m_angles;
	int m_health, m_reviveCount;
	float m_buffer;
	bool m_alive, m_dying;
	MoveType_t m_moveType;
	Vector m_ladderNormal;
	bool m_allowAutoMovement, m_activeWeaponHidden, m_ducked, m_ducking, m_duckFlag, m_leftCheckpoint, m_spawnMusicPlayed;
	Activity m_activity; // Captured by native code, deliberately unused by Apply.
	int m_specificSequence;
	float m_cycle; // Original33C880 takes but ignores this argument.
	CBaseCombatWeapon *m_active;
	CUtlVector< CBaseCombatWeapon * > m_weapons;
	int m_ammo[32];
	int m_lastPrimaryId, m_lastPrimaryAmmo, m_lastSecondaryId;
	bool m_lastHadElites;
	CHandle< CBaseCombatWeapon > m_hidden;
	bool m_incapacitated;
	CTerrorPlayer *m_reviver;
	Vector m_reviveDirection, m_revivePosition;
	int m_currentReviveCount;
	bool m_thirdStrike;
	int m_savedHealth, m_savedBuffer;
	bool m_canGrabLedge, m_hanging, m_falling;
	IntervalTimer m_hangTimer;
	Vector m_hangAirPos, m_hangPos, m_hangStandPos, m_hangNormal;
	bool m_isReleasingLedge;
	Vector m_hangFallPos, m_hangClimbEndPos;
	EHANDLE m_hangSurface;
	Vector m_hangSurfaceOrigin;
	QAngle m_hangSurfaceAngles;
	CountdownTimer m_itTimer;
	bool m_wasIT, m_itUntilNextAttack, m_healingGnome;
	float m_vomitStart;
	CTerrorPlayer *m_pounceAttacker, *m_pummelAttacker, *m_pummelObject, *m_carryAttacker, *m_tongueOwner, *m_jockeyAttacker;
	IntervalTimer m_pounceStart, m_tongueVictimTimer;
	bool m_tongueHanging, m_tongueReached, m_tongueProne;
	int m_tongueReasons;
	CountdownTimer m_tonguePullTimer;
	IntervalTimer m_tongueChokeStart;
	bool m_joinedScenario;
	bool m_locked;
	EHANDLE m_viewEntity, m_positionEntity;
	CUtlVector< CTerrorPlayer * > m_observers;
	EHANDLE m_elevator;
};
void SurvivorReplacementState::Capture( CTerrorPlayer *source )
{
	CollectReplacementObservers( source, m_observers );
	m_character = source->GetSurvivorCharacter();
	m_origin = source->GetAbsOrigin(); m_velocity = source->GetAbsVelocity(); m_angles = source->GetAbsAngles();
	m_moveType = source->GetMoveType(); m_ladderNormal = source->GetLadderNormal();
	m_allowAutoMovement = source->m_Local.m_bAllowAutoMovement;
	m_ducked = source->m_Local.m_bDucked; m_ducking = source->m_Local.m_bDucking; m_duckFlag = ( source->GetFlags() & FL_DUCKING ) != 0;
	m_leftCheckpoint = source->m_hasLeftCheckpoint; m_spawnMusicPlayed = source->m_hasPlayedSpawnMusic;
	m_activity = source->GetTerrorAnimState()->GetTerrorActivity();
	m_specificSequence = source->GetTerrorAnimState()->GetSequenceOverride();
	m_cycle = source->GetCycle();
	m_health = source->GetHealth(); m_buffer = source->GetHealthBuffer(); m_reviveCount = source->m_currentReviveCount;
	m_alive = source->IsAlive(); m_dying = source->m_isGoingToDie;
	m_lastPrimaryId = source->m_lastPrimaryWeaponId; m_lastPrimaryAmmo = source->m_lastPrimaryWeaponAmmo;
	m_lastSecondaryId = source->m_lastSecondaryWeaponId; m_lastHadElites = source->m_lastHadElites;
	m_elevator = source->GetElevator();
	m_hidden = source->m_hHiddenWeapon;
	if ( m_hidden.Get() ) m_hidden->SetParent( NULL );
	m_active = source->GetActiveWeapon();
	if ( m_active ) m_activeWeaponHidden = m_active->IsEffectActive( EF_NODRAW );
	CaptureWeapons( source, m_weapons, m_ammo );
	m_incapacitated = source->m_isIncapacitated;
	m_reviver = ToTerrorPlayer( source->m_reviveOwner.Get() ); source->m_reviveOwner.GetForModify().Init( 0, 0 );
	m_reviveDirection = source->m_reviveStartDirection; m_revivePosition = source->m_reviveStartPosition;
	m_currentReviveCount = source->m_currentReviveCount; m_thirdStrike = source->m_bIsOnThirdStrike;
	m_savedHealth = source->m_savedHealth; m_savedBuffer = source->m_savedHealthBuffer;
	m_canGrabLedge = source->m_canGrabLedge; m_hanging = source->m_isHangingFromLedge; m_falling = source->m_isFallingFromLedge;
	m_hangTimer = source->m_hangTimer;
	m_hangAirPos = source->m_hangAirPos; m_hangPos = source->m_hangPos; m_hangStandPos = source->m_hangStandPos; m_hangNormal = source->m_hangNormal;
	m_isReleasingLedge = source->m_isReleasingLedge;
	m_hangFallPos = source->m_hangFallPos; m_hangClimbEndPos = source->m_hangClimbEndPos;
	m_hangSurface = source->m_hangSurface; m_hangSurfaceOrigin = source->m_hangSurfaceOrigin; m_hangSurfaceAngles = source->m_hangSurfaceAngles;
	m_itTimer = source->m_itTimer; m_wasIT = source->m_bWasIT; m_itUntilNextAttack = source->m_bIsITUntilNextAttack; m_healingGnome = source->m_bCarryingHealingGnome;
	m_vomitStart = source->m_vomitStart;
	m_pounceAttacker = ToTerrorPlayer( source->m_pounceAttacker.Get() ); source->m_pounceAttacker.GetForModify().Init( 0, 0 );
	m_pounceStart = source->m_pounceStartTimer;
	m_pummelAttacker = ToTerrorPlayer( source->m_pummelAttacker.Get() ); source->m_pummelAttacker.GetForModify().Init( 0, 0 );
	m_pummelObject = ToTerrorPlayer( source->m_pummelObject.Get() ); source->m_pummelObject = NULL;
	m_carryAttacker = ToTerrorPlayer( source->m_carryAttacker.Get() ); source->m_carryAttacker.GetForModify().Init( 0, 0 );
	m_tongueOwner = ToTerrorPlayer( source->m_tongueOwner.Get() ); source->m_tongueOwner.GetForModify().Init( 0, 0 );
	m_tongueVictimTimer = source->m_tongueVictimTimer;
	m_tongueHanging = source->m_isHangingFromTongue; m_tongueReached = source->m_reachedTongueOwner; m_tongueProne = source->m_isProneTongueDrag;
	m_tongueReasons = source->m_tongueChokeReasons; m_tonguePullTimer = source->m_tonguePullTimer; m_tongueChokeStart = source->m_tongueChokeStartTimer;
	m_jockeyAttacker = ToTerrorPlayer( source->m_jockeyAttacker.Get() ); if ( m_jockeyAttacker ) source->m_jockeyAttacker = NULL;
	CTerrorExpresser *sourceExpresser = static_cast< CTerrorExpresser * >( source->GetMultiplayerExpresser() );
	if ( sourceExpresser ) m_joinedScenario = sourceExpresser->GetSurvivorSpeech()->HasJoinedScenario();
	m_viewEntity = source->GetViewEntity(); m_positionEntity = source->m_positionEntity;
	source->SetSurvivorPosition( NULL );
	if ( m_positionEntity.Get() == source ) m_positionEntity = NULL;
	m_locked = source->IsPlayerLockedInPlace();
}
void SurvivorReplacementState::Apply( CTerrorPlayer *oldPlayer, CTerrorPlayer *target )
{
	target->HandleCommand_JoinTeam( TEAM_SURVIVOR, m_character, true );
	if ( m_alive )
	{
		target->m_hasPlayedSpawnMusic = oldPlayer->m_hasPlayedSpawnMusic;
		target->State_Transition( STATE_ACTIVE ); target->Spawn();
	}
	else target->State_Transition( STATE_OBSERVER_MODE );
	if ( m_alive )
	{
		target->SetHealth( m_health ); target->SetHealthBuffer( m_buffer ); target->SetReviveCount( m_reviveCount ); target->m_isGoingToDie = m_dying;
	}
	target->SetMoveType( m_moveType ); target->SetLadderNormal( m_ladderNormal );
	target->m_Local.m_bDucked = m_ducked; target->m_Local.m_bDucking = m_ducking;
	if ( m_duckFlag ) target->AddFlag( FL_DUCKING );
	target->m_Local.m_bAllowAutoMovement = m_allowAutoMovement;
	target->m_hasLeftCheckpoint = m_leftCheckpoint; target->m_hasPlayedSpawnMusic = m_spawnMusicPlayed;
	target->m_lastPrimaryWeaponId = m_lastPrimaryId; target->m_lastPrimaryWeaponAmmo = m_lastPrimaryAmmo;
	target->m_lastSecondaryWeaponId = m_lastSecondaryId; target->m_lastHadElites = m_lastHadElites; target->m_hHiddenWeapon = m_hidden;
	if ( m_elevator.Get() ) { static ConVarRef hack_elevator_z_offset( "hack_elevator_z_offset" ); m_origin.z += hack_elevator_z_offset.GetFloat(); }
	target->Teleport( &m_origin, &m_angles, &m_velocity );
	ApplyWeapons( target, m_weapons, m_active, m_ammo );
	if ( m_active && m_activeWeaponHidden && !target->IsBot() ) m_active->AddEffects( EF_NODRAW );
	target->m_isIncapacitated = m_incapacitated;
	target->m_reviveOwner = m_reviver;
	if ( m_reviver ) m_reviver->m_reviveTarget = target;
	target->m_reviveStartDirection = m_reviveDirection; target->m_reviveStartPosition = m_revivePosition;
	target->m_currentReviveCount = m_currentReviveCount; target->m_bIsOnThirdStrike = m_thirdStrike;
	target->m_savedHealth = m_savedHealth; target->m_savedHealthBuffer = m_savedBuffer;
	target->m_canGrabLedge = m_canGrabLedge; target->m_isHangingFromLedge = m_hanging; target->m_isFallingFromLedge = m_falling;
	target->m_hangTimer.CopyFrom( m_hangTimer );
	target->m_hangAirPos = m_hangAirPos; target->m_hangPos = m_hangPos; target->m_hangStandPos = m_hangStandPos; target->m_hangNormal = m_hangNormal;
	target->m_isReleasingLedge = m_isReleasingLedge;
	target->m_hangFallPos = m_hangFallPos; target->m_hangClimbEndPos = m_hangClimbEndPos;
	target->m_hangSurface = m_hangSurface; target->m_hangSurfaceOrigin = m_hangSurfaceOrigin; target->m_hangSurfaceAngles = m_hangSurfaceAngles;
	target->m_itTimer.CopyFrom( m_itTimer ); target->m_bWasIT = m_wasIT; target->m_bIsITUntilNextAttack = m_itUntilNextAttack; target->m_bCarryingHealingGnome = m_healingGnome;
	target->m_vomitStart = m_vomitStart;
	target->m_pounceAttacker = m_pounceAttacker;
	if ( m_pounceAttacker ) { m_pounceAttacker->m_pounceVictim = target; target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCED ); m_pounceAttacker->DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCE_ACQUIRE ); }
	target->m_pounceStartTimer = m_pounceStart;
	target->m_pummelAttacker = m_pummelAttacker;
	if ( m_pummelAttacker )
	{
		if ( m_pummelAttacker->m_pummelTarget.Get() ) m_pummelAttacker->m_pummelTarget = target;
		if ( m_pummelAttacker->m_pummelVictim.Get() ) m_pummelAttacker->m_pummelVictim = target;
		target->SetParent( m_pummelAttacker );
		m_pummelAttacker->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUND_START ); target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUNDED );
	}
	target->m_carryAttacker = m_carryAttacker;
	if ( m_carryAttacker )
	{
		m_carryAttacker->m_carryVictim = target;
		m_carryAttacker->m_jockeyMountTimer.Start( 0.2f );
		target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_CARRIED );
	}
	target->m_pummelObject = NULL;
	if ( m_pummelObject ) m_pummelObject->SetPummelTarget( target, -1.0f );
	target->SetJockeyAttacker( m_jockeyAttacker );
	if ( m_jockeyAttacker )
	{
		m_jockeyAttacker->SetJockeyVictim( target ); target->SetAbsOrigin( target->GetAbsOrigin() );
		m_jockeyAttacker->SetParent( target ); m_jockeyAttacker->SetMoveType( MOVETYPE_NONE );
		target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_JOCKEY_RIDDEN );
	}
	target->m_tongueOwner = m_tongueOwner;
	if ( m_tongueOwner )
	{
		m_tongueOwner->m_tongueVictim = target;
		CTongue *tongue = dynamic_cast< CTongue * >( m_tongueOwner->m_customAbility.Get() );
		if ( tongue ) tongue->ReplacePlayerReferences( oldPlayer, target );
		target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_GRABBED );
	}
	target->m_tongueVictimTimer.CopyFrom( m_tongueVictimTimer );
	target->m_isHangingFromTongue = m_tongueHanging; target->m_reachedTongueOwner = m_tongueReached; target->m_isProneTongueDrag = m_tongueProne;
	target->m_tongueChokeReasons = m_tongueReasons; target->m_tonguePullTimer = m_tonguePullTimer; target->m_tongueChokeStartTimer = m_tongueChokeStart;
	CTerrorExpresser *targetExpresser = static_cast< CTerrorExpresser * >( target->GetMultiplayerExpresser() );
	if ( targetExpresser ) targetExpresser->GetSurvivorSpeech()->SetHasJoinedScenario( m_joinedScenario );
	CBaseEntity *view = m_viewEntity.Get();
	if ( view == oldPlayer ) view = target;
	else if ( view ) ReplaceCameraPlayer( view, oldPlayer, target );
	target->SetViewEntity( view );
	if ( m_positionEntity.Get() ) target->SetSurvivorPosition( dynamic_cast< CSurvivorPosition * >( m_positionEntity.Get() ) );
	FOR_EACH_VEC( m_observers, i ) m_observers[i]->SetObserverTarget( target );
	target->UpdateLastKnownArea(); target->UpdateReplacementCollisionBounds(); target->UnstickAfterPin();
	if ( m_alive ) target->GetMusic()->OnPlayerRestored( oldPlayer );
	if ( m_locked ) target->LockPlayerInPlace(); else target->UnlockPlayer();
	if ( m_specificSequence != -1 )
	{
		target->GetTerrorAnimState()->ForceSequenceOverride( m_specificSequence );
	}
}

class InfectedReplacementState
{
public:
	void Capture( CTerrorPlayer *source );
	void Apply( CTerrorPlayer *oldPlayer, CTerrorPlayer *target );
private:
	int m_class, m_health;
	Vector m_origin, m_velocity;
	QAngle m_angles;
	float m_buffer;
	bool m_alive, m_incapacitated;
	int m_variant;
	CBaseCombatWeapon *m_active;
	CUtlVector< CBaseCombatWeapon * > m_weapons;
	int m_ammo[32];
	CTerrorPlayer *m_pounceVictim, *m_tongueVictim, *m_jockeyVictim, *m_pummelVictim, *m_pummelTarget, *m_carryVictim;
	int m_pounceDamageRemaining, m_bloodyHandsLevel;
	CBaseAbility *m_ability;
	bool m_onFire;
	CUtlVector< CTerrorPlayer * > m_observers;
};
void InfectedReplacementState::Capture( CTerrorPlayer *source )
{
	CollectReplacementObservers( source, m_observers );
	m_class = source->GetZombieClass(); m_origin = source->GetAbsOrigin(); m_velocity = source->GetAbsVelocity(); m_angles = source->GetAbsAngles();
	m_health = source->GetHealth(); m_buffer = source->GetHealthBuffer(); m_alive = source->IsAlive(); m_incapacitated = source->m_isIncapacitated;
	m_variant = source->m_nVariantType; m_active = source->GetActiveWeapon();
	CaptureWeapons( source, m_weapons, m_ammo );
	m_pounceVictim = ToTerrorPlayer( source->m_pounceVictim.Get() ); source->m_pounceVictim.GetForModify().Init( 0, 0 );
	m_pounceDamageRemaining = source->m_pounceDamageRemaining; m_bloodyHandsLevel = source->m_iBloodyHandsLevel;
	m_tongueVictim = ToTerrorPlayer( source->m_tongueVictim.Get() ); source->m_tongueVictim.GetForModify().Init( 0, 0 );
	m_jockeyVictim = ToTerrorPlayer( source->m_jockeyVictim.Get() ); source->m_jockeyVictim.GetForModify().Init( 0, 0 );
	m_pummelVictim = ToTerrorPlayer( source->m_pummelVictim.Get() ); source->m_pummelVictim.GetForModify().Init( 0, 0 );
	m_pummelTarget = ToTerrorPlayer( source->m_pummelTarget.Get() ); source->m_pummelTarget.Init( 0, 0 );
	m_carryVictim = ToTerrorPlayer( source->m_carryVictim.Get() ); source->m_carryVictim.GetForModify().Init( 0, 0 );
	m_ability = source->m_customAbility.Get(); source->m_customAbility.GetForModify().Init( 0, 0 );
	m_onFire = ( source->GetFlags() & FL_ONFIRE ) != 0;
}
void InfectedReplacementState::Apply( CTerrorPlayer *oldPlayer, CTerrorPlayer *target )
{
	target->HandleCommand_JoinTeam( TEAM_INFECTED, SurvivorCharacter_Unknown, true );
	if ( m_alive )
	{
		g_iNextZombieClass = m_class; g_iNextInfectedVariant = m_variant;
		target->State_Transition( STATE_ACTIVE ); target->Spawn();
		g_iNextInfectedVariant = 0;
		if ( target->m_customAbility.Get() ) UTIL_Remove( target->m_customAbility.Get() );
	}
	else target->State_Transition( STATE_OBSERVER_MODE );
	if ( m_alive ) { target->SetHealth( m_health ); target->SetHealthBuffer( m_buffer ); target->m_isIncapacitated = m_incapacitated; }
	ApplyWeapons( target, m_weapons, m_active, m_ammo );
	target->Teleport( &m_origin, &m_angles, &m_velocity );
	target->m_customAbility = m_ability;
	if ( m_ability ) m_ability->RebindOwner( target );
	target->m_iBloodyHandsLevel = m_bloodyHandsLevel;
	target->m_pounceVictim = m_pounceVictim;
	if ( m_pounceVictim )
	{
		m_pounceVictim->m_pounceAttacker = target; target->m_isAttemptingToPounce = false; target->m_pounceLanded = true;
		target->m_pounceDamageRemaining = m_pounceDamageRemaining;
		static ConVarRef z_pounce_damage_delay( "z_pounce_damage_delay" ); target->m_pounceDamageTimer.Start( z_pounce_damage_delay.GetFloat() );
		m_pounceVictim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCED ); target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCE_ACQUIRE );
	}
	target->m_tongueVictim = m_tongueVictim;
	if ( m_tongueVictim )
	{
		m_tongueVictim->m_tongueOwner = target;
		CTongue *tongue = dynamic_cast< CTongue * >( m_ability ); if ( tongue ) tongue->ReplacePlayerReferences( oldPlayer, target );
	}
	target->m_jockeyVictim = m_jockeyVictim;
	if ( m_jockeyVictim )
	{
		m_jockeyVictim->m_jockeyAttacker = target;
		target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_JOCKEY_RIDE ); m_jockeyVictim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_JOCKEY_RIDDEN );
	}
	target->m_pummelVictim = m_pummelVictim;
	if ( m_pummelVictim )
	{
		m_pummelVictim->m_pummelAttacker = target; m_pummelVictim->SetParent( target );
		target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUND_START ); m_pummelVictim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUNDED );
	}
	if ( m_pummelTarget )
	{
		// Native31A960 deliberately overwrites its snapshot slot57 with slot58,
		// then repeats the PUMMEL pair. Do not replace this with SetPummelTarget.
		m_pummelVictim = m_pummelTarget; target->m_pummelVictim = m_pummelVictim; m_pummelVictim->m_pummelAttacker = target;
		m_pummelVictim->SetParent( target ); target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUND_START ); m_pummelVictim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUNDED );
	}
	target->m_carryVictim = m_carryVictim;
	if ( m_carryVictim ) { m_carryVictim->m_carryAttacker = target; m_carryVictim->SetParent( target ); m_carryVictim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_CARRIED ); }
	if ( m_onFire ) target->Ignite( 9999.0f, false, 0.0f, false );
	FOR_EACH_VEC( m_observers, i ) m_observers[i]->SetObserverTarget( target );
}

void CTerrorPlayer::TakeOverTankBot( CTerrorPlayer *botTank ) // S32B400, SAME destructive snapshot owner.
{
	const bool crouched = botTank->m_Local.m_bDucking || ( botTank->GetFlags() & FL_DUCKING );
	SetParent( NULL );
	InfectedReplacementState state;
	state.Capture( botTank );
	botTank->ChangeTeam( TEAM_SPECTATOR );
	state.Apply( botTank, this );
	FireReplaceEvent( "bot_player_replace", this, botTank );
	if ( crouched )
	{
		m_afPhysicsFlags |= PFLAG_DUCKING;
		AddFlag( FL_DUCKING );
		m_Local.m_bDucked = true;
		m_Local.m_bDucking = true;
		m_Local.m_nDuckTimeMsecs = 0;
		SetViewOffset( TerrorGameRules()->GetViewVectors()->m_vDuckView );
		UTIL_SetSize( this, TerrorGameRules()->GetViewVectors()->m_vDuckHullMin,
			TerrorGameRules()->GetViewVectors()->m_vDuckHullMax );
	}
}

CTerrorPlayer *CTerrorPlayer::GetSpectatedBot() const
{
	// S304690/301AB0 selects the first real associated actor in index order.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() ) continue;
		if ( player->GetTeamNumber() == TEAM_SURVIVOR )
		{
			SurvivorBot *bot = dynamic_cast< SurvivorBot * >( player );
			if ( bot && bot->GetHumanSpectatorUserID() == GetUserID() ) return bot;
		}
		else if ( player->GetTeamNumber() == TEAM_INFECTED )
		{
			BossZombiePlayerBot *bot = dynamic_cast< BossZombiePlayerBot * >( player );
			if ( bot && bot->GetHumanSpectatorUserID() == GetUserID() ) return bot;
		}
	}
	return NULL;
}

bool CTerrorPlayer::TakeOverBot( bool force )
{
	CTerrorPlayer *associated = GetSpectatedBot();
	SurvivorBot *survivor = dynamic_cast< SurvivorBot * >( associated );
	BossZombiePlayerBot *infected = dynamic_cast< BossZombiePlayerBot * >( associated );
	if ( ( force || GetTeamNumber() == TEAM_SPECTATOR ) && survivor &&
		( TheDirector->GetSessionManager()->IsLevelActive() || m_bRestoringFromTransition ) )
	{
		const float idleDuration = gpGlobals->curtime - GetLastActivityTime();
		// Native398640(NULL) is blocked by its existing positive-userid guard;
		// do not substitute ClearHumanSpectator or add a null authorization path.
		survivor->SetHumanSpectator( (CTerrorPlayer *)NULL );
		SurvivorReplacementState state;
		state.Capture( survivor ); survivor->ChangeTeam( TEAM_SPECTATOR ); state.Apply( survivor, this );
		static ConVarRef director_afk_timeout( "director_afk_timeout" ); m_wasIdle = idleDuration >= director_afk_timeout.GetFloat();
		FireReplaceEvent( "bot_player_replace", this, survivor );
		return true;
	}
	if ( ( force || GetTeamNumber() == TEAM_SPECTATOR || GetTeamNumber() == TEAM_INFECTED ) && infected )
	{
		infected->SetHumanSpectator( (CTerrorPlayer *)NULL );
		InfectedReplacementState state;
		state.Capture( infected ); infected->ChangeTeam( TEAM_SPECTATOR ); state.Apply( infected, this );
		FireReplaceEvent( "bot_player_replace", this, infected );
		return true;
	}
	return false;
}
CTerrorPlayer *CTerrorPlayer::ReplaceWithBot( bool force )
{
	// S32B5F0 retains BOTH native actor families and all original guards.
	static ConVarRef nav_edit( "nav_edit" );
	if ( ( IsBot() && !force ) || ( ( !IsAlive() || IsGhost() ) && GetTeamNumber() == TEAM_INFECTED ) || nav_edit.GetBool() || TheDirector->IsSwappingTeams() ) return NULL;
	CTerrorPlayer *bot = NULL;
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		if ( TheDirector->AreSurvivorBotsDisabled() ) return NULL;
		SurvivorReplacementState state; state.Capture( this );
		const SurvivorCharacterType character = GetSurvivorCharacter();
		m_survivorCharacter = SurvivorCharacter_Unknown; SetModel( GetTerrorDefaultPlayerModel() );
		bot = NextBotCreatePlayerBot< SurvivorBot >( GetSurvivorBotName( character ) );
		if ( bot ) { if ( force ) ChangeTeam( TEAM_SPECTATOR ); state.Apply( this, bot ); }
		m_survivorCharacter = character;
	}
	else if ( GetTeamNumber() == TEAM_INFECTED )
	{
		InfectedReplacementState state; state.Capture( this );
		bot = CreateTerrorInfectedPlayerBot( GetZombieClass() );
		if ( bot ) state.Apply( this, bot );
	}
	else return NULL;
	if ( bot ) FireReplaceEvent( "player_bot_replace", this, bot );
	return bot;
}
void CTerrorPlayer::UpdateReplacementCollisionBounds()
{
	// S2C9810: ledge uses the standing hull even when FL_DUCKING is set.
	const CViewVectors *vectors = TerrorGameRules()->GetViewVectors();
	if ( m_isHangingFromLedge ) UTIL_SetSize( this, vectors->m_vHullMin, vectors->m_vHullMax );
	else if ( GetFlags() & FL_DUCKING ) UTIL_SetSize( this, vectors->m_vDuckHullMin, vectors->m_vDuckHullMax );
	else
	{
		Vector maxs = vectors->m_vHullMax;
		if ( m_jockeyVictim.Get() ) maxs.z += 32.0f;
		UTIL_SetSize( this, vectors->m_vHullMin, maxs );
	}
}

void CTerrorPlayer::ChangeTeam( int team )
{
	// S335BD0. Disconnection reaches this through the real base rules/player
	// chain. Transfer the still-live actor BEFORE the inherited team teardown.
	const int oldTeam = GetTeamNumber();
	if ( oldTeam != team )
	{
		if ( oldTeam == TEAM_SURVIVOR || oldTeam == TEAM_L4D1_SURVIVOR ||
			team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
			UpdatePotentiallyVisibleAreas( NULL, NULL );
		// The native inline transfer is exactly S32B5F0's force=false path,
		// including its actor guards, survivor disable guard and replace event.
		CTerrorPlayer *replacement = ReplaceWithBot( false );
		if ( TerrorGameRules()->IsPlayerControlledZombies() && replacement &&
			replacement->IsBot() && replacement->GetTeamNumber() == TEAM_INFECTED )
		{
			if ( replacement->GetZombieClass() == 8 ) TheDirector->OfferTank( replacement, false );
			else replacement->TakeDamage( CTakeDamageInfo( this, this, (float)replacement->GetHealth(), DMG_GENERIC ) );
		}
	}
	BaseClass::ChangeTeam( team );
	if ( oldTeam == TEAM_SURVIVOR || oldTeam == TEAM_L4D1_SURVIVOR )
	{
		ResponseQueueManager().RemoveExpresser( GetExpresser() );
		if ( TheDirector->GetSessionManager()->IsStartAreaExitBlocked() )
			DeactivateMovementConstraint();
	}
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		ResponseQueueManager().AddExpresser( GetExpresser() );
	if ( team == TEAM_SURVIVOR || team == TEAM_INFECTED )
		m_iVersusTeam = TerrorGameRules()->GetScavengeTeamIndex( team );
}

#include "l4d2/player/terrorplayer_replacement_console_server_callbacks.inc"
