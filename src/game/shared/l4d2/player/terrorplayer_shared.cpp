//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/c_base_mounted_weapon.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/base_mounted_weapon.h"
#include "engine/IStaticPropMgr.h"
#endif
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "in_buttons.h"
#include "gamemovement.h"
#include "l4d2/player/terrorplayer_shared_console_server_cvars.inc"
#include "tier0/memdbgon.h"
extern IGameMovement *g_pGameMovement;
extern ConVar z_avoidteammates;

ConVar sb_pushscale( "sb_pushscale", "1.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_sidespeed( "z_sidespeed", "450", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_forwardspeed( "z_forwardspeed", "450", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_backspeed( "z_backspeed", "450", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );

// C512260/C512290: client console registration exists even without native readers.
#if defined( CLIENT_DLL )
ConVar versus_shove_hunter_fov( "versus_shove_hunter_fov", "90",
	FCVAR_CLIENTDLL | FCVAR_REPLICATED | FCVAR_CHEAT,
	"Angle that survivor shove will hit hunters" );
ConVar versus_shove_hunter_fov_pouncing( "versus_shove_hunter_fov_pouncing", "45",
	FCVAR_CLIENTDLL | FCVAR_REPLICATED | FCVAR_CHEAT,
	"Angle that survivor shove will hit hunters while pouncing" );
#endif

#pragma float_control( precise, on, push )
// S2C87C0/C26A0B0: zero or STRICT now>deadline, including unordered failure.
bool CTerrorPlayer::IsShoveAvailable() const
{
	return m_flNextShoveTime == 0.0f || gpGlobals->curtime > m_flNextShoveTime;
}

// S2CB6E0/C26CFC0: this is an extension, not unconditional assignment or MAX.
void CTerrorPlayer::ExtendNextShoveTime( float deadline )
{
	if ( deadline > m_flNextShoveTime )
		m_flNextShoveTime = deadline;
}
#pragma float_control( pop )
CTerrorPlayerAnimState *CTerrorPlayer::GetTerrorAnimState() const
{
	return static_cast< CTerrorPlayerAnimState * >( m_PlayerAnimState );
}

ConVar survivor_crouch_speed( "survivor_crouch_speed", "75", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
ConVar survivor_teammatefocus_enabled( "survivor_teammatefocus_enabled", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"Enable or disable focusing on teammates via the use key" );
ConVar survivor_teammatefocus_speed( "survivor_teammatefocus_speed", "10.0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"Scalar speed for locking on a teammate in focus", true, 0.0f, true, 30.0f );
ConVar survivor_teammatefocus_stickyness( "survivor_teammatefocus_stickyness", "5", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"Amount the look controller must move before unlocking a teammate in focus" );
ConVar survivor_limp_health( "survivor_limp_health", "40", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
extern ConVar tongue_victim_max_speed;
extern ConVar z_speed;
extern ConVar z_tank_speed;
extern ConVar z_tank_speed_vs;
extern ConVar z_tank_walk_speed;

ConVar z_ghost_speed( "z_ghost_speed", "450", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_jockey_min_mounted_speed( "z_jockey_min_mounted_speed", "0.6", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar adrenaline_run_speed( "adrenaline_run_speed", "260", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_drag_speed_multiplier( "survivor_drag_speed_multiplier", ".5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
,
	"Multiplier to speed when we are dragging somebody." );
ConVar z_gas_speed( "z_gas_speed", "210", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_exploding_speed( "z_exploding_speed", "175", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_spitter_speed( "z_spitter_speed", "210", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_jockey_speed( "z_jockey_speed", "250.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_fumes_walk_speed( "survivor_fumes_walk_speed", "85", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_limp_walk_speed( "survivor_limp_walk_speed", "85", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_walk_speed( "z_walk_speed", "85", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_crouch_speed( "z_crouch_speed", "75", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_allow_crawling( "survivor_allow_crawling", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
ConVar survivor_crawl_speed( "survivor_crawl_speed", "15", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_incapacitated_roll( "survivor_incapacitated_roll", "20", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_incapacitated_eye_height( "survivor_incapacitated_eye_height", "24", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_pounce_victim_eye_height( "survivor_pounce_victim_eye_height", "12", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_hanging_eye_height( "survivor_hanging_eye_height", "85", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar survivor_hanging_from_tongue_eye_height( "survivor_hanging_from_tongue_eye_height", "40", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar view_offset_down( "view_offset_down", "16", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar view_offset_forward( "view_offset_forward", "8", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar view_offset_up( "view_offset_up", "-16", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar view_offset( "view_offset", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
#ifdef CLIENT_DLL
// C511AD0; server keeps its existing sole use.cpp registration.
ConVar survivor_revive_continuous_fire( "survivor_revive_continuous_fire", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
,
	"Players need to hold +attack to revive" );
#endif

ConVar z_noise_level_hold_time( "z_noise_level_hold_time", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How long we hold a given noise level before it starts to fade." );
ConVar z_noise_level_fade_rate( "z_noise_level_fade_rate", "40", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How much noise we lose each tick, after hold_time expires." );
ConVar z_noise_level_max( "z_noise_level_max", "135", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "The highest the noise level can go." );
ConVar player_use_radius( "player_use_radius", "96", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar player_incap_use_radius( "player_incap_use_radius", "96", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_use_tolerance( "z_use_tolerance", "0.82", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_round_start_attack_prevention( "z_round_start_attack_prevention", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"How many seconds after round start until a player is allowed to use a weapon." );

// S2B66F0/2C8E50 and C2535A0 resolve the one actual ability handle.
CBaseAbility *CTerrorPlayer::GetCustomAbility() const
{
	return m_customAbility.Get();
}

Vector CTerrorPlayer::GetTonguePullPosition( bool absolute ) const
{
	// S2C9680/C269C60 delegates only through the actual tongue owner's ability.
	CTerrorPlayer *pOwner = GetTongueOwner();
	if ( pOwner )
	{
		CTongue *pTongue = dynamic_cast< CTongue * >( pOwner->GetCustomAbility() );
		if ( pTongue )
			return pTongue->GetPullPosition( absolute );
	}
	return vec3_origin;
}

unsigned int CTerrorPlayer::PlayerSolidMask( bool brushOnly ) const
{
	// S2C7710/C269660 is the actor policy, distinct from game-movement masks.
	const bool bInfected = GetTeamNumber() == TEAM_INFECTED;
	const bool bBot = IsBot();
	if ( brushOnly )
		return bInfected ? ( bBot ? 0x0002420Bu : 0x8002420Bu ) : 0x0001420Bu;
	return bInfected ? ( bBot ? 0x0202420Bu : 0x8202420Bu ) : 0x0201420Bu;
}

// S2C80E0/C26A970: movement uses team contents and the active stuck context.
bool CTerrorPlayer::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	CGameMovement *movement = static_cast< CGameMovement * >( g_pGameMovement );
	if ( ( collisionGroup == COLLISION_GROUP_PLAYER_MOVEMENT || collisionGroup == COLLISION_GROUP_PLAYER ) &&
		z_avoidteammates.GetBool() )
	{
		if ( IsGhost() )
			return false;

		const int team = GetTeamNumber();
		if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		{
			if ( !( contentsMask & CONTENTS_TEAM2 ) ||
				( ( GetPounceAttacker() || IsKnockedDown() ) && !( contentsMask & CONTENTS_TEAM1 ) ) )
				return false;
		}
		else if ( team == TEAM_INFECTED )
		{
			if ( !( contentsMask & CONTENTS_TEAM1 ) ||
				( ( GetPounceVictim() || IsKnockedDown() ) && !( contentsMask & CONTENTS_TEAM2 ) ) )
				return false;
		}

		CBasePlayer *movingPlayer = movement->GetMovingPlayer();
		if ( movingPlayer && movingPlayer->IsPlayer() )
		{
			CTerrorPlayer *movingTerror = static_cast< CTerrorPlayer * >( movingPlayer );
			if ( movingTerror->CanBeRevived() ||
				( movingTerror->IsBeingPounced() && GetPounceVictim() == movingTerror ) )
				return false;
		}
	}

	bool ignoreIncapacitated = false;
	if ( CanBeRevived() )
	{
		const int team = GetTeamNumber();
		if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		{
			ignoreIncapacitated = true;
#ifndef CLIENT_DLL
			CBasePlayer *movingPlayer = movement->GetMovingPlayer();
			if ( movingPlayer && movingPlayer->IsPlayer() )
				ignoreIncapacitated = !static_cast< CTerrorPlayer * >( movingPlayer )->IsAttemptingToPounce();
#endif
		}
	}

#ifdef CLIENT_DLL
	// The client tests this actor's raw counter, even outside movement.
	const bool ignorePlayer = ignoreIncapacitated || m_StuckLast != 0;
#else
	// Native IsPlayerStuck observes processing, probe flag, player, then counter.
	CBasePlayer *stuckPlayer = movement->GetMovingPlayerOutsideStuckTest();
	const bool isStuck = stuckPlayer && stuckPlayer->m_StuckLast > 0;
	const bool ignorePlayer = isStuck || ignoreIncapacitated;
#endif
	if ( ignorePlayer && ( contentsMask & ( CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP ) ) )
		return false;

	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

float CTerrorPlayer::GetPlayerMaxSpeed()
{
	if ( IsGhost() )
		return z_ghost_speed.GetFloat();

#ifdef CLIENT_DLL
	if ( GetObserverMode() != OBS_MODE_NONE || m_lifeState != LIFE_ALIVE )
#else
	if ( HasPhysicsFlag( PFLAG_OBSERVER ) || m_lifeState != LIFE_ALIVE )
#endif
		return 900.0f;

	float speed = 1.0f;
	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
	{
		if ( IsTongueVictim() )
			return tongue_victim_max_speed.GetFloat();

		const bool waterSlowsMovement = GetWaterLevel() >= WL_Feet && TerrorGameRules()->IsWaterSlowdownEnabled();
		if ( IsJockeyVictim() )
		{
			if ( waterSlowsMovement )
				return 115.0f;

			const float minimum = z_jockey_min_mounted_speed.GetFloat();
			const float health = static_cast< float >( GetHealth() );
			const float ratio = health / static_cast< float >( GetMaxHealth() );
			if ( minimum > ratio )
				return static_cast< double >( z_jockey_min_mounted_speed.GetFloat() ) * 220.0f;

			// Native repeats both health and max-health queries on this arm.
			const float currentHealth = static_cast< float >( GetHealth() );
			const float currentRatio = currentHealth / static_cast< float >( GetMaxHealth() );
			return static_cast< double >( currentRatio ) * 220.0f;
		}

		const float limpHealth = survivor_limp_health.GetFloat();
#ifdef CLIENT_DLL
		const float health = static_cast< float >( GetHealth() );
#else
		const int health = GetHealth();
#endif
		// GetHealthBuffer may deliver a real server player_hurt callback. Both
		// health and the cvar are sampled BEFORE that query. Ordered > is
		// intentional: an unordered x87 comparison does not select limping.
		const bool limping = static_cast< double >( limpHealth ) >
			static_cast< double >( GetHealthBuffer() ) + static_cast< double >( health );

		if ( m_bAdrenalineActive )
		{
			speed = adrenaline_run_speed.GetFloat();
		}
		else if ( IsGoingToDie() )
		{
			speed = 85.0f;
		}
		else if ( waterSlowsMovement )
		{
			// The original tests the descriptor-backed mode again here, not
			// while initially sampling the water-enable bit.
			const bool playerControlledZombies = TerrorGameRules()->IsPlayerControlledZombies();
			speed = playerControlledZombies ? 170.0f : 115.0f;
			if ( limping && speed > 150.0f )
				speed = 150.0f;
		}
		else if ( limping )
		{
			speed = 150.0f;
		}
		else
		{
			speed = 220.0f;
		}

#ifdef CLIENT_DLL
		// The client rounds its FLOAT FOV query before obtaining default FOV.
		const float fov = GetFOV();
		const int defaultFOV = GetDefaultFOV();
		if ( static_cast< double >( defaultFOV ) > static_cast< double >( fov ) && !( 85.0f > speed ) )
#else
		// Server FOV has an INTEGER return contract, unlike the client.
		const int fov = GetFOV();
		const int defaultFOV = GetDefaultFOV();
		if ( fov < defaultFOV && !( 85.0f > speed ) )
#endif
			speed = 85.0f;

#ifndef CLIENT_DLL
		// Server only; the actual serial-validated DRAG TARGET is not use,
		// revive, or a camera handle. Native MULSS rounds the product to float.
		if ( m_dragTarget.Get() )
			return survivor_drag_speed_multiplier.GetFloat() * speed;
#endif
		return speed;
	}

	// Preserve the second team query, including its placement after the
	// survivor branch. Unassigned/spectator/life-alive actors retain1.
	if ( GetTeamNumber() != TEAM_INFECTED )
		return speed;

	switch ( GetZombieClass() )
	{
	case 1: // Smoker, original zombie-class domain.
		{
			CBaseAbility *ability = GetCustomAbility();
			if ( ( ability && static_cast< CTongue * >( ability )->IsTongueExtended() ) || GetStamina() > 2500.0f )
				return 1.0f;
			return z_gas_speed.GetFloat();
		}
	case 2: // Boomer.
		// Native samples the cvar before testing stamina. > preserves the
		// original COMISS/JBE behavior for NaN stamina (retain cvar speed).
		speed = z_exploding_speed.GetFloat();
		if ( GetStamina() > 2500.0f )
			return 1.0f;
		return speed;
	case 4: // Spitter.
		return z_spitter_speed.GetFloat();
	case 5: // Jockey.
		return z_jockey_speed.GetFloat();
	case 6: // Charger.
		if ( m_pummelVictim.Get() )
			return 1.0f;
		return z_speed.GetFloat();
	case 8: // Tank.
		// Do not cache either the actual ability handle or its callback:
		// native has up to THREE resolves and TWO movement-speed callbacks.
		// An unordered result rejects the override; the run return is rounded
		// to float. This is GetMovementSpeed, NOT GetMaxSpeed.
		if ( GetCustomAbility() && GetCustomAbility()->GetMovementSpeed() >= 0.0f )
		{
			const float abilitySpeed = GetCustomAbility()->GetMovementSpeed();
			return abilitySpeed;
		}
		if ( TerrorGameRules()->IsPlayerControlledZombies() )
			return z_tank_speed_vs.GetFloat();
		return z_tank_speed.GetFloat();
	default:
		// Hunter3 and Witch7 intentionally share this default; no additional
		// hunter cvar, ability gate, or weapon max-speed lookup is present.
		return z_speed.GetFloat();
	}
}

// S:2C9D90 / C:269DA0. Native CLIENT primary slot342; server direct helper.
float CTerrorPlayer::GetWalkTopSpeed() const
{
	if ( m_lifeState != LIFE_ALIVE )
	{
#ifdef CLIENT_DLL
		return const_cast< CTerrorPlayer * >( this )->GetPlayerMaxSpeed();
#else
		return const_cast< CTerrorPlayer * >( this )->CTerrorPlayer::GetPlayerMaxSpeed();
#endif
	}

	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
	{
		if ( IsTongueVictim() )
		{
#ifdef CLIENT_DLL
			return const_cast< CTerrorPlayer * >( this )->GetPlayerMaxSpeed();
#else
			return const_cast< CTerrorPlayer * >( this )->CTerrorPlayer::GetPlayerMaxSpeed();
#endif
		}
#ifndef CLIENT_DLL
		// The original client has NO corresponding stagger query.
		if ( IsStaggering() )
			return const_cast< CTerrorPlayer * >( this )->CTerrorPlayer::GetPlayerMaxSpeed();
#endif
		if ( !m_bAdrenalineActive )
		{
			if ( IsGoingToDie() )
				return survivor_fumes_walk_speed.GetFloat();
			// This is the raw live flag, not another IsGoingToDie query.
			if ( m_isGoingToDie )
				return survivor_limp_walk_speed.GetFloat();
		}
		return 85.0f;
	}

	// Native does NOT require infected team for this class dispatch.
	const int zombieClass = GetZombieClass();
	if ( zombieClass == 3 ) // Hunter.
		return 85.0f;
	if ( zombieClass != 8 ) // Tank.
		return z_walk_speed.GetFloat();
	if ( GetCustomAbility() && GetCustomAbility()->GetMovementSpeed() >= 0.0f )
		return GetCustomAbility()->GetMovementSpeed();
	return z_tank_walk_speed.GetFloat();
}

// S:2C9E80 / C:269E90. Native CLIENT primary slot343; server direct helper.
float CTerrorPlayer::GetCrouchTopSpeed() const
{
	if ( m_lifeState != LIFE_ALIVE )
	{
#ifdef CLIENT_DLL
		return const_cast< CTerrorPlayer * >( this )->GetPlayerMaxSpeed();
#else
		return const_cast< CTerrorPlayer * >( this )->CTerrorPlayer::GetPlayerMaxSpeed();
#endif
	}
	if ( GetTeamNumber() == TEAM_INFECTED )
		return z_crouch_speed.GetFloat();
#ifndef CLIENT_DLL
	// This branch is absent from the original client, intentionally.
	if ( IsStaggering() )
		return 220.0f;
#endif
	return survivor_crouch_speed.GetFloat();
}

// S2C9760/C269D20: a valid tongue owner suppresses actor ground friction.
float CTerrorPlayer::GetFriction() const
{
	return m_tongueOwner.Get() ? 0.0f : BaseClass::GetFriction();
}

// S2C8650/C2694C0. Native JBE accepts unordered deadline/time comparisons.
bool CTerrorPlayer::CanJump() const
{
	const int team = GetTeamNumber();
	return ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) ||
		!( m_jumpSupressedUntil > gpGlobals->curtime );
}

// S2C97A0/C269D60. Actual case-insensitive live classname predicate precedes
// IsActive. Client SDK FClassnameIs/strcmp and dynamic_cast differ from native.
bool CTerrorPlayer::IsLunging() const
{
	CBaseAbility *ability = GetCustomAbility();
	if ( !ability )
		return false;
	const char *className = ability->GetClassname();
	const char *const lungeClass = "ability_lunge";
#ifdef CLIENT_DLL
	if ( Q_stricmp( className, lungeClass ) != 0 )
		return false;
#else
	if ( !className )
		className = "";
	if ( className != lungeClass && Q_stricmp( className, lungeClass ) != 0 )
		return false;
#endif
	return ability->IsActive();
}

#ifndef CLIENT_DLL
bool CTerrorPlayer::IsPouncing() const
{
	return m_pounceVictim.Get() != NULL;
}

bool CTerrorPlayer::IsJockeyRiding() const
{
	return m_jockeyVictim.Get() != NULL;
}
#endif

CTerrorPlayer *CTerrorPlayer::GetPounceVictim() const
{
	return static_cast< CTerrorPlayer * >( m_pounceVictim.Get() );
}

CTerrorPlayer *CTerrorPlayer::GetJockeyVictim() const
{
	return static_cast< CTerrorPlayer * >( m_jockeyVictim.Get() );
}

CTerrorPlayer *CTerrorPlayer::GetJockeyAttacker() const
{
	return static_cast< CTerrorPlayer * >( m_jockeyAttacker.Get() );
}


// S314B20/C278B40. No impulse/application: genuine whole-vector queue setter.
void CTerrorPlayer::SetShoveForce( const Vector &force )
{
#ifndef CLIENT_DLL
	m_shoveForce = force;
#else
	if ( m_shoveForce != force )
		m_shoveForce = force;
#endif
}


// S24B890/C22DCB0. Genuine CCS virtual default reached by Terror IsBusy.
bool CCSPlayer::IsBusy() const
{
	if ( GetMoveType() == MOVETYPE_NONE )
		return true;
	if ( IsObserver() )
		return false;
#ifdef CLIENT_DLL
	return State_Get() != STATE_ACTIVE && State_Get() != STATE_OBSERVER_MODE &&
		State_Get() != STATE_GHOST;
#else
	const CSPlayerState state = State_Get();
	return state != STATE_ACTIVE && state != STATE_OBSERVER_MODE && state != STATE_GHOST;
#endif
}

// S2C9380/C26A510. This query is NOT m_airMovementRestricted, a CanMove stub,
// animstate busy, or a cached whole-frame movement snapshot.
bool CTerrorPlayer::IsBusy() const
{
	if ( IsIncapacitated() ||
		LookupTerrorOriginalActivity( GetTerrorActivity() ) == TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND ||
		LookupTerrorOriginalActivity( GetTerrorActivity() ) == TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING )
		return true;
	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
	{
		if ( IsBeingPounced() || IsPummelVictim() || IsTongueVictim() || m_reviveTarget.Get() || m_iCurrentUseAction != 0 )
			return true;
		switch ( LookupTerrorOriginalActivity( GetTerrorActivity() ) )
		{
		case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_TANKPUNCH:
		case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TANKPUNCH:
		case TERROR_ORIGINAL_ACT_TERROR_TANKPUNCH_LAND:
		case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_CHARGER:
		case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_CHARGERHIT:
		case TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW:
		case TERROR_ORIGINAL_ACT_TERROR_SLAMMED_WALL:
		case TERROR_ORIGINAL_ACT_TERROR_SLAMMED_GROUND:
		case TERROR_ORIGINAL_ACT_TERROR_POUNCED_TO_STAND:
			return true;
		default:
			break;
		}
		float duration = 0.75f;
		if ( m_knockdownReason == 2 ) duration = 1.5f;
		else if ( m_knockdownReason == 0 ) duration = 0.5f;
		if ( m_knockdownTimer.GetStartTime() > 0.0f && m_knockdownTimer.GetElapsedTime() < duration )
			return true;
	}
	if ( m_pummelVictim.Get() )
		return true;
	CBaseAbility *ability = GetCustomAbility();
	if ( ability && !ability->CanJump() )
		return true;
	CBaseCombatWeapon *weapon = GetActiveWeapon();
	if ( weapon && !static_cast< CTerrorWeapon * >( weapon )->CanPerformUseAction() )
		return true;
	if ( m_usingMountedWeapon || m_positionEntity.Get() )
		return true;
#ifdef CLIENT_DLL
	// Native C26A6A5 first checks actual local-player identity; remote stamina
	// is not made authoritative for this client predicate.
	if ( IsPlayer() && IsLocalPlayer() && GetStamina() > 2000.0f )
		return true;
#else
	if ( GetStamina() > 2000.0f )
		return true;
#endif
	return BaseClass::IsBusy();
}

// S241CD0/C223740. COMISS(0,duration)/JB also admits unordered duration.
bool CCSPlayer::IsProgressBarActive() const
{
	return !( m_flProgressBarDuration <= 0.0f );
}

// S2C8580/C269EE0. Reuse the sole inherited CS live progress fields.
bool CTerrorPlayer::IsProgressBarActive() const
{
	if ( m_flProgressBarDuration <= 0.0f )
		return false;
	if ( GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
		m_zombieClass == 3 )
#else
		GetZombieClass() == 3 )
#endif
		return true;
	return !( gpGlobals->curtime > m_flProgressBarStartTime + m_flProgressBarDuration );
}

// S2C85E0/C269F30. Preserve the widened native result and unordered JBE.
// CheckParameters compares this value BEFORE any explicit float spill.
double CTerrorPlayer::GetProgressBarProgress() const
{
	if ( !IsProgressBarActive() )
		return 0.0;
#ifdef CLIENT_DLL
	const float duration = m_flProgressBarDuration;
	const float start = m_flProgressBarStartTime;
#else
	const float start = m_flProgressBarStartTime;
	const float duration = m_flProgressBarDuration;
#endif
	const float now = GetSimulationTime();
#ifdef CLIENT_DLL
	const float end = duration + start;
#else
	const float end = start + duration;
#endif
	if ( !( now > end ) )
		return ( (double)now - (double)start ) / (double)duration;
	return 1.0;
}

float CTerrorPlayer::GetIncapCrawlSpeed() const
{
	return survivor_crawl_speed.GetFloat(); // S2C75C0/C269ED0.
}

// Native S2C8460/C26B160, plain nonvirtual paired query.
bool CTerrorPlayer::IsCrawling() const
{
	return IsIncapacitated() && !m_isHangingFromLedge && !m_isHangingFromTongue &&
		( m_nButtons & IN_FORWARD ) && survivor_allow_crawling.GetBool();
}

// S2C8680/C269FA0. It intentionally resolves the push handle AGAIN.
float CTerrorPlayer::GetPushDistance() const
{
	CBaseEntity *entity = m_pushEntity.Get();
	if ( !entity )
		return 0.0f;
	const Vector origin = GetAbsOrigin();
	Vector nearest;
	entity->CollisionProp()->CalcNearestPoint( origin, &nearest );
	const Vector delta = origin - nearest;
	return (float)sqrt( (double)( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x ) );
}

// S2C8B00/C26A230. Preserve the existing view's XY and native progress blend.
Vector CTerrorPlayer::GetMovementViewOffset( bool duck ) const
{
	if ( m_usingMountedWeapon )
		return GetViewOffset();
	if ( duck && !IsIncapacitated() )
	{
		Vector result = GetViewOffset();
		result.z = g_pGameRules->GetViewVectors()->m_vDuckView.z;
		return result;
	}
	Vector result = GetViewOffset();
	if ( IsIncapacitated() && !m_isHangingFromLedge && !m_isHangingFromTongue )
	{
		const float height = survivor_incapacitated_eye_height.GetFloat();
		const float crouchedHeight = g_pGameRules->GetViewVectors()->m_vDuckView.z;
		const float progress = GetProgressBarProgress();
		result.z = ( 1.0f - progress ) * height + progress * crouchedHeight;
	}
	else
		result.z = g_pGameRules->GetViewVectors()->m_vView.z;
	return result;
}

namespace
{
// Native CTraceFilterNoNPCs -> CTraceFilterViewOffset, S2C77A0/C26A120.
// No actor/self pass entity: the class filters are the original exclusions.
class CTraceFilterViewOffset : public CTraceFilterSimple
{
public:
	CTraceFilterViewOffset() : CTraceFilterSimple( NULL, COLLISION_GROUP_NONE ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
	{
		if ( !CTraceFilterSimple::ShouldHitEntity( handle, contentsMask ) )
			return false;
#ifndef CLIENT_DLL
		if ( staticpropmgr->IsStaticProp( handle ) || !handle )
			return false;
#endif
		CBaseEntity *entity = EntityFromEntityHandle( handle );
		if ( !entity )
			return false;
#ifndef CLIENT_DLL
		if ( entity->Classify() == CLASS_PLAYER_ALLY )
			return false;
#endif
		if ( entity->MyCombatCharacterPointer() || entity->IsNPC() || entity->IsPlayer() )
			return false;
#ifdef CLIENT_DLL
		// Native C26A120 resolves again AFTER the no-NPC base C1AD080.
		entity = EntityFromEntityHandle( handle );
		return !entity || entity->MyCombatWeaponPointer() == NULL;
#else
		// Native S2C77A0 repeats the static-prop/null tests after S20D870.
		return staticpropmgr->IsStaticProp( handle ) || !handle || entity->MyCombatWeaponPointer() == NULL;
#endif
	}
};

// S2C77F0/C26A160. Positive/negative pitch use separate cvars; no clamp.
float GetViewOffsetDistance( const QAngle &angles, bool pounced )
{
	float pitch = angles.x;
	if ( pitch > 180.0f ) pitch -= 360.0f;
	const float down = view_offset_down.GetFloat();
	const float forward = view_offset_forward.GetFloat();
	float up = view_offset_up.GetFloat();
	if ( pounced ) up = -32.0f;
	if ( pitch > 0.0f )
		return ( 1.0f - pitch * 0.011111111f ) * forward +
			( pitch * 0.011111111f ) * down;
	if ( pitch < 0.0f )
		return ( 1.0f - pitch * -0.011111111f ) * forward + ( pitch * -0.011111111f ) * up;
	return forward;
}
}

// S2C8890/C26B710. This is the DIRECT view-offset trace helper, not a ledge
// acquisition test. Ordinary UTIL_TraceHull performs the original yellow debug
// line after TraceRay when r_visualizetraces is enabled.
Vector CTerrorPlayer::TraceMovementViewOffset( const Vector &origin, const QAngle &angles, float height, bool pounced ) const
{
	Vector result( 0.0f, 0.0f, height );
	if ( view_offset.GetBool() )
	{
		QAngle horizontal = angles;
		horizontal.x = 0.0f;
		Vector direction;
		AngleVectors( horizontal, &direction );
		VectorNormalize( direction );
		const float distance = GetViewOffsetDistance( angles, pounced );
		direction.x *= distance;
		direction.y *= distance;
		direction.z = 0.0f;
		CTraceFilterViewOffset filter;
		trace_t trace;
		const Vector start = origin + result;
		UTIL_TraceHull( start, start + direction, Vector( -16.0f, -16.0f, -6.0f ),
			Vector( 16.0f, 16.0f, 6.0f ), 0x020060CBu, &filter, &trace );
		result = trace.endpos - origin;
	}
	return result;
}

// S2CA190/C26B980, called after actual PlayerMove and modifier recovery.
Vector CTerrorPlayer::ComputeMovementViewOffset( const Vector &origin, const QAngle &angles )
{
	Vector result = GetViewOffset();
	result.x = 0.0f;
	result.y = 0.0f;
	CBaseMountedWeapon *mounted = GetMountedWeapon();
	if ( mounted )
	{
		float pitch = angles.x;
		if ( pitch > 180.0f ) pitch -= 360.0f;
		float height = 54.0f;
		if ( pitch > 0.0f )
			height = ( 1.0f - pitch * 0.011111111f ) * 54.0f + ( pitch * 0.011111111f ) * 62.0f;
		const Vector eyes = mounted->EyePosition();
		const QAngle mountedAngles = mounted->GetAbsAngles();
		VectorRotate( Vector( 0.0f, 0.0f, height ), mountedAngles, result );
		result.x = ( eyes.x - origin.x ) + result.x;
		result.y = ( eyes.y - origin.y ) + result.y;
		return result;
	}

#ifndef CLIENT_DLL
	if ( IsAlive() && m_isHangingFromLedge )
		result.z = survivor_hanging_eye_height.GetFloat();
	else if ( IsAlive() && m_isHangingFromTongue )
		result.z = survivor_hanging_from_tongue_eye_height.GetFloat();
	else if ( IsIncapacitated() )
		result.z = GetMovementViewOffset( false ).z;
	else if ( IsBeingPounced() )
		result.z = survivor_pounce_victim_eye_height.GetFloat();
	else if ( !IsJockeyVictim() )
	{
		// The SDK does not replicate the command-owned EFL_NOCLIP_ACTIVE bit.
		// Use the movement state shared with client prediction instead.
		if ( IsAlive() && GetMoveType() != MOVETYPE_NOCLIP )
		{
			if ( !m_Local.m_bDucking )
			{
				if ( !( GetFlags() & FL_DUCKING ) && !m_Local.m_bDucked )
					result.z = g_pGameRules->GetViewVectors()->m_vView.z;
				else if ( m_Local.m_bDucked )
					result.z = g_pGameRules->GetViewVectors()->m_vDuckView.z;
			}
		}
		else if ( IsAlive() && g_pGameRules->GetViewVectors()->m_vView.z > result.z )
			result.z = g_pGameRules->GetViewVectors()->m_vView.z;
	}
	if ( IsAlive() )
		return TraceMovementViewOffset( origin, angles, result.z, IsBeingPounced() );
#else
	const bool alive = m_lifeState == LIFE_ALIVE;
	if ( alive && m_isHangingFromLedge )
		result.z = survivor_hanging_eye_height.GetFloat();
	else if ( alive && m_isHangingFromTongue )
		result.z = survivor_hanging_from_tongue_eye_height.GetFloat();
	else if ( alive && m_isIncapacitated )
		result.z = GetMovementViewOffset( false ).z;
	else if ( IsBeingPounced() )
		result.z = survivor_pounce_victim_eye_height.GetFloat();
	else if ( IsJockeyVictim() )
	{
		if ( gpGlobals->curtime > m_jockeyAttachmentViewTime )
			result.z = survivor_pounce_victim_eye_height.GetFloat();
	}
	else if ( alive )
	{
		if ( GetMoveType() == MOVETYPE_NOCLIP )
		{
			if ( g_pGameRules->GetViewVectors()->m_vView.z > result.z )
				result.z = g_pGameRules->GetViewVectors()->m_vView.z;
		}
		else if ( !IsDucking() )
		{
			if ( !( GetFlags() & FL_DUCKING ) && !IsDucked() )
				result.z = g_pGameRules->GetViewVectors()->m_vView.z;
			else if ( IsDucked() )
				result.z = g_pGameRules->GetViewVectors()->m_vDuckView.z;
		}
	}
	if ( m_lifeState == LIFE_ALIVE )
	{
		// C26BC21..C26BC3D resolves the pounce handle again, after height.
		const bool pounced = IsBeingPounced();
		return TraceMovementViewOffset( origin, angles, result.z, pounced );
	}
#endif
	if ( GetObserverMode() == OBS_MODE_ROAMING )
		return g_pGameRules->GetViewVectors()->m_vDeadViewHeight;
	return result;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool CTerrorPlayer::IsAutoCrouchEnabled() const
{
	return m_isAutoCrouchEnabled || ( GetFlags() & FL_FAKECLIENT ) != 0;
}

bool CTerrorPlayer::IsAutoCrouching() const
{
	const float flStartTime = m_autoCrouchTimer.GetStartTime();
	return flStartTime > 0.0f && gpGlobals->curtime - flStartTime < 0.75f;
}

void CTerrorPlayer::StartAutoCrouch()
{
	m_autoCrouchTimer.Start();
}

// S:0x2C76C0. The compile-time labels are original IDs, never SDK activities.
bool CTerrorPlayer::IsClimbingActivity( Activity activity )
{
	switch ( LookupTerrorOriginalActivity( activity ) )
	{
	case TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB:
	case TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB_TO_CROUCH:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_36_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_38_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_50_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_70_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_115_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_130_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_150_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_166_FROM_STAND:
		return true;
	default:
		return false;
	}
}

// S:0x2C75D0 / C:0x269520 share these exact forty main-activity cases.
bool CTerrorPlayer::IsAttackBlockingActivity( Activity activity )
{
	switch ( LookupTerrorOriginalActivity( activity ) )
	{
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_CHAINSAW:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_CHAINSAW:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_CHAINSAW:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_CHAINSAW:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_BAT:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_BAT:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_BAT:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_BAT:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_MELEE:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_MELEE:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_MELEE:
	case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_MELEE:
	case TERROR_ORIGINAL_ACT_TERROR_TUGGED_FORWARD:
	case TERROR_ORIGINAL_ACT_TERROR_TUGGED_BACKWARD:
	case TERROR_ORIGINAL_ACT_TERROR_TUGGED_LEFTWARD:
	case TERROR_ORIGINAL_ACT_TERROR_TUGGED_RIGHTWARD:
	case TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING:
	case TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB:
	case TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB_TO_CROUCH:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_36_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_38_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_50_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_70_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_115_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_130_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_150_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_CLIMB_166_FROM_STAND:
	case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_L:
	case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_R:
	case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_BACKWARD:
	case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_FORWARD:
	case TERROR_ORIGINAL_ACT_TERROR_HULK_VICTORY:
	case TERROR_ORIGINAL_ACT_TERROR_HULK_VICTORY_B:
	case TERROR_ORIGINAL_ACT_TERROR_RAGE_AT_ENEMY:
	case TERROR_ORIGINAL_ACT_TERROR_RAGE_AT_KNOCKDOWN:
		return true;
	default:
		return false;
	}
}

bool CTerrorPlayer::IsAbleToDualWield() const
{
	return m_dragTarget.Get() == NULL;
}

#pragma float_control( precise, on, push )
bool CTerrorPlayer::IsZoomed()
{
#ifdef CLIENT_DLL
	const float fov = static_cast< float >( GetFOV() );
	const int defaultFOV = GetDefaultFOV();
	return static_cast< double >( fov ) != static_cast< double >( defaultFOV );
#else
	const int fov = GetFOV();
	return fov != GetDefaultFOV();
#endif
}
#pragma float_control( pop )
