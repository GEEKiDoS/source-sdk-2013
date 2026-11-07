//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/player/terror_lagcompensation.h"
#include "ilagcompensationmanager.h"
#include "ammodef.h"
#include "igameevents.h"
#include "l4d2/stats/player_reporter.h"
#include "in_buttons.h"
#include "NextBot/NextBot.h"
#include "l4d2/infected/common/infected.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "NextBot/NextBotBodyInterface.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "debugoverlay_shared.h"
#include "ipredictionsystem.h"
#include <math.h>
#include "tier0/memdbgon.h"

static ConVar z_stomp_always( "z_stomp_always", "0", 0x00004004,
	"Force melee attacks on single infected to always be stomps." );

void FireTerrorBullets( CTerrorPlayer *pPlayer, const Vector &vecOrigin, const QAngle &angles, int nWeaponID, int nMode, int nSeed, float flSoundTime )
{
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( nWeaponID );
	if ( !pPlayer || !pInfo )
		return;

	// S:0x3C78B0 emits event40/41 once for each actual bullet invocation,
	// before its temporary effect and firing notification. Empty fire never
	// reaches this wrapper; dormant players do not start the gesture.
	if ( !pPlayer->IsDormant() )
		pPlayer->DoTerrorAnimationEvent( nMode ? TERROR_ANIMEVENT_FIRE_PRIMARY_ALT : TERROR_ANIMEVENT_FIRE_PRIMARY );

	TE_Bullets( pPlayer->entindex(), vecOrigin, angles, nWeaponID, nMode, nSeed, flSoundTime );
	pPlayer->NoteTerrorWeaponFired();
	IncrementTerrorBulletSerial();
	// Original S:0x3C78B0 supplies mode2 and an empty ray. This mode gates
	// animation rewind after transform selection; do not invent a shot ray.
	StartTerrorLagCompensation( pPlayer, TERROR_LAG_COMPENSATE_HITBOXES_ALONG_RAY, vec3_origin, vec3_angle, 0.0f );
	pPlayer->FireBullet( vecOrigin, angles, *pInfo, nSeed );
	lagcompensation->FinishLagCompensation( pPlayer );
}

TerrorPlayerAnimEvent CTerrorPlayer::SelectTerrorShoveAnimation( CTerrorWeapon *pWeapon )
{
	TargetScan<CBaseCombatCharacter> scan( this, TEAM_INFECTED, 2.0f, 150.0f, 50.0f, false );
	ForEachActor( scan );
	const int nTargets = scan.GetAllTargets().Count();
	pWeapon->m_nShoveTargetCount = nTargets;
	if ( nTargets > 2 )
		return TERROR_ANIMEVENT_MELEE_SWEEP;
	if ( nTargets <= 0 )
	{
		Vector start, forward;
		EyePositionAndVectors( &start, &forward, NULL, NULL );
		trace_t trace;
		UTIL_TraceLine( start, start + forward * 150.0f, 0x0200400B, this, COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction > 0.0f && trace.fraction < 1.0f && trace.m_pEnt &&
			( trace.m_pEnt->ClassMatches( "func_breakable" ) || trace.m_pEnt->ClassMatches( "func_breakable_surf" ) ) )
			return TERROR_ANIMEVENT_MELEE_STRAIGHT;
	}
	else if ( nTargets == 1 )
	{
		Infected *pInfected = dynamic_cast<Infected *>( scan.GetTarget() );
		if ( pInfected )
		{
			IBody *pBody = pInfected->GetBodyInterface();
			if ( !pBody->IsArousal( IBody::INTENSE ) || z_stomp_always.GetBool() )
			{
				if ( ( pBody->GetActualPosture() == IBody::LIE &&
					pInfected->GetAbsOrigin().z < GetAbsOrigin().z + 36.0f ) || z_stomp_always.GetBool() )
				{
					const bool bDucking = m_Local.m_bDucked || m_Local.m_bDucking;
					const float flSpeedLimit = GetWalkTopSpeed() * 1.1f;
					const bool bTooFast = GetAbsVelocity().LengthSqr() > flSpeedLimit * flSpeedLimit;
					if ( GetGroundEntity() && !bTooFast && !bDucking )
					{
						pWeapon->m_bStompTarget = true;
						pWeapon->m_hStompTarget = pInfected;
						return TERROR_ANIMEVENT_MELEE_STOMP;
					}
				}
				return TERROR_ANIMEVENT_MELEE_STRAIGHT;
			}
		}
	}
	return TERROR_ANIMEVENT_MELEE_SHOVE;
}

