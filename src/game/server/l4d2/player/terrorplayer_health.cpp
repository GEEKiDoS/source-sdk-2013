//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/terror_weapon.h"
#include "KeyValues.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/effects/survivor_death_model.h"
#include "nav_area.h"
#include "in_buttons.h"
#include "ai_speech.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "ai_criteria.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "activitylist.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer_health_console_server_cvars.inc"
#include <float.h>
#include "tier0/memdbgon.h"

ConVar z_survivor_respawn_health( "z_survivor_respawn_health", "50", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL, "How much health does a respawned survivor get?" );
ConVar survivor_incap_health( "survivor_incap_health", "300", 0x00002006 );
ConVar survivor_incap_decay_rate( "survivor_incap_decay_rate", "3", 0x00002006 );
ConVar survivor_incap_tongued_decay_rate( "survivor_incap_tongued_decay_rate", "12", 0x00000006 );
ConVar survivor_incap_hopeless_decay_rate( "survivor_incap_hopeless_decay_rate", "12", 0x00000006, "If all survivors are incapacitated, their health decays at this speed" );
static ConVar survivor_revive_health( "survivor_revive_health", "30", 0x00004004, "How much temp health you get revived with." );
ConVar survivor_ledge_grab_health( "survivor_ledge_grab_health", "300", 0x00002006 );
static ConVar survivor_death_anims( "survivor_death_anims", "0", 0x00000006 );
ConVar survivor_ledge_grab_ground_check_time( "survivor_ledge_grab_ground_check_time", "3.0", 0x00004004 );
extern ConVar z_noise_level_vocalize;
extern ConVar z_leap_interval_post_incap;
static ConVar survivor_intensity_recent_enemy_duration( "survivor_intensity_recent_enemy_duration", "5", 0x00004004 );
static ConVar defibrillator_return_to_life_time( "defibrillator_return_to_life_time", "3.0", 0x00004004 );

// Original factory metadata identifies info_hang_lighting as CPointEntity.
LINK_ENTITY_TO_CLASS( info_hang_lighting, CPointEntity );

static bool IsSurvivorTeam( const CTerrorPlayer *player )
{
	return player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
}

static void FireIncapacitatedEvent( CTerrorPlayer *player, const CTakeDamageInfo &info, bool start )
{
	IGameEvent *event = gameeventmanager->CreateEvent( start ? "player_incapacitated_start" : "player_incapacitated" );
	if ( start )
		++player->m_checkpointIncaps;
	if ( !event )
		return;
	event->SetInt( "userid", player->GetUserID() );
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker && attacker->IsPlayer() )
	{
		event->SetInt( "attacker", ToBasePlayer( attacker )->GetUserID() );
		const char *weapon = "";
		CBaseEntity *inflictor = info.GetInflictor();
		if ( inflictor && inflictor == attacker )
		{
			CBaseCombatWeapon *active = ToBasePlayer( attacker )->GetActiveWeapon();
			if ( active )
				weapon = active->GetClassname();
		}
		else if ( inflictor )
		{
			CTerrorPlayer *terror = ToTerrorPlayer( inflictor );
			weapon = terror && terror->GetTeamNumber() == TEAM_INFECTED && terror->m_zombieClass == 2 ? "boomer" : inflictor->GetClassname();
		}
		if ( !Q_strncmp( weapon, "weapon_", 7 ) ) weapon += 7;
		else if ( !Q_strncmp( weapon, "pipe_bomb", 9 ) ) weapon = "pipe_bomb";
		else if ( !Q_strncmp( weapon, "flashbang", 9 ) ) weapon = "flashbang";
		else if ( !Q_strncmp( weapon, "smokegrenade", 12 ) ) weapon = "smokegrenade";
		event->SetString( "weapon", weapon );
	}
	else if ( attacker )
	{
		event->SetInt( "attackerentid", attacker->entindex() );
	}
	event->SetInt( "type", info.GetDamageType() );
	gameeventmanager->FireEvent( event );
}