void CTerrorPlayer::FireBullet( const Vector &vecOrigin, const QAngle &angles, const CTerrorWeaponInfo &info, int nSeed )
{
	TerrorTraceBullet( this, vecOrigin, angles, info, nSeed, false );
}



// Native literal16384 is FCVAR_CHEAT, not FCVAR_REPLICATED;24576 contains both.
static ConVar survivor_hitsound_interval_timer( "survivor_hitsound_interval_timer", "0.1", 0x00004004 );
ConVar z_gun_damage( "z_gun_damage", "10", 0x00006004 );
static ConVar z_gun_physics_force( "z_gun_physics_force", "25", 0x00004004, "How hard we push things that aren't players or doors." );
static ConVar z_gun_survivor_friend_push( "z_gun_survivor_friend_push", "1", 0x00006004, "Allow survivors to push survivors with a right click." );
static ConVar z_gun_survivor_shove_dot( "z_gun_survivor_shove_dot", "0.95", 0x00004004, "Max dot of a shove swing that can push a fellow survivor" );
static ConVar survivor_team_hit_pitch_min( "survivor_team_hit_pitch_min", "-10", 0x00004004 );
static ConVar survivor_team_hit_pitch_max( "survivor_team_hit_pitch_max", "10", 0x00004004 );
static ConVar survivor_team_hit_yaw_min( "survivor_team_hit_yaw_min", "-10", 0x00004004 );
static ConVar survivor_team_hit_yaw_max( "survivor_team_hit_yaw_max", "10", 0x00004004 );
static ConVar z_gun_force( "z_gun_force", "300", 0x00004004 );
static ConVar z_gun_stun_duration( "z_gun_stun_duration", "1.25", 0x00006004 );
static ConVar z_gun_survivor_force( "z_gun_survivor_force", "200", 0x00004004 );
static ConVar z_zombie_knockoff_death( "z_zombie_knockoff_death", "0", 0x00006004, "Knocking off a pouncing zombie instantly kills them." );
static ConVar survivor_shove_teammates( "survivor_shove_teammates", "0", 0x00004004 );

extern ConVar z_show_swings;

// Actual existing globals in terrorplayer_hunter_jockey.cpp. Do NOT duplicate.
extern ConVar versus_shove_hunter_fov;
extern ConVar versus_shove_hunter_fov_pouncing;

// z_gun_damage is intentionally NOT static: existing survivor_behavior.cpp
// and survivor_locomotion.cpp already have extern ConVar z_gun_damage.

class CTerrorShoveTraceFilter : public CTraceFilterSimple
{
public:
	CTerrorShoveTraceFilter( CTerrorPlayer *pPlayer, const CUtlVectorFixed< EHANDLE, 128 > &affected )
		: CTraceFilterSimple( pPlayer, COLLISION_GROUP_NONE ), m_Affected( affected )
	{
	}
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int nContentsMask ) OVERRIDE
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity && pEntity->MyCombatCharacterPointer() )
		{
			FOR_EACH_VEC( m_Affected, i )
			{
				if ( m_Affected[i].Get() == pEntity )
					return false;
			}
		}
		if ( !CTraceFilterSimple::ShouldHitEntity( pHandleEntity, nContentsMask ) )
			return false;
		return !pEntity || !pEntity->IsPlayer() || !static_cast< CTerrorPlayer * >( pEntity )->IsGhost();
	}
private:
	const CUtlVectorFixed< EHANDLE, 128 > &m_Affected;
};

#pragma float_control( precise, on, push )
bool CTerrorPlayer::TraceTerrorShove( CTerrorWeapon *pWeapon, const Vector &direction )
{
	if ( pWeapon->m_bStompTarget )
		return false;
	CTerrorShoveTraceFilter filter( this, pWeapon->m_swingAffectedEntities );
	const Vector vecStart = Weapon_ShootPosition();
	const Vector vecEnd = Weapon_ShootPosition() + direction * pWeapon->m_flSwingRange;
	trace_t trace;
	UTIL_TraceHull( vecStart, vecEnd, Vector( -3.0f, -3.0f, -3.0f ), Vector( 3.0f, 3.0f, 3.0f ),
		0x4600400B, &filter, &trace );
	const int showSwings = z_show_swings.GetInt();
	if ( showSwings != 0 )
	{
		if ( showSwings > 1 )
			NDebugOverlay::SweptBox( vecStart, vecEnd, Vector( -3.0f, -3.0f, -3.0f ), Vector( 3.0f, 3.0f, 3.0f ),
				vec3_angle, 255, 255, 0, 255, 3.0f );
		else
			NDebugOverlay::Line( vecStart, vecEnd, 255, 255, 0, true, 1.0f );
	}
	CBaseEntity *pTarget = trace.m_pEnt;
	if ( !trace.DidHit() || !pTarget )
		return false;
	CDisablePredictionFiltering disablePredictionFiltering;
	CTerrorPlayer *pVictim = pTarget->IsPlayer() ? static_cast< CTerrorPlayer * >( pTarget ) : NULL;
	if ( pVictim && ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		( pTarget->GetTeamNumber() == TEAM_SURVIVOR || pTarget->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		bool reject = pWeapon->m_nShoveTargetCount > 0 && !pVictim->IsTongueVictim() &&
			!pVictim->IsBeingPounced() && !pVictim->IsPummelVictim() && !pVictim->IsJockeyVictim();
		if ( !reject && !pTarget->GetGroundEntity() && !pVictim->IsTongueVictim() )
			reject = true;
		if ( !reject && pVictim->CanBeRevived() )
			reject = true;
		if ( !reject )
		{
			Vector toVictim = pTarget->GetAbsOrigin() - GetAbsOrigin();
			toVictim.z = 0.0f;
			VectorNormalize( toVictim );
			Vector forward;
			EyeVectors( &forward );
			forward.z = 0.0f;
			VectorNormalize( forward );
			if ( z_gun_survivor_shove_dot.GetFloat() > DotProduct( forward, toVictim ) && !pVictim->IsTongueVictim() )
				reject = true;
		}
		if ( reject )
		{
			if ( pWeapon->m_swingAffectedEntities.Count() < 128 )
				pWeapon->m_swingAffectedEntities.AddToTail( pTarget );
			return false;
		}
	}
	FOR_EACH_VEC( pWeapon->m_swingAffectedEntities, i )
	{
		if ( pWeapon->m_swingAffectedEntities[i].Get() == pTarget )
			return pWeapon->OnSwingHit( trace, direction, false );
	}
	if ( pWeapon->m_swingAffectedEntities.Count() < 128 )
		pWeapon->m_swingAffectedEntities.AddToTail( pTarget );
	return pWeapon->OnSwingHit( trace, direction, true );
}
#pragma float_control( pop )

void CTerrorWeapon::OnSwingEnd( bool bHitAny )
{
	if ( !m_bStompTarget )
		return;
	CBaseEntity *pTarget = m_hStompTarget.Get();
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pTarget || !pPlayer || !pPlayer->IsPlayer() )
		return;
	CTakeDamageInfo damage( pPlayer, pPlayer, (float)pTarget->GetHealth() + 1.0f, DMG_CLUB );
	pTarget->TakeDamage( damage );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "melee_kill" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		pEvent->SetInt( "entityid", pTarget->entindex() );
		pEvent->SetBool( "ambush", false );
		gameeventmanager->FireEvent( pEvent );
	}
}