void CTerrorPlayer::ResetSurvivorHealth()
{
	m_isIncapacitated = false;
	m_bIsOnThirdStrike = false;
	m_bIsFirstManOut = false;
	m_currentReviveCount = 0;
	m_bAdrenalineActive = false;
	m_adrenalineTimer.Invalidate();
	m_incapacitatedTimer.Invalidate();
	m_incapacitatedTimestamp.Invalidate();
	m_incapacitatedByAttacker = NULL;
	m_isHangingFromLedge = false;
	m_isFallingFromLedge = false;
	m_hangTimer.Invalidate();
	m_ledgeGroundCheckTimer.Invalidate();
	m_savedHealth = 0;
	m_savedHealthBuffer = 0;
	m_hangStandArea = NULL;
	m_isReleasingLedge = false;
	m_defibrillatorReturnToLifeTimer.Invalidate();
	m_totalReviveCount = 0;
	m_isBleedingOut = false;
	m_downAndBeatenTimer.Invalidate();
	m_canGrabLedge = false;
	RemoveHangLighting();
	if ( m_deathEvent )
	{
		m_deathEvent->deleteThis();
		m_deathEvent = NULL;
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:0x32EB40 is reached from the native pre-application damage gate (332C20), not
// after base damage has killed a player. Incapacitation remains LIFE_ALIVE.
void CTerrorPlayer::OnIncapacitated( const CTakeDamageInfo &info )
{
	bool releasedFromSpecial = false; // native [ebp-219h]

	LogTerrorDamage( info, 1 );
	FireIncapacitatedEvent( this, info, true );
	m_savedHealth = m_isHangingFromLedge ? GetHealth() : 0;
	m_savedHealthBuffer = m_isHangingFromLedge ? (int)GetHealthBuffer() : 0;

	// 177560 is CBasePlayer::ClearUseEntity, then the real 312A80 setter. There is
	// no StopUseAction here and no direct handle write.
	ClearUseEntity();
	SetPushEntity( NULL );

	// 32EBD5..32EC6C: the JOCKEY's release (313220, no rescuer), then the jockey's
	// own ability recharge. This precedes EndReviving.
	if ( CTerrorPlayer *jockey = ToTerrorPlayer( m_jockeyAttacker.Get() ) )
	{
		jockey->ReleaseJockey( NULL );
		releasedFromSpecial = true;
		if ( CBaseAbility *ability = jockey->GetCustomAbility() )
			ability->SetRechargeTime( z_leap_interval_post_incap.GetFloat(), 0.0f );
	}

	// 32EC75: exactly one EndReviving(true), on this player. The native does not
	// end the reviver's (m_reviveOwner) revive here.
	EndReviving( true );

	m_isIncapacitated = true;
	m_incapacitatedByAttacker = info.GetAttacker();
	m_incapacitatedTimestamp.Start();

	// 32ED3C..32ED8B: the native inlines exactly S:3051C0 (identity of this, then of NULL,
	// the second unused by the format). Both formatter calls happen inside the shared function.
	LogIncapEvent( "%s is incapacitated", this, NULL );

	SetHealth( survivor_incap_health.GetInt() );
	m_incapacitatedTimer.Start( 1.0f );
	m_incapBloodPoolPlaced = false; // native byte 14984; reader is the 34A290 bleed tail
	ClearStagger();
	m_helpTimer.Start( RandomFloat( 1.0f, 3.0f ) );

	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
	// Fixed original IDs: pistol1 and magnum32 are the only exceptions here.
	if ( weapon && weapon->GetWeaponID() != 1 && weapon->GetWeaponID() != 32 )
	{
		weapon->SetForcedDropAttacker( info.GetAttacker() );
		weapon->Holster( NULL );
		weapon->SetForcedDropAttacker( NULL );
	}
	EquipIncapacitatedWeapon();
	SetNoiseLevel( z_noise_level_vocalize.GetFloat() ); // v2124 = 314000

	char criteria[256];
	Q_strncpy( criteria, "PainLevel:Critical", sizeof( criteria ) );

	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker && attacker->IsPlayer() && attacker->GetTeamNumber() == TEAM_INFECTED && IsSurvivorTeam( this ) )
	{
		CTerrorPlayer *infectedAttacker = ToTerrorPlayer( attacker );
		Q_strncat( criteria, ",SubjectIsA:", sizeof( criteria ) );
		Q_strncat( criteria, GetZombieClassName( infectedAttacker->GetZombieClass() ), sizeof( criteria ) );
		infectedAttacker->RecordSurvivorIncap( GetSurvivorCharacter() ); // 3224F0 at 32F0C7
	}

	// 32F100: the native dereferences the attacker without a NULL check. User decision
	// (2026-10-22): a NULL attacker is treated as a non-player here.
	CBaseEntity *inflictor = info.GetInflictor();
	if ( attacker && attacker->IsPlayer() )
	{
		// 32F178: the native repeats this virtual and discards the result.
		attacker->IsPlayer();
	}
	else if ( inflictor && inflictor->IsPlayer() )
	{
		// 304610 functor {type 13, victim this, attacker inflictor, assist 0, 0, byte 0}.
		SendPZDamageMessage( 13, this, ToTerrorPlayer( inflictor ), NULL );
	}

	const bool wasOnFire = IsOnFire();
	if ( info.GetDamageType() & DMG_BURN )
	{
		AddFlag( FL_ONFIRE );
		Q_strncat( criteria, ",DamageType:DMG_BURN", sizeof( criteria ) );
	}
	SpeakIfAllowed( "PlayerIncapacitated", 1, criteria );
	if ( !wasOnFire )
		RemoveFlag( FL_ONFIRE );
	m_vocalizeTimer.Start( 5.0f );

	// 32F27D: native GetTeamNumber() on a possibly NULL attacker. User decision
	// (2026-10-22): a NULL attacker is treated as team != 2.
	if ( !attacker || attacker->GetTeamNumber() != TEAM_SURVIVOR ||
		survivor_intensity_recent_enemy_duration.GetFloat() > GetTimeSinceEnemyInjury() )
	{
		m_survivorIntensity.Add( m_currentReviveCount ? 4 : 5 );
	}

	Q_strncat( criteria, ",IncapacitatedCharacter:", sizeof( criteria ) );
	Q_strncat( criteria, GetResponseCharacterName(), sizeof( criteria ) );
	SpeakConceptToSurvivors( this, "SurvivorIncapacitated", criteria );

	GetMusic()->OnDown();
	FireIncapacitatedEvent( this, info, false );

	// 32F37D..32F394: test/jg/cmp 1/jnz. The call is reachable only when the witch
	// count is both <= 0 and == 1, so it never executes; the native guard is kept.
	const int witchCount = TheDirector->GetWitchCount();
	if ( witchCount <= 0 && witchCount == 1 )
		RequestSurvivorBotAssistance();

	// 275B20 = scripted-mode enabled byte (+1) && option != 0.
	if ( !TheDirector->GetScriptedMode()->QueryOptionBool( "cm_AutoReviveFromSpecialIncap", false ) )
		return;

	if ( CTerrorPlayer *hunter = ToTerrorPlayer( m_pounceAttacker.Get() ) )
	{
		hunter->ReleasePounce();
		releasedFromSpecial = true;
	}
	if ( CTerrorPlayer *charger = ToTerrorPlayer( m_carryAttacker.Get() ) )
	{
		charger->ReleaseCarry( true, false, false );
		releasedFromSpecial = true;
	}
	if ( CTerrorPlayer *smoker = ToTerrorPlayer( m_tongueOwner.Get() ) )
	{
		// An unresolvable smoker victim releases nothing but still marks the release.
		if ( CTerrorPlayer *tongueVictim = ToTerrorPlayer( smoker->m_tongueVictim.Get() ) )
		{
			CTongue *tongue = dynamic_cast< CTongue * >( smoker->GetCustomAbility() );
			if ( tongue )
				tongue->ResetTongueCooldown();
			IGameEvent *event = gameeventmanager->CreateEvent( "tongue_release" );
			if ( event )
			{
				event->SetInt( "userid", smoker->GetUserID() );
				event->SetInt( "victim", tongueVictim->GetUserID() );
				if ( tongue )
				{
					const Vector &origin = tongueVictim->GetAbsOrigin();
					const Vector &grabPosition = tongue->GetGrabPosition();
					const float dx = grabPosition.x - origin.x;
					const float dy = grabPosition.y - origin.y;
					const float dz = grabPosition.z - origin.z;
					const float distanceSquared = ( dx * dx + dy * dy ) + dz * dz;
					event->SetInt( "distance", _mm_cvttss_si32( _mm_sqrt_ss( _mm_set_ss( distanceSquared ) ) ) );
				}
				else
				{
					event->SetInt( "distance", 0 );
				}
				gameeventmanager->FireEvent( event );
			}
			if ( CTerrorPlayer *releasedVictim = ToTerrorPlayer( smoker->m_tongueVictim.Get() ) )
				releasedVictim->ReleaseTongueVictim();
			smoker->ReleaseTongueOwner();
		}
		releasedFromSpecial = true;
	}

	if ( CTerrorPlayer *pummeler = ToTerrorPlayer( m_pummelAttacker.Get() ) )
	{
		pummeler->ReleasePummel( true, NULL );
	}
	else if ( !releasedFromSpecial )
	{
		return;
	}

	OnRevived(); // v2332 = 31F900
	m_incapacitationRecoveryTimer.Start( 5.0f );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
// S:0x32E7A0. Tank incapacitation is not survivor incapacitation or death.
void CTerrorPlayer::OnTankIncapacitated( const CTakeDamageInfo &info )
{
	static const Activity climbActivities[] =
	{
		(Activity)ActivityList_IndexForName( "ACT_TERROR_LEDGE_CLIMB" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_LEDGE_CLIMB_TO_CROUCH" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_36_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_38_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_50_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_70_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_115_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_130_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_150_FROM_STAND" ),
		(Activity)ActivityList_IndexForName( "ACT_TERROR_CLIMB_166_FROM_STAND" )
	};
	const Activity activity = GetTerrorAnimState()->GetCurrentMainActivity();
	for ( int i = 0; i < ARRAYSIZE( climbActivities ); ++i )
	{
		if ( climbActivities[i] != ACT_INVALID && activity == climbActivities[i] )
		{
			SetHealth( 1 );
			return;
		}
	}
	m_deathEvent = new KeyValues( "deathEvent" );
	TerrorGameRules()->PopulateDeathEvent( m_deathEvent, info.GetAttacker(), this, info );
	LogTerrorDamage( info, 1 );
	m_savedHealth = 0;
	m_savedHealthBuffer = 0;
	m_isIncapacitated = true;
	m_incapacitatedByAttacker = info.GetAttacker();
	m_incapacitatedTimestamp.Start();
	FireIncapacitatedEvent( this, info, false );
	static ConVarRef health( "z_tank_incapacitated_health" );
	const int incapHealth = health.GetInt();
	const int currentHealth = GetHealth();
	SetHealth( (int)( (double)GetHealthBuffer() + currentHealth - m_iDamageHealth + incapHealth ) );
	EmitSound( "HulkZombie.Die" );
	if ( CBaseCombatWeapon *weapon = GetActiveWeapon() )
		weapon->Holster( NULL );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_DYING );
}
// S:0x300FD0. This is the local bot response deadline, not adrenaline.
bool CTerrorPlayer::IsRespondingToSurvivorDistress() const
{
	const float deadline = m_survivorDistressResponseTimer.m_timestamp.Get();
	return deadline > 0.0f && gpGlobals->curtime < deadline;
}

// S:0x30A210. Preserve the original slot order, strict 1800-unit range,
// human precedence for bot requesters, and absence of an alive candidate gate.
CTerrorPlayer *CTerrorPlayer::RequestSurvivorBotAssistance()
{
	if ( IsBot() )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !player || !player->edict() || !player->entindex() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR )
				continue;
			if ( player->IsBot() || !player->IsAlive() )
				continue;
			const bool pinned = player->m_pummelAttacker.Get() || player->m_pounceAttacker.Get() ||
				player->GetTongueOwner() || player->m_jockeyAttacker.Get() || player->m_carryAttacker.Get();
			if ( player->IsIncapacitated() || player->m_isHangingFromLedge || pinned )
				return NULL;
		}
	}
	CTerrorPlayer *selected = NULL;
	float bestRangeSquared = 3240000.0f;
	const Vector &from = WorldSpaceCenter();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		if ( !player->IsBot() || player == this || player->IsIncapacitated() )
			continue;
		const bool pinned = player->m_pummelAttacker.Get() || player->m_pounceAttacker.Get() ||
			player->GetTongueOwner() || player->m_jockeyAttacker.Get() || player->m_carryAttacker.Get();
		if ( pinned || player->IsIT() )
			continue;
		const Vector &to = player->WorldSpaceCenter();
		const float rangeSquared = ( ( to.y - from.y ) * ( to.y - from.y ) + ( to.x - from.x ) * ( to.x - from.x ) ) +
			( to.z - from.z ) * ( to.z - from.z );
		if ( bestRangeSquared > rangeSquared )
		{
			bestRangeSquared = rangeSquared;
			selected = player;
		}
	}
	if ( selected )
		selected->m_survivorDistressResponseTimer.Start( 20.0f );
	return selected;
}