// Native3EDDD0: complete pin/disposition/player/NextBot/damage/physics response order.
#pragma float_control( precise, on, push )
bool CTerrorWeapon::OnSwingHit( trace_t &trace, const Vector &direction, bool bNewHit )
{
	if ( !bNewHit )
		return false;
	CBaseEntity *pTarget = trace.m_pEnt;
	if ( !pTarget )
		return false;
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || !pPlayer->IsPlayer() )
		return false;
	CTerrorPlayer *pVictim = pTarget->IsPlayer() ? static_cast< CTerrorPlayer * >( pTarget ) : NULL;

	const int allowBash = TheDirector->GetScriptedMode()->AllowBash( pPlayer, pTarget );
	if ( allowBash == 2 )
		return false;
	const bool bashHandled = allowBash == 1;

	if ( pVictim && ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		pPlayer->GetTeamNumber() == pTarget->GetTeamNumber() )
	{
		if ( pVictim->IsTongueVictim() && pVictim->GetTongueOwner() )
		{
			if ( ( !TerrorGameRules()->IsPlayerControlledZombies() && !TerrorGameRules()->IsSurvivalMode() ) ||
				( pVictim->m_tongueOwner.Get() && !pVictim->m_isHangingFromTongue &&
					static_cast< double >( gpGlobals->curtime ) - static_cast< double >( pVictim->m_tongueVictimTimer.GetStartTime() ) > 1.0 ) ||
				pVictim->m_isHangingFromTongue )
			{
				IGameEvent *event = gameeventmanager->CreateEvent( pVictim->m_isHangingFromTongue ? "choke_stopped" : "tongue_pull_stopped" );
				if ( event )
				{
					event->SetInt( "userid", pPlayer->GetUserID() );
					event->SetInt( "victim", pVictim->GetUserID() );
					CTerrorPlayer *eventTongueOwner = pVictim->GetTongueOwner();
					event->SetInt( "smoker", eventTongueOwner ? eventTongueOwner->GetUserID() : 0 );
					event->SetInt( "release_type", 2 );
					gameeventmanager->FireEvent( event );
				}
				CTerrorPlayer *debugTongueOwner = pVictim->GetTongueOwner();
				if ( debugTongueOwner )
				{
					CTongue *tongue = dynamic_cast< CTongue * >( debugTongueOwner->GetCustomAbility() );
					if ( tongue )
						static_cast< CBaseAbility * >( tongue )->DebugVictim( pVictim, "Tongue letting go for a melee." );
				}
				CTerrorPlayer *releaseTongueOwner = pVictim->GetTongueOwner();
				if ( releaseTongueOwner )
					releaseTongueOwner->ReleaseTongue( false );
			}
		}
		else
		{
			CTerrorPlayer *pinner;
			bool recursiveHit = true;
			if ( pVictim->IsBeingPounced() )
				pinner = pVictim->GetPounceAttacker();
			else if ( pVictim->IsPummelVictim() )
				pinner = static_cast< CTerrorPlayer * >( pVictim->m_pummelAttacker.Get() );
			else
			{
				pinner = NULL;
				recursiveHit = false;
			}
			if ( recursiveHit )
			{
				// S3ED440 appends without a membership test, up to native cap128.
				if ( m_swingAffectedEntities.Count() < 128 )
					m_swingAffectedEntities.AddToTail( pinner );
				CBaseEntity *originalTarget = pTarget;
				trace.m_pEnt = pinner;
				OnSwingHit( trace, direction, true ); // genuine virtual recursion
				trace.m_pEnt = originalTarget;
				pTarget = originalTarget;
			}
		}
	}

	if ( !z_gun_survivor_friend_push.GetBool() && pPlayer->GetTeamNumber() == TEAM_SURVIVOR &&
		pPlayer->GetTeamNumber() == pTarget->GetTeamNumber() )
		return false;
	if ( pVictim && ( pVictim->IsInvulnerable() || pVictim->IsPerformingUseAction( 0 ) || pVictim->m_reviveTarget.Get() ) )
		return false;
	if ( TerrorGameRules()->IsPlayerControlledZombies() &&
		( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		pVictim && pVictim->GetTeamNumber() == TEAM_INFECTED && pVictim->GetZombieClass() == 3 )
	{
		// Native x87 FMUL/FCOS/FSTPfloat vs CRT cos is an unmeasured
		// precision boundary. Preserve widened FLOAT constants and one final
		// float store; no x87 emulation without measured divergence.
		bool lookingTowards;
		if ( pVictim->m_isAttemptingToPounce )
		{
			const float cosine = static_cast< float >( cos( static_cast< double >( versus_shove_hunter_fov_pouncing.GetFloat() ) * static_cast< double >( 0.5f ) *
				static_cast< double >( 0.017453292f ) ) );
			lookingTowards = pPlayer->IsLookingTowards( pVictim, cosine );
		}
		else
		{
			const float cosine = static_cast< float >( cos( static_cast< double >( versus_shove_hunter_fov.GetFloat() ) * static_cast< double >( 0.5f ) *
				static_cast< double >( 0.017453292f ) ) );
			lookingTowards = pPlayer->IsLookingTowards( pVictim->WorldSpaceCenter(), cosine );
		}
		if ( !lookingTowards )
			return false;
	}

	Vector normalizedDirection( direction.x, direction.y, direction.z > 0.1f ? direction.z : 0.1f );
	VectorNormalize( normalizedDirection );
	Vector shoveForce = normalizedDirection * z_gun_force.GetFloat();
	bool applyShoveForce = true;
	if ( pVictim )
	{
		// Force disposition ONLY, before player_shoved. Do not return here or
		// suppress the native damage/response/NextBot tails. Later survivor /
		// Boomer branches can overwrite shoveForce while this flag stays false.
		if ( pVictim->GetTeamNumber() == TEAM_INFECTED &&
			( pVictim->GetZombieClass() == 8 || pVictim->GetZombieClass() == 7 || !pVictim->CanBeShoved( pPlayer ) ) )
		{
			shoveForce = vec3_origin;
			applyShoveForce = false;
		}
		IGameEvent *event = gameeventmanager->CreateEvent( "player_shoved" );
		if ( event )
		{
			event->SetInt( "userid", pVictim->GetUserID() );
			event->SetInt( "attacker", pPlayer->GetUserID() );
			gameeventmanager->FireEvent( event );
		}
	}
	else
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "entity_shoved" );
		if ( event )
		{
			event->SetInt( "entityid", pTarget->entindex() );
			event->SetInt( "attacker", pPlayer->GetUserID() );
			gameeventmanager->FireEvent( event );
		}
	}

	const Vector hitPosition = trace.endpos;
	if ( pVictim )
	{
		if ( !pVictim->IsStaggering() )
			pTarget->SetAbsVelocity( vec3_origin );
		Vector hitDirection = direction;
		if ( ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
			( pTarget->GetTeamNumber() == TEAM_SURVIVOR || pTarget->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		{
			pPlayer->EyeVectors( &hitDirection );
			VectorNormalize( hitDirection );
			const float force = z_gun_survivor_force.GetFloat();
			shoveForce.Init( force * hitDirection.x, force * hitDirection.y,
				force * ( hitDirection.z > 0.1f ? hitDirection.z : 0.1f ) );
			if ( m_hitSoundTimer.IsElapsed() )
			{
				pPlayer->EmitSound( "Weapon.HitSurvivor" );
				m_hitSoundTimer.Start( survivor_hitsound_interval_timer.GetFloat() );
			}
		}
		else if ( ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
			pVictim->GetTeamNumber() == TEAM_INFECTED && pVictim->GetZombieClass() == 2 )
		{
			pPlayer->EyeVectors( &hitDirection );
			VectorNormalize( hitDirection );
			const float force = z_gun_force.GetFloat();
			shoveForce.Init( force * hitDirection.x, force * hitDirection.y,
				force * ( hitDirection.z > 0.1f ? hitDirection.z : 0.1f ) );
			if ( m_hitSoundTimer.IsElapsed() )
			{
				pPlayer->EmitSound( "Weapon.HitBoomer" );
				m_hitSoundTimer.Start( survivor_hitsound_interval_timer.GetFloat() );
			}
		}
		else if ( m_hitSoundTimer.IsElapsed() )
		{
			pPlayer->EmitSound( "Weapon.HitInfected" );
			m_hitSoundTimer.Start( survivor_hitsound_interval_timer.GetFloat() );
		}

		const Vector initialForce = shoveForce * 3.0f;
		CTakeDamageInfo damage( pPlayer, pPlayer, initialForce, hitPosition, 0.0f, 0x80 );
		CalculateMeleeDamageForce( &damage, hitDirection, pPlayer->Weapon_ShootPosition(), 1.0f );
		if ( applyShoveForce && pPlayer->GetTeamNumber() != pTarget->GetTeamNumber() )
		{
			if ( pVictim->GetTeamNumber() == TEAM_INFECTED && pVictim->GetZombieClass() == 3 && pVictim->IsPouncing() )
			{
				IGameEvent *event = gameeventmanager->CreateEvent( "pounce_stopped" );
				if ( event )
				{
					event->SetInt( "userid", pPlayer->GetUserID() );
					event->SetInt( "victim", pVictim->GetPounceVictim()->GetUserID() );
					gameeventmanager->FireEvent( event );
				}
				if ( z_zombie_knockoff_death.GetBool() )
				{
					damage.SetDamage( 500.0f );
					CalculateMeleeDamageForce( &damage, hitDirection, pPlayer->Weapon_ShootPosition(), 1.0f );
				}
			}
			else
			{
				CTerrorPlayer *tongueVictim = static_cast< CTerrorPlayer * >( pVictim->m_tongueVictim.Get() );
				if ( tongueVictim )
				{
					IGameEvent *event = gameeventmanager->CreateEvent( tongueVictim->m_isHangingFromTongue ? "choke_stopped" : "tongue_pull_stopped" );
					if ( event )
					{
						event->SetInt( "userid", pPlayer->GetUserID() );
						event->SetInt( "victim", tongueVictim->GetUserID() );
						event->SetInt( "smoker", pVictim->GetUserID() );
						event->SetInt( "release_type", 1 );
						gameeventmanager->FireEvent( event );
					}
				}
				pVictim->Bash( z_gun_stun_duration.GetFloat() );
			}
		}
		if ( damage.GetDamage() >= 1.0f && !bashHandled )
		{
			ClearMultiDamage();
			pTarget->DispatchTraceAttack( damage, hitDirection, &trace );
			ApplyMultiDamage();
		}
		const bool nearlyZero = shoveForce.x > -0.01f && shoveForce.x < 0.01f &&
			shoveForce.y > -0.01f && shoveForce.y < 0.01f && shoveForce.z > -0.01f && shoveForce.z < 0.01f;
		if ( !nearlyZero )
		{
			if ( pVictim->GetTeamNumber() == TEAM_INFECTED )
				pVictim->MeleeRepel( pPlayer, shoveForce );
			else if ( !pVictim->IsBeingPounced() && !pVictim->IsPummelVictim() && !pVictim->IsInvulnerable() &&
				!pVictim->CanBeRevived() && !pVictim->IsKnockedDown() )
			{
				if ( pVictim->IsJockeyVictim() )
					static_cast< CTerrorPlayer * >( pVictim->m_jockeyAttacker.Get() )->MeleeRepel( pPlayer, shoveForce );
				else
				{
					const float pitch = RandomFloat( survivor_team_hit_pitch_min.GetFloat(), survivor_team_hit_pitch_max.GetFloat() );
					const float yaw = RandomFloat( survivor_team_hit_yaw_min.GetFloat(), survivor_team_hit_yaw_max.GetFloat() );
					pVictim->SetPunchAngle( QAngle( pitch, yaw, 0.0f ) );
					pVictim->DoTerrorAnimationEvent( pVictim->GetDamageAnimationEvent( damage ), 0 );
					if ( survivor_shove_teammates.GetBool() )
					{
						pVictim->SetShoveForce( shoveForce );
						pVictim->SetLaunchSource( pPlayer );
					}
				}
			}
		}
		// Player-backed NextBots reach this only AFTER all player transitions,
		// damage and movement. Native RTTI is intentional (not all entities'
		// MyNextBotPointer implementations are equivalent to this cross-cast).
		INextBot *bot = dynamic_cast< INextBot * >( pTarget );
		if ( bot )
			bot->OnShoved( pPlayer );
		return false;
	}

	INextBot *bot = dynamic_cast< INextBot * >( pTarget );
	if ( bot )
	{
		Infected *infected = pTarget->MyInfectedPointer();
		if ( infected )
			infected->SetShoveImmune( bashHandled ); // S3EE9DD: native7572 gates this OnShoved dispatch only.
		bot->OnShoved( pPlayer );
		if ( infected )
			infected->SetShoveImmune( false );
		if ( !m_hitSoundTimer.IsElapsed() )
			return false;
		CBaseCombatCharacter *botEntity = bot->GetEntity();
		if ( botEntity->GetGender() == 16 )
		{
			pPlayer->EmitSound( "Weapon.HitClown" );
			IGameEvent *event = gameeventmanager->CreateEvent( "punched_clown" );
			if ( event )
			{
				event->SetInt( "userid", pPlayer->GetUserID() );
				gameeventmanager->FireEvent( event );
				m_hitSoundTimer.Start( survivor_hitsound_interval_timer.GetFloat() );
				return false;
			}
		}
		else
			pPlayer->EmitSound( "Weapon.HitInfected" );
		m_hitSoundTimer.Start( survivor_hitsound_interval_timer.GetFloat() );
		return false;
	}

	const Vector initialForce = shoveForce * 3.0f;
	CTakeDamageInfo damage( pPlayer, pPlayer, initialForce, hitPosition, z_gun_damage.GetFloat(), 0x80 );
	CalculateMeleeDamageForce( &damage, direction, pPlayer->Weapon_ShootPosition(), 1.0f );
	if ( pTarget->IsWorld() )
	{
		if ( m_hitSoundTimer.IsElapsed() )
		{
			pPlayer->EmitSound( "Weapon.HitWorld" );
			m_hitSoundTimer.Start( survivor_hitsound_interval_timer.GetFloat() );
		}
		return false;
	}
	if ( pTarget->GetNonShadowMass() > 400.0f ||
		( !TerrorGameRules()->IsRoundInProgress() && TerrorGameRules()->IsSurvivalMode() ) )
	{
		damage.AddDamageType( 0x800 );
		damage.SetDamageForce( damage.GetDamageForce() * 0.0f );
		normalizedDirection = vec3_origin;
	}
	if ( !bashHandled )
	{
		ClearMultiDamage();
		pTarget->DispatchTraceAttack( damage, direction, &trace );
		ApplyMultiDamage();
	}
	if ( pTarget->GetMoveType() != MOVETYPE_NONE && pTarget->GetMoveType() != MOVETYPE_PUSH &&
		!pTarget->ClassMatches( "func_*" ) && !pTarget->ClassMatches( "prop_door*" ) )
		pTarget->ApplyAbsVelocityImpulse( normalizedDirection * z_gun_physics_force.GetFloat() );
	return false;
}
#pragma float_control( pop )

void CTerrorPlayer::InputRemoveWeaponUpgrades( inputdata_t &inputdata )
{
	// S2FD390: one primary-slot query and the genuine weapon virtual.
	CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( Weapon_GetSlot( 0 ) );
	if ( weapon )
		weapon->RemoveAllUpgrades();
}