// S:2CB2D0. First serial-valid native handle, including raw zero resolving to world.
CBaseEntity *CTerrorPlayer::GetDominatingAttacker() const
{
	CBaseEntity *attacker = m_pummelAttacker.Get();
	if ( attacker ) return attacker;
	attacker = m_pounceAttacker.Get();
	if ( attacker ) return attacker;
	attacker = m_tongueOwner.Get();
	if ( attacker ) return attacker;
	attacker = m_jockeyAttacker.Get();
	if ( attacker ) return attacker;
	return m_carryAttacker.Get();
}



void CTerrorPlayer::RemoveHangLighting()
{
	CBaseEntity *lighting = GetLightingOrigin();
	if ( lighting && lighting->ClassMatches( "info_hang_lighting" ) )
		UTIL_Remove( lighting );
	SetLightingOrigin( (CBaseEntity *)NULL );
}

// S316640 prepares geometry; actual hanging begins separately in S320710.
void CTerrorPlayer::PrepareLedgeGrab( const Vector &hangPosition, const Vector &standPosition,
	const Vector &fallPosition, const Vector &normal, const Vector &climbEndPosition,
	CBaseEntity *surface, CNavArea *standArea )
{
	m_canGrabLedge = true;
	m_hangPos = hangPosition;
	m_hangFallPos = fallPosition;
	m_hangStandPos = standPosition;
	m_hangNormal = normal;
	m_hangClimbEndPos = climbEndPosition;
	m_hangSurface = surface;
	// The native caller supplies the real surface, including the world.
	m_hangSurfaceOrigin = surface->GetAbsOrigin();
	m_hangSurfaceAngles = surface->GetAbsAngles();
	m_hangStandArea = standArea;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:0x320710. Movement supplies the actual geometry and can-grab bit, just as
// S:0x2A81B0/0x2A8F80 do; this method does not manufacture a valid ledge.
void CTerrorPlayer::GrabLedge( const Vector &airPosition )
{
	if ( !IsAlive() || IsIncapacitated() || !IsSurvivorTeam( this ) || !m_canGrabLedge || !m_ledgeHangingEnabled )
		return;
	ClearUseEntity();
	SetPushEntity( NULL );
	m_canGrabLedge = false;
	m_isHangingFromLedge = true;
	RemoveHangLighting();
	CBaseEntity *lighting = CreateEntityByName( "info_hang_lighting" );
	if ( lighting )
	{
		lighting->SetAbsOrigin( m_hangStandPos );
		SetLightingOrigin( lighting );
	}
	m_isFallingFromLedge = false;
	m_hangAirPos = airPosition;
	SetMoveType( MOVETYPE_CUSTOM );
	m_hangTimer.Start();
	m_isReleasingLedge = false;
	m_ledgeGroundCheckTimer.Start( survivor_ledge_grab_ground_check_time.GetFloat() );
	const Vector &standing = m_lastStandingPosition;
	const Vector &hang = m_hangPos.Get();
	const Vector &stand = m_hangStandPos.Get();
	UTIL_LogPrintf( "%s grabs a ledge - last standing at (%f, %f, %f).  hang pos is (%f, %f, %f).  fall pos is (%f, %f, %f).  stand target is (%f, %f, %f).  landing target is (%f, %f, %f).  IsTongueVictim() == %d, IsBeingLunged() == %d\n",
		GetPlayerName(), standing.x, standing.y, standing.z, hang.x, hang.y, hang.z,
		m_hangFallPos.x, m_hangFallPos.y, m_hangFallPos.z, stand.x, stand.y, stand.z,
		m_hangClimbEndPos.x, m_hangClimbEndPos.y, m_hangClimbEndPos.z, IsTongueVictim() ? 1 : 0,
		( m_staggerSourceTimer.m_timestamp.Get() > 0.0f &&
			static_cast< double >( gpGlobals->curtime ) < static_cast< double >( m_staggerSourceTimer.m_timestamp.Get() ) ) ? 1 : 0 );
	EndReviving( true );
	m_isIncapacitated = true;
	m_savedHealth = GetHealth();
	m_savedHealthBuffer = static_cast< int >( GetHealthBuffer() );
	SetHealth( survivor_ledge_grab_health.GetInt() );
	m_incapacitatedTimer.Start( 1.0f );
	if ( GetActiveWeapon() )
		GetActiveWeapon()->Holster( NULL );
	if ( IsJockeyVictim() )
		ToTerrorPlayer( m_jockeyAttacker.Get() )->ReleaseJockey( NULL );
	if ( IsTongueVictim() )
		GetTongueOwner()->ReleaseTongue( false );
	SetNoiseLevel( z_noise_level_vocalize.GetFloat() );
	SpeakIfAllowed( "PlayerLedgeHangStart" );
	m_vocalizeTimer.Start( 5.0f );
	GetMusic()->OnLedgeHangTwoHands();
	if ( !m_staggerSourceTimer.IsElapsed() || !m_tongueReleaseTimer.IsElapsed() )
		m_survivorIntensity.Add( 3 );
	char criteria[256] = {};
	Q_strncat( criteria, "HangingCharacter:", sizeof( criteria ) );
	Q_strncat( criteria, GetResponseCharacterName(), sizeof( criteria ) );
	SpeakConceptToSurvivors( this, "SurvivorGrabbedLedge", criteria );
	CBaseEntity *causer = GetLedgeCauser();
	IGameEvent *event = gameeventmanager->CreateEvent( "player_ledge_grab" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		if ( causer && causer->IsPlayer() )
			event->SetInt( "causer", ToBasePlayer( causer )->GetUserID() );
		else if ( GetTongueReleaseSource() )
			event->SetInt( "causer", ToBasePlayer( GetTongueReleaseSource() )->GetUserID() );
		else if ( IsTongueVictim() )
			event->SetInt( "causer", GetTongueOwner()->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	RequestSurvivorBotAssistance();
	if ( causer && GetTeamNumber() == TEAM_SURVIVOR && causer->GetTeamNumber() == TEAM_INFECTED && ToTerrorPlayer( causer ) )
		ToTerrorPlayer( causer )->RecordDamageToSurvivor( 50, false, GetSurvivorCharacter() );
	RequestSurvivorBotAssistance();
}

class CSurvivorLedgeTraceFilter : public CTraceFilterSimple
{
public:
	CSurvivorLedgeTraceFilter( int team ) : CTraceFilterSimple( NULL, COLLISION_GROUP_NONE ), m_team( team ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE
		{
			if ( !CTraceFilterSimple::ShouldHitEntity( handle, mask ) )
				return false;
			CBaseEntity *entity = EntityFromEntityHandle( handle );
			return entity && entity->GetTeamNumber() != m_team;
		}
private:
	int m_team;
};

// S:0x3025A0 first checks occupancy, then drops the hull exactly 35.5 units.
static bool FindLedgeGround( Vector &position, const Vector &mins, const Vector &maxs, int team )
{
	CSurvivorLedgeTraceFilter filter( team );
	trace_t trace;
	UTIL_TraceHull( position, position, mins, maxs, 0x0201420bu, &filter, &trace );
	if ( trace.fraction != 1.0f || trace.startsolid || trace.allsolid )
		return false;
	UTIL_TraceHull( position, position - Vector( 0, 0, 35.5f ), mins, maxs, 0x0200400bu, &filter, &trace );
	position = trace.endpos;
	return trace.fraction != 1.0f && !( trace.plane.normal.z < 0.7f );
}

Vector CTerrorPlayer::FindLedgeRevivePosition() const
{
	const Vector mins = m_Local.m_bDucked ? g_pGameRules->GetViewVectors()->m_vDuckHullMin : g_pGameRules->GetViewVectors()->m_vHullMin;
	const Vector maxs = m_Local.m_bDucked ? g_pGameRules->GetViewVectors()->m_vDuckHullMax : g_pGameRules->GetViewVectors()->m_vHullMax;
	Vector position = GetAbsOrigin();
	if ( FindLedgeGround( position, mins, maxs, GetTeamNumber() ) ) return position;
	position.z += 18.0f;
	if ( FindLedgeGround( position, mins, maxs, GetTeamNumber() ) ) return position;
	position.z += 18.0f;
	if ( FindLedgeGround( position, mins, maxs, GetTeamNumber() ) ) return position;
	const Vector offsets[] = { Vector( 18, 0, 18 ), Vector( -18, 0, 18 ), Vector( 0, 18, 18 ), Vector( 0, -18, 18 ) };
	for ( int i = 0; i < ARRAYSIZE( offsets ); ++i )
	{
		position = GetAbsOrigin() + offsets[i];
		if ( FindLedgeGround( position, mins, maxs, GetTeamNumber() ) ) return position;
	}
	const float distance = g_pGameRules->GetViewVectors()->m_vHullMax.x * 2.0f - 1.0f;
	if ( m_hangStandArea && !m_hangStandArea->IsBlocked( TEAM_SURVIVOR ) )
	{
		for ( int i = -1; i < NUM_CORNERS; ++i )
		{
			Vector direction = ( i < 0 ? m_hangStandArea->GetCenter() : m_hangStandArea->GetCorner( (NavCornerType)i ) ) - m_hangStandPos;
			// S4EA090 retains the wide length for the comparison, but spills it
			// before the float reciprocal used to normalize the direction.
			const double length = sqrt( ( static_cast< double >( direction.x ) * direction.x +
				static_cast< double >( direction.y ) * direction.y ) +
				static_cast< double >( direction.z ) * direction.z );
			const float roundedLength = static_cast< float >( length );
			const float inverseLength = 1.0f / ( roundedLength + FLT_EPSILON );
			direction *= inverseLength;
			const float step = static_cast< double >( distance ) > length ? roundedLength : distance;
			position = m_hangStandPos + direction * step + Vector( 0, 0, 18 );
			if ( FindLedgeGround( position, mins, maxs, GetTeamNumber() ) ) return position;
		}
	}
	return m_hangStandPos;
}

// S2FC2D0 is distinct from the periodic ground-probe release S316920.
void CTerrorPlayer::SelfReleaseLedge()
{
	m_isReleasingLedge = true;
	OnRevived();
}

void CTerrorPlayer::ReleaseLedge()
{
	if ( !m_isHangingFromLedge || m_reviveOwner.Get() )
		return;
	const Vector start = m_hangPos + m_hangNormal * 12.0f;
	trace_t trace;
	CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_PLAYER_MOVEMENT );
	UTIL_TraceHull( start, start - Vector( 0, 0, 100 ), Vector( -2, -2, 0 ), Vector( 2, 2, 1 ), PlayerSolidMask(), &filter, &trace );
	if ( !trace.startsolid && !( trace.fraction < 1.0f && trace.plane.normal.z >= 0.7f ) )
		return;
	SetHealth( m_savedHealth );
	SetHealthBuffer( (float)m_savedHealthBuffer );
	GetMusic()->OnSavedFromLedgeHang();
	SetAbsOrigin( m_hangPos );
	IGameEvent *event = gameeventmanager->CreateEvent( "player_ledge_release" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	m_isIncapacitated = false;
	m_incapacitatedByAttacker = NULL;
	m_incapacitatedTimestamp.Invalidate();
	m_isHangingFromLedge = false;
	RemoveHangLighting();
	m_isFallingFromLedge = false;
	m_isReleasingLedge = false;
	SetMoveType( MOVETYPE_WALK );
}

// Return true only when the original late OnTakeDamage ledge branch consumed
// the damage. This is a fall transition; it is not an immediate death/corpse.
bool CTerrorPlayer::OnLedgeBleedOut( const CTakeDamageInfo &info )
{
	if ( !m_isHangingFromLedge || !( info.GetDamageType() & 0x00020000u ) )
		return false;
	// S33369B..3336A8: no float spill before the ordered <= 0 test.
	const float damage = info.GetDamage();
	const float buffer = GetHealthBuffer();
	const double remaining = static_cast< double >( buffer ) + GetHealth() - damage;
	if ( !( remaining <= 0.0 ) )
		return false;
	if ( IsTongueVictim() )
	{
		DebugAbilityVictim( GetTongueOwner(), this, "Tongue letting go on victim falling." );
		// The debug callback precedes the native re-read; skip only a lost owner.
		if ( CTerrorPlayer *owner = GetTongueOwner() )
			owner->ReleaseTongue( false );
	}
	SetAbsOrigin( m_hangFallPos );
	SetAbsVelocity( Vector( 0, 0, -1 ) );
	SetHealth( 1 );
	SetHealthBuffer( 0.0f );
	m_isHangingFromLedge = false;
	RemoveHangLighting();
	m_isIncapacitated = false;
	m_incapacitatedByAttacker = NULL;
	m_incapacitatedTimestamp.Invalidate();
	m_isFallingFromLedge = true;
	m_isReleasingLedge = false;
	SetMoveType( MOVETYPE_WALK );
	LogIncapEvent( "%s bled out and starts falling", this, NULL );
	SetNoiseLevel( z_noise_level_vocalize.GetFloat() );
	EmitSound( "Player.Fall" );
	m_vocalizeTimer.Start( 5.0f );
	GetMusic()->OnFellToDeathFromLedgeHang();
	const int lostHealth = static_cast< int >( static_cast< unsigned int >( m_savedHealthBuffer ) +
		static_cast< unsigned int >( m_savedHealth ) );
	if ( lostHealth > 1 )
		RecordDamageTaken( "world", lostHealth );
	CBaseEntity *camera = gEntList.FindEntityByClassnameNearest( "point_deathfall_camera", GetAbsOrigin(), 10000.0f );
	if ( camera && IsAbleToSee( camera, CBaseCombatCharacter::DISREGARD_FOV ) )
	{
		variant_t value;
		camera->AcceptInput( "Enable", this, NULL, value, 0 );
	}
	return true;
}

// S:0x31F900 runs on the patient while the real owner's handles still exist.
void CTerrorPlayer::OnRevived()
{
	if ( !IsAlive() || !CanBeRevived() )
		return;
	const bool ledge = m_isHangingFromLedge;
	const bool selfLedgeRelease = ledge && !m_reviveOwner.Get() && m_isReleasingLedge;
	if ( ledge )
	{
		if ( selfLedgeRelease ) GetMusic()->OnDroppedFromLedgeHang();
		else GetMusic()->OnSavedFromLedgeHang();
	}
	CTerrorPlayer *owner = ToTerrorPlayer( m_reviveOwner.Get() );
	if ( !owner ) owner = this;
	++owner->m_checkpointReviveOtherCount;
	if ( m_downAndBeatenTimer.HasStarted() && m_downAndBeatenTimer.IsElapsed() )
	{
		m_downAndBeatenTimer.Invalidate();
		GetMusic()->OnDownAndBeatenEnded();
	}
	if ( m_isBleedingOut )
	{
		m_isBleedingOut = false;
		GetMusic()->OnBleedingOutEnded();
	}
	else
	{
		GetMusic()->OnDownEnded();
	}
	const int incapHealth = GetHealth();
	m_isIncapacitated = false;
	m_incapacitatedByAttacker = NULL;
	m_incapacitatedTimestamp.Invalidate();
	m_isGoingToDie = true;
	SetHealth( 1 );
	SetHealthBuffer( (float)MAX( 1, survivor_revive_health.GetInt() ) );
	bool lastlife = false;
	if ( !m_isHangingFromLedge )
	{
		++m_currentReviveCount;
		++m_totalReviveCount;
		if ( m_currentReviveCount >= TerrorGameRules()->GetSurvivorMaxIncapacitatedCount() )
		{
			lastlife = true;
			m_bIsOnThirdStrike = true;
			EmitSound( "Player.Heartbeat" );
			TrackSound( "Player.Heartbeat" );
		}
	}
	IGameEvent *event = gameeventmanager->CreateEvent( selfLedgeRelease ? "player_ledge_release" : "revive_success" );
	if ( event )
	{
		event->SetInt( "userid", selfLedgeRelease ? GetUserID() : owner->GetUserID() );
		if ( !selfLedgeRelease )
		{
			event->SetInt( "subject", GetUserID() );
			event->SetBool( "ledge_hang", m_isHangingFromLedge );
			event->SetBool( "lastlife", lastlife );
		}
		gameeventmanager->FireEvent( event );
	}
	if ( !selfLedgeRelease )
	{
		AI_CriteriaSet criteria;
		criteria.AppendCriteria( "Subject", owner->GetResponseCharacterName() );
		criteria.AppendCriteria( "ReviveType", m_isHangingFromLedge ? "Ledge" : "Incap" );
		BaseClass::SpeakIfAllowed( "RevivedByFriend", criteria );
	}
	owner->SetProgressBarTime( 0.0f );
	StopBeingRevived( false );
	const int total = m_savedHealth + m_savedHealthBuffer;
	if ( total > 1 )
	{
		const int restored = (int)( ( (float)incapHealth / survivor_incap_health.GetFloat() ) * (float)total );
		if ( restored > 1 )
		{
			const int lost = total - restored;
			if ( m_savedHealthBuffer <= lost )
			{
				SetHealthBuffer( 0.0f );
				SetHealth( m_savedHealthBuffer + m_savedHealth - lost );
			}
			else
			{
				SetHealthBuffer( (float)( m_savedHealthBuffer - lost ) );
				SetHealth( m_savedHealth );
			}
			m_isGoingToDie = false;
			RecordDamageTaken( "world", lost );
		}
	}
	if ( m_isHangingFromLedge )
	{
		if ( selfLedgeRelease )
		{
			SetAbsOrigin( m_hangFallPos );
		}
		else
		{
			const Vector position = FindLedgeRevivePosition();
			SetAbsOrigin( position );
			m_noAvoidanceTimer.Start( 2.0f );
		}
		m_isHangingFromLedge = false;
		RemoveHangLighting();
		m_isFallingFromLedge = false;
		m_isReleasingLedge = false;
		SetMoveType( MOVETYPE_WALK );
	}
	else
	{
		m_invulnerabilityTimer.Start( 2.0f );
	}
	RestoreIncapacitatedWeapon();
	CBaseCombatWeapon *primary = Weapon_GetSlot( 0 );
	if ( primary && primary->HasAmmo() )
		Weapon_Switch( primary, 0 );
	else if ( GetActiveWeapon() )
		GetActiveWeapon()->Deploy();
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_REVIVE_SUCCESS );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S318810. Restart only the deadline, using the RAW duration even when
// the old timestamp is nonpositive. The native comparison remains wide.
void CTerrorPlayer::ExpireAdrenaline()
{
	const double deadline = static_cast< double >( gpGlobals->curtime ) +
		static_cast< double >( m_adrenalineTimer.m_duration.Get() );
	const float storedDeadline = static_cast< float >( deadline );
	if ( static_cast< double >( m_adrenalineTimer.m_timestamp.Get() ) != deadline )
		m_adrenalineTimer.m_timestamp.GetForModify() = storedDeadline;
	m_bAdrenalineActive = false;
	GetMusic()->OnAdrenalineExpired();
}

// S31F8A0. The caller separately requires a positive timestamp; this
// routine retains its own ordered deadline test before ending action 5.
void CTerrorPlayer::UpdateDefibrillatorReturnToLife()
{
	if ( static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( m_defibrillatorReturnToLifeTimer.m_timestamp.Get() ) )
	{
		StopUseAction();
		m_defibrillatorReturnToLifeTimer.Invalidate();
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

CSurvivorDeathModel *CTerrorPlayer::GetDeathModel() const
{
	return CSurvivorDeathModel::FindForPlayer( this );
}


void CTerrorPlayer::RemoveDeathModel()
{
	CSurvivorDeathModel::RemoveForPlayer( this );
}


// S:0x31F460. Read the selected body's transform and respawn health AFTER
// Spawn; the native post-restore bookkeeping remains in this owner.
void CTerrorPlayer::RestoreAfterDeath( CTerrorPlayer *rescuer, CSurvivorDeathModel *deathModel )
{
	const bool wasIdle = m_isIdle;
	m_bRespawningFromDefibrillator = true;
	State_Transition( STATE_ACTIVE );
	Spawn();
	m_bRespawningFromDefibrillator = false;
	Teleport( &deathModel->GetAbsOrigin(), &deathModel->GetAbsAngles(), &vec3_origin );
	SetHealth( z_survivor_respawn_health.GetInt() );
	ApplyTempHealthOnlyMutation();
	SpeakIfAllowed( "RevivedByDefibrillator" );
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "Subject", rescuer->GetResponseCharacterName() );
	QueueResponseConceptForEntity( "RevivedByDefibrillatorDelayed", criteria, gpGlobals->curtime + 1.0f, this, rescuer );
	if ( TerrorGameRules()->IsVersusMode() )
	{
		TerrorGameRules()->IncrementVersusDefibsUsed();
		TerrorGameRules()->CacheVersusScores( false );
	}
	UTIL_Remove( deathModel );
	const float duration = defibrillator_return_to_life_time.GetFloat();
	SetUseAction( 5, this, duration, false );
	m_defibrillatorReturnToLifeTimer.Start( duration );
	if ( wasIdle )
		m_idleTimer.Start( 1.0f );
	if ( TerrorGameRules()->IsCoopMode() &&
		( TerrorGameRules()->GetDifficultyLevel() == 0 || TerrorGameRules()->GetDifficultyLevel() == 1 ) )
		m_survivorIntensity.Add( 5 );
	RestoreIncapacitatedWeapon();
	GetMusic()->OnDeathEnded();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:0x3155F0. The bucket is internal notification bookkeeping, not a second
// authoritative HP pool or the wire-quantized value.
void CTerrorPlayer::SetHealthBuffer( float health )
{
	if ( !( health > 0.0f ) )
		health = 0.0f;
	m_healthBuffer = health;
	m_healthBufferTime = gpGlobals->curtime;
	m_lastHealthBuffer = 5 * ( _mm_cvttss_si32( _mm_set_ss( health ) ) / 5 );
}

// S:0x3156C0. Unlike SetHealthBuffer, this writer does not clamp negatives.
void CTerrorPlayer::GiveHealthBuffer( float health, float maximumHealth )
{
	float buffer = GetHealthBuffer() + health;
	if ( buffer + (float)GetHealth() > maximumHealth )
		buffer = maximumHealth - (float)GetHealth();
	m_healthBuffer = buffer;
	m_healthBufferTime = gpGlobals->curtime;
	m_lastHealthBuffer = 5 * ( _mm_cvttss_si32( _mm_set_ss( buffer ) ) / 5 );
}

// S:0x312FB0 resets last-life state even if the base refuses the heal or its
// float amount truncates to no permanent HP.
int CTerrorPlayer::TakeHealth( float health, int damageType )
{
	m_isGoingToDie = false;
	if ( IsSurvivorTeam( this ) && m_currentReviveCount >= TerrorGameRules()->GetSurvivorMaxIncapacitatedCount() )
		StopSound( "Player.Heartbeat" );
	m_currentReviveCount = 0;
	m_bIsOnThirdStrike = false;
	// S:0x16B470 -> 0x03DB30 -> 0x0597E0. The SDK base adds the float
	// before truncation and emits take_health, neither of which is this route.
	if ( m_takedamage != DAMAGE_NO )
		m_bitsDamageType &= ~( damageType & ~g_pGameRules->Damage_GetTimeBased() );
	if ( !edict() || m_takedamage < DAMAGE_YES )
		return 0;
	const int maximumHealth = GetMaxHealth();
	const int oldHealth = GetHealth();
	if ( oldHealth >= maximumHealth )
		return 0;
	const int restored = _mm_cvttss_si32( _mm_set_ss( health ) );
	SetHealth( static_cast< int >( static_cast< unsigned int >( oldHealth ) + static_cast< unsigned int >( restored ) ) );
	if ( GetHealth() > maximumHealth )
		SetHealth( maximumHealth );
	return static_cast< int >( static_cast< unsigned int >( GetHealth() ) - static_cast< unsigned int >( oldHealth ) );
}

float CTerrorPlayer::HealSurvivor( float maximumHealth, float healFraction )
{
	const float deficit = maximumHealth - (float)GetHealth();
	// COMISS 0,deficit / JNB selects zero only for ordered nonpositive values.
	const float restored = deficit <= 0.0f ? 0.0f : healFraction * deficit;
	TakeHealth( restored, 0 );
	SetHealthBuffer( 0.0f );
	return restored;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorPlayer::GiveHealth()
{
	SetHealth( GetMaxHealth() );
	if ( IsAlive() )
	{
		if ( IsIncapacitated() )
		{
			OnRevived();
		}
		TakeHealth( (float)GetMaxHealth(), 0 );
	}
}

void CTerrorPlayer::UseAdrenaline( float duration )
{
	m_adrenalineTimer.Start( duration );
	m_bAdrenalineActive = true;
	GetMusic()->OnAdrenalineUsed();
}

// S:0x258470, also reached by pills in the real TempHealthOnly mutation.
void CTerrorPlayer::SetReviveCount( int count )
{
	const int maximum = TerrorGameRules()->GetSurvivorMaxIncapacitatedCount();
	const bool wasLastLife = IsSurvivorTeam( this ) && m_currentReviveCount >= maximum;
	m_currentReviveCount = count;
	m_bIsOnThirdStrike = count >= maximum;
	if ( m_bIsOnThirdStrike )
	{
		StopSound( "Player.Heartbeat" );
		EmitSound( "Player.Heartbeat" );
	}
	else if ( wasLastLife )
	{
		StopSound( "Player.Heartbeat" );
	}
}

void CTerrorPlayer::InputEnableLedgeHang( inputdata_t &input )
{
	m_ledgeHangingEnabled = true;
}

void CTerrorPlayer::InputDisableLedgeHang( inputdata_t &input )
{
	m_ledgeHangingEnabled = false;
}

// S:0x31CFE0, reached after rescue/defibrillation writes permanent HP.
void CTerrorPlayer::ApplyTempHealthOnlyMutation()
{
	if ( GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules()->IsTempHealthOnly() )
	{
		SetHealthBuffer( (float)GetHealth() );
		SetHealth( 1 );
		m_isGoingToDie = true;
	}
}

bool CTerrorPlayer::BeginDeathAnimation( const CTakeDamageInfo &info )
{
	if ( !( GetFlags() & FL_ONGROUND ) || IsIncapacitated() || IsBeingPounced() || !survivor_death_anims.GetBool() )
		return false;
	EndReviving( true );
	ClearUseEntity();
	SetPushEntity( NULL );
	StopUseAction();
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_DYING );
	m_deathInfo = info;
	m_deathInfo.SetDamageForce( vec3_origin );
	m_deathEvent = new KeyValues( "deathEvent" );
	TerrorGameRules()->PopulateDeathEvent( m_deathEvent, info.GetAttacker(), this, info );
	SpeakIfAllowed( "PlayerDeath", 3 );
	// Original 12460 is the vocalization throttle, NOT a death-completion timer.
	m_vocalizeTimer.Start( 15.0f );
	return true;
}


// S:0x3269D0 clears life state before CCSPlayer::Spawn, but clears the raw
// buffer and applies TempHealthOnly after the native permanent-HP setup.
void CTerrorPlayer::ResetSurvivorHealthBuffer()
{
	SetHealthBuffer( 0.0f );
	m_isGoingToDie = false;
	ApplyTempHealthOnlyMutation();
}
