//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terror_playeranimstate.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/c_base_mounted_weapon.h"
#include "l4d2/entities/c_func_elevator.h"
#include "c_playerresource.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/weapons/base_mounted_weapon.h"
#endif
#include "l4d2/infected/base_ability.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/weapon_chainsaw.h"
#include "l4d2/weapons/weapon_basecsgrenade.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "activitylist.h"
#include "animation.h"
#include "engine/ivdebugoverlay.h"
#include "studio.h"
#include "bone_setup.h"
#include "physics_shared.h"
#include "datacache/imdlcache.h"
#include "in_buttons.h"

#include "l4d2/player/terror_playeranimstate_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern ConVar mp_feetyawrate;
extern ConVar anim_showstate;
extern ConVar mp_showgestureslots;

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

static bool s_bDumpTerrorAnimationStates;

#ifdef CLIENT_DLL
// C27E700 also requests the server's independent dump.

#if defined( CLIENT_DLL )
CON_COMMAND_F( anim_dumpstate, "Dump the animation state for all the player classes.", 0x0000400A )
#else
CON_COMMAND_F( anim_dumpstate, "Dump the animation state for all the player classes.", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY )
#endif
{
	engine->ClientCmd( "sv_anim_dumpstate" );
	s_bDumpTerrorAnimationStates = true;
}
#else

#if defined( GAME_DLL )
CON_COMMAND_F( sv_anim_dumpstate, "Dump the animation state for all the player classes.", 0x00004006 )
#else
CON_COMMAND_F( sv_anim_dumpstate, "Dump the animation state for all the player classes.", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY )
#endif
{
	s_bDumpTerrorAnimationStates = true;
}
#endif


// Original registrations S:0x582650 and S:0x59DB00. Despite its name,
// mp_feetmaxyawrate is the elapsed-time threshold in S:0x341390.
ConVar mp_feetyawrate_max( "mp_feetyawrate_max", "360", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"How many degrees per second that we can turn our feet or upper body when we're outside the max body yaw." );
ConVar mp_feetmaxyawrate( "mp_feetmaxyawrate", "100.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
static ConVar z_minigun_fire_anim_speed( "z_minigun_fire_anim_speed", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "cycle rate for main sequence when firing miniguns" );
#ifndef CLIENT_DLL
static ConVar fidget_min_interval( "fidget_min_interval", "10"
#if defined( GAME_DLL )
	, 0x00000006
#endif
 );
static ConVar fidget_max_interval( "fidget_max_interval", "20"
#if defined( GAME_DLL )
	, 0x00000006
#endif
 );
#endif

// The generic movement configuration is unused by Terror's custom update.
// These are not the original custom aim gains at state offsets 0x138-0x148.
static MultiPlayerMovementData_t s_TerrorMovementData = { -1.0f, -1.0f, -1.0f, 0.0f };

static Activity TerrorPlayerActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x33BA20. Above 90 degrees the eye delta, rather than the feet delta,
// selects the turn direction. Native JBE also admits unordered comparisons.
static void ConvergeSurvivorFeetYaw( float flGoalYaw, float flEyeYaw,
	float flYawRate, float flYawRateCap, float flDeltaTime, float &flCurrentYaw )
{
	const float flDelta = AngleNormalize( flGoalYaw - flCurrentYaw );
	const float flDistance = fabs( flDelta );
	const float flEyeDelta = AngleNormalize( flEyeYaw - flCurrentYaw );
	const float flDirection = !( flDistance > 90.0f ) ? flDelta : flEyeDelta;
	float flBlend = 1.0f;
	if ( flDistance < 45.0f )
		flBlend = clamp( flDistance * 0.022222223f, 0.01f, 1.0f );
	float flRate = flBlend * flYawRate;
	if ( flRate > flYawRateCap )
		flRate = flYawRateCap;
	const float flStep = flRate * flDeltaTime;
	if ( !( flStep > flDistance ) )
		flCurrentYaw = AngleNormalize( flCurrentYaw + ( flDirection > 0.0f ? flStep : -flStep ) );
	else
		flCurrentYaw = AngleNormalize( flGoalYaw );
}

static float SurvivorAimStep( float flDelta, float flGain, float flMultiplier )
{
	float flStep = flGain * flDelta * flMultiplier * gpGlobals->frametime;
	if ( fabs( flStep ) > fabs( flDelta ) )
		flStep = flDelta;
	return flStep;
}

static void ReadTerrorAimPoseParameters( CTerrorPlayer *pPlayer, int nHeadPose, int nBodyPose,
	float &flHead, float &flBody )
{
	flHead = pPlayer->GetPoseParameter( nHeadPose );
	flBody = pPlayer->GetPoseParameter( nBodyPose );
#ifdef CLIENT_DLL
	// Native client getters return normalized storage; server getters already
	// return the physical pose range. Keep both reads before either range query.
	float flMin, flMax;
	pPlayer->GetPoseParameterRange( nHeadPose, flMin, flMax );
	flHead = flMin + ( flMax - flMin ) * flHead;
	pPlayer->GetPoseParameterRange( nBodyPose, flMin, flMax );
	flBody = flMin + ( flMax - flMin ) * flBody;
#endif
}

CTerrorPlayerAnimState::CTerrorPlayerAnimState( CTerrorPlayer *pPlayer )
	: BaseClass( pPlayer, s_TerrorMovementData )
{
	// S:0x33F3A0 ->0x124030 ->0x33C960. The base constructor allocates and
	// initializes the seven slots once; do not initialize another slot vector.
	m_nHeadPitchPoseParameter = -1;
	m_nHeadYawPoseParameter = -1;
	m_nLeanXPoseParameter = -1;
	m_nLeanYPoseParameter = -1;
	m_nDismountPoseParameter = -1;
	m_nTongueAnglePoseParameter = -1;
	m_nVerticalVelocityPoseParameter = -1;
	m_nHorizontalVelocityPoseParameter = -1;
	m_nJockeyXPoseParameter = -1;
	m_nJockeyYPoseParameter = -1;
	ClearAnimationState();
}

CTerrorPlayer *CTerrorPlayerAnimState::GetTerrorPlayer()
{
	return static_cast< CTerrorPlayer * >( GetBasePlayer() );
}

bool CTerrorPlayerAnimState::IsGrenadeProjectilePresentationActive()
{
	// S33DD80/C281870: exact four resolving handles, active action and QC timer.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( pPlayer->m_reviveTarget.Get() ) return true;
	if ( pPlayer->m_pounceAttacker.Get() ) return true;
	if ( pPlayer->m_pummelAttacker.Get() ) return true;
	if ( pPlayer->m_jockeyAttacker.Get() ) return true;
	CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase * >( pPlayer->GetActiveWeapon() );
	if ( pWeapon && pWeapon->IsPerformingAction() ) return true;
	return m_grenadePresentationTimer.m_timestamp > 0.0f && !m_grenadePresentationTimer.IsElapsed();
}

void CTerrorPlayerAnimState::FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options )
{
	// Native model QC56/57, not Activity or player-event ordinals.
	if ( event == 56 )
	{
		if ( options )
			m_grenadePresentationTimer.Start( static_cast< float >( atof( options ) ) );
	}
	else if ( event == 57 )
		m_grenadePresentationTimer.Invalidate();
}

#ifdef CLIENT_DLL
void CTerrorPlayerAnimState::DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData )
{
	if ( TerrorGameRules()->IsInIntro() )
		return;
	switch ( event )
	{
	case TERROR_ANIMEVENT_FIRE_PRIMARY:
	case TERROR_ANIMEVENT_FIRE_PRIMARY_ALT:
		GetTerrorPlayer()->ProcessMuzzleFlashEvent();
		return;
	case TERROR_ANIMEVENT_JUMP:
		BaseClass::DoAnimationEvent( PLAYERANIMEVENT_JUMP, nData );
		return;
	case TERROR_ANIMEVENT_DYING:
		BaseClass::DoAnimationEvent( PLAYERANIMEVENT_DIE, nData );
		return;
	case TERROR_ANIMEVENT_RELOAD_END:
		BaseClass::DoAnimationEvent( PLAYERANIMEVENT_RELOAD_END, nData );
		return;
	default:
		// C27DB00 ignores4/5; the reached >24 cases fall through its native
		// base dispatcher without an effect. Never cast the compact enum.
		return;
	}
}
#endif

// S33C110/C27E5D0 and S33C140/C27E600 own accumulated animation motion.
void CTerrorPlayerAnimState::GetAnimationMovement( Vector *pMovement ) const
{
	if ( pMovement )
		*pMovement = m_vecAnimationMovement;
}

void CTerrorPlayerAnimState::ResetAnimationMovement()
{
	m_vecAnimationMovement.Init();
}

// S33C840 includes the raw hanging flag; the client C27E590 uses activity only.
bool CTerrorPlayerAnimState::IsLedgeClimbAnimation()
{
#ifndef CLIENT_DLL
	if ( !GetTerrorPlayer()->m_isHangingFromLedge )
		return false;
#endif
	const TerrorOriginalActivity originalActivity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	return originalActivity == TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB ||
		originalActivity == TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB_TO_CROUCH;
}

void CTerrorPlayerAnimState::ClearAnimationState()
{
	// S:0x33C960 leaves the feet-initialized flag alone, even while resetting
	// the two yaws. Calling the generic clear here would change that behavior.
	m_flPreviousMovementCycle = 0.0f;
	ResetAnimationMovement();
	m_flGoalFeetYaw = 0.0f;
	m_flCurrentFeetYaw = 0.0f;
	m_flLastAimTurnTime = 0.0f;
	m_flAimUpdateMultiplier = 1.0f;
	m_flJockeyMoveX = 0.0f;
	m_flJockeyMoveY = 0.0f;
	m_bWasAirborne = false;
	m_flAirborneStartTime = 0.0f;
#ifndef CLIENT_DLL
	// S33CA10's DWORD340 clears the independent bytes 340..343.
	m_bSequenceOverride = false;
	m_bTerrorDying = false;
	m_bUseGestureActive = false;
	m_bReturnToLifePending = false;
	// WORD344 and DWORD346 clear only 344..349, never native byte350.
	m_bChargerImpact = false;
	m_bChargerPummeled = false;
	m_bChargerSlammedWall = false;
	m_bChargerSlammedGround = false;
	m_bChargerPummelVictimNotified = false;
	m_bChargerPounded = false;
	// Native DWORD351/DWORD355/DWORD359/WORD363 clear 351..364.
	m_bHitByTank = false;
	m_bSpitting = false;
	m_bPounced = false;
	m_bTongueVictim = false;
	m_bChainsawStartup = false;
	m_bHunterPounce = false;
	m_bJockeyRide = false;
	m_bJockeyRidden = false;
	m_bTongueAttack = false;
	m_bLunging = false;
	m_bChargerPummel = false;
	m_bChargerPound = false;
	m_bTongueChoke = false;
	m_bTongueWallHit = false;
	m_nThrowAnimation = -1;
	m_nClimbAnimation = -1;
	m_nTankSpecialAnimation = -1;
	m_nStaggerDirection = -1;
	m_bFidgetDisabled = false;
	m_fidgetTimer.Start( RandomInt( fidget_min_interval.GetInt(), fidget_max_interval.GetInt() ) );
#endif
	m_grenadePresentationTimer.Invalidate();
	ResetGestureSlots();

}

bool CTerrorPlayerAnimState::SetupSurvivorPoseParameters( CStudioHdr *pStudioHdr )
{
	if ( m_bPoseParameterInit )
		return true;
	if ( !pStudioHdr )
		return false;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();

	// S:0x122D80. Zero lean only when both lean parameters exist.
	m_PoseParameterData.m_iMoveX = pPlayer->LookupPoseParameter( pStudioHdr, "move_x" );
	m_PoseParameterData.m_iMoveY = pPlayer->LookupPoseParameter( pStudioHdr, "move_y" );
	m_nLeanXPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "lean_x" );
	m_nLeanYPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "lean_y" );
	if ( m_nLeanXPoseParameter >= 0 && m_nLeanYPoseParameter >= 0 )
	{
		pPlayer->SetPoseParameter( pStudioHdr, m_nLeanXPoseParameter, 0.0f );
		pPlayer->SetPoseParameter( pStudioHdr, m_nLeanYPoseParameter, 0.0f );
	}
	m_PoseParameterData.m_iAimPitch = pPlayer->LookupPoseParameter( pStudioHdr, "body_pitch" );
	m_PoseParameterData.m_iAimYaw = pPlayer->LookupPoseParameter( pStudioHdr, "body_yaw" );
	m_nHeadPitchPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "head_pitch" );
	m_nHeadYawPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "head_yaw" );
	m_nDismountPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "dismount" );
	m_nTongueAnglePoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "tongue_angle" );
	m_nVerticalVelocityPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "vert_vel" );
	m_nHorizontalVelocityPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "horiz_vel" );
	m_nJockeyXPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "jockey_x" );
	m_nJockeyYPoseParameter = pPlayer->LookupPoseParameter( pStudioHdr, "jockey_y" );
	m_bPoseParameterInit = true;
	return true;
}

#ifndef CLIENT_DLL
void CTerrorPlayerAnimState::UpdateIdleGesture()
{
	if ( m_bFidgetDisabled || !m_fidgetTimer.IsElapsed() )
		return;
	m_fidgetTimer.Start( RandomInt( fidget_min_interval.GetInt(), fidget_max_interval.GetInt() ) );
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
	if ( m_eCurrentMainSequenceActivity == ACT_IDLE && pWeapon && pWeapon->CanFidget() &&
		!IsGestureSlotActive( GESTURE_SLOT_ATTACK_AND_RELOAD ) &&
		pPlayer->GetTimeSinceWeaponFired() >= 4.0 )
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_FIDGET, 0 );
}
#endif

#ifndef CLIENT_DLL
Activity CTerrorPlayerAnimState::CalcMainActivity()
{
	// S342820: each successful action preempts only the selectors after it.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( pPlayer->m_usingMountedWeapon )
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_PRIMARYATTACK_MINIGUN );
	UpdateChainsawGestures();
	Activity activity;
	if ( ( activity = CalcTerrorDyingActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcReturnToLifeActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcSpitActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcChargerPummelActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcChargerPummelVictimActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcChargerImpactActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcChargerSlammedWallActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcChargerSlammedGroundActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcCarriedActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcTankPunchActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcTongueVictimActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcPounceVictimActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcJockeyVictimActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcSpecialMoveActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcHunterPounceActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcJockeyRideActivity() ) != ACT_INVALID ) return activity;
	if ( m_bLunging )
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_CHARGER_CHARGE );
	if ( ( activity = CalcSmokerTongueActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcIncapacitatedActivity() ) != ACT_INVALID ) return activity;
	if ( pPlayer->m_isFallingFromLedge )
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_FALL );
	if ( ( activity = CalcLadderActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcPushActivity() ) != ACT_INVALID ) return activity;
	if ( ( activity = CalcStaggerActivity() ) != ACT_INVALID ) return activity;
	return BaseClass::CalcMainActivity();
}

// S33B9C0 ->121EE0 is the conditional +20 entry, not low-level +56.
void CTerrorPlayerAnimState::RestartMainSequenceIfUnlocked()
{
	if ( m_bSequenceOverride )
		return;
	m_eCurrentMainSequenceActivity = ACT_IDLE;
	RestartMainSequence();
	GetTerrorPlayer()->ResetSequence( SelectWeightedSequence( TranslateActivity( ACT_IDLE ) ) );
}

int CTerrorPlayerAnimState::GetSequenceOverride()
{
	return m_bSequenceOverride ? GetTerrorPlayer()->GetSequence() : -1;
}

void CTerrorPlayerAnimState::ClearSequenceOverride()
{
	// S33C170: no restart, timer, activity, or other animation-state reset.
	m_bSequenceOverride = false;
}

void CTerrorPlayerAnimState::ForceSequenceOverride( int sequence )
{
	// S33C880 ignores the captured cycle argument. The original calls the
	// base restart directly, after publishing its real sequence and clock.
	MDLCACHE_CRITICAL_SECTION();
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	pPlayer->SetSequence( sequence );
	pPlayer->m_fServerAnimStartTime = gpGlobals->curtime;
	pPlayer->ResetSequenceInfo();
	BaseClass::RestartMainSequence();
	m_bSequenceOverride = true;
}

void CTerrorPlayerAnimState::ComputeMainSequence()
{
	// S33D600 clears the lock without recomputing at the strict boundary.
	if ( !m_bSequenceOverride )
		BaseClass::ComputeMainSequence();
	else if ( !GetTerrorPlayer()->SequenceLoops() && GetTerrorPlayer()->GetCycle() > 0.99000001f )
		m_bSequenceOverride = false;
}


// S33C470 resets four authored upper-body poses, in this exact order.
void CTerrorPlayerAnimState::ResetUpperBodyPoseParameters()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	CStudioHdr *pStudioHdr = pPlayer->GetModelPtr();
	if ( pStudioHdr && pStudioHdr->IsValid() )
	{
		pPlayer->SetPoseParameter( pStudioHdr, m_nHeadYawPoseParameter, 0.0f );
		pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iAimYaw, 0.0f );
		pPlayer->SetPoseParameter( pStudioHdr, m_nHeadPitchPoseParameter, 0.0f );
		pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iAimPitch, 0.0f );
	}
}

// S33C2A0; 669 is the authored forward-running Tank death.
Activity CTerrorPlayerAnimState::CalcTerrorDyingActivity()
{
	if ( !m_bTerrorDying )
		return ACT_INVALID;
	const int current = GetOriginalMainActivity();
	if ( current == 667 || current == 669 )
		return m_eCurrentMainSequenceActivity;
	RestartMainSequence();
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( pPlayer->GetTeamNumber() != TEAM_INFECTED || pPlayer->GetZombieClass() != 8 )
		return TerrorActivityFromOriginal( 667 );
	Vector velocity = pPlayer->GetAbsVelocity();
	if ( velocity.Length2D() <= 50.0f )
		return TerrorActivityFromOriginal( 667 );
	velocity.z = 0.0f;
	VectorNormalize( velocity );
	Vector forward;
	pPlayer->EyeVectors( &forward );
	return TerrorActivityFromOriginal( DotProduct( forward, velocity ) > 0.69999999f ? 669 : 667 );
}

Activity CTerrorPlayerAnimState::CalcReturnToLifeActivity()
{
	if ( !m_bReturnToLifePending )
		return ACT_INVALID;
	if ( GetTerrorPlayer()->m_iCurrentUseAction != 5 )
	{
		m_bReturnToLifePending = false;
		return ACT_INVALID;
	}
	if ( GetOriginalMainActivity() != 697 )
		RestartMainSequence();
	return TerrorActivityFromOriginal( 697 );
}

Activity CTerrorPlayerAnimState::CalcSpitActivity()
{
	if ( !m_bSpitting )
		return ACT_INVALID;
	if ( GetOriginalMainActivity() != 543 )
	{
		RestartMainSequence();
		return TerrorActivityFromOriginal( 543 );
	}
	if ( GetTerrorPlayer()->GetCycle() > 0.99000001f )
	{
		m_bSpitting = false;
		return ACT_INVALID;
	}
	return m_eCurrentMainSequenceActivity;
}

Activity CTerrorPlayerAnimState::CalcChargerSlammedWallActivity()
{
	if ( m_bChargerSlammedWall && !GetTerrorPlayer()->IsIncapacitated() )
	{
		if ( GetOriginalMainActivity() != 529 )
		{
			RestartMainSequence();
			return TerrorActivityFromOriginal( 529 );
		}
		if ( GetTerrorPlayer()->GetCycle() <= 0.99000001f )
			return TerrorActivityFromOriginal( 529 );
		m_bChargerSlammedWall = false;
	}
	return ACT_INVALID;
}

Activity CTerrorPlayerAnimState::CalcChargerSlammedGroundActivity()
{
	if ( m_bChargerSlammedGround && !GetTerrorPlayer()->IsIncapacitated() )
	{
		if ( GetOriginalMainActivity() != 530 )
		{
			RestartMainSequence();
			return TerrorActivityFromOriginal( 530 );
		}
		if ( GetTerrorPlayer()->GetCycle() <= 0.99000001f )
			return TerrorActivityFromOriginal( 530 );
		m_bChargerSlammedGround = false;
	}
	return ACT_INVALID;
}

Activity CTerrorPlayerAnimState::CalcCarriedActivity()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer->m_carryAttacker.Get() || pPlayer->IsIncapacitated() )
		return ACT_INVALID;
	if ( GetOriginalMainActivity() != 528 || pPlayer->GetCycle() > 0.99000001f )
		RestartMainSequence();
	return TerrorActivityFromOriginal( 528 );
}

// S33FA00. Unlike Charger impact, this branch does not create a knockdown.
Activity CTerrorPlayerAnimState::CalcTankPunchActivity()
{
	if ( !m_bHitByTank )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	const int team = pPlayer->GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
	{
		m_bHitByTank = false;
		return ACT_INVALID;
	}
	if ( GetOriginalMainActivity() == 521 || GetOriginalMainActivity() == 522 )
	{
		const Vector &velocity = pPlayer->GetAbsVelocity();
		// S33FA80..FAB9: preserve (z*z + x*x) + y*y before sqrtss.
		const float speed = sqrtf( ( velocity.z * velocity.z + velocity.x * velocity.x ) +
			velocity.y * velocity.y );
		if ( pPlayer->GetGroundEntity() || ( pPlayer->GetCycle() < 0.25f && speed < 5.0f ) )
		{
			RestartMainSequence();
			return TerrorActivityFromOriginal( 523 );
		}
	}
	if ( pPlayer->GetCycle() > 0.99000001f )
	{
		RestartMainSequence();
		switch ( GetOriginalMainActivity() )
		{
		case 521: return TerrorActivityFromOriginal( 522 );
		case 522: return TerrorActivityFromOriginal( 523 );
		case 523:
			m_bHitByTank = false;
			return ACT_INVALID;
		default: break;
		}
	}
	if ( GetOriginalMainActivity() == 523 &&
		( m_bJockeyRidden || ( pPlayer->GetCycle() > 0.5f && !pPlayer->IsKnockedDown() &&
			( pPlayer->m_nButtons & 0x61A ) ) ) )
	{
		m_bHitByTank = false;
		return ACT_INVALID;
	}
	const int current = GetOriginalMainActivity();
	if ( current >= 521 && current <= 523 )
		return m_eCurrentMainSequenceActivity;
	RestartMainSequence();
	return TerrorActivityFromOriginal( 521 );
}

Activity CTerrorPlayerAnimState::CalcTongueVictimActivity()
{
	if ( !m_bTongueVictim )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer->m_tongueOwner.Get() )
	{
		m_bTongueVictim = false;
		if ( pPlayer->IsIncapacitated() )
		{
			RestartMainSequence();
			return TerrorActivityFromOriginal( 773 );
		}
		return ACT_INVALID;
	}
	int selected = 775;
	if ( pPlayer->GetGroundEntity() )
	{
		if ( pPlayer->m_reachedTongueOwner )
			selected = 783;
		else if ( pPlayer->m_tongueOwner.Get() && pPlayer->m_isProneTongueDrag &&
			pPlayer->m_tongueVictimTimer.IsGreaterThen( 1.0f ) )
			selected = 779;
		else
			selected = 958;
	}
	if ( GetOriginalMainActivity() != selected || pPlayer->GetCycle() > 0.99000001f )
		RestartMainSequence();
	return TerrorActivityFromOriginal( selected );
}

Activity CTerrorPlayerAnimState::CalcPounceVictimActivity()
{
	if ( !m_bPounced )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( pPlayer->m_pounceAttacker.Get() )
	{
		if ( GetOriginalMainActivity() != 770 || pPlayer->GetCycle() > 0.99000001f )
			RestartMainSequence();
		return TerrorActivityFromOriginal( 770 );
	}
	if ( !pPlayer->IsIncapacitated() )
	{
		if ( !m_bJockeyRidden )
		{
			if ( GetOriginalMainActivity() == 771 )
			{
				if ( pPlayer->GetCycle() > 0.99000001f ||
					( pPlayer->GetAbsVelocity().Length2D() > 50.0f && pPlayer->GetCycle() > 0.2f ) )
				{
					m_bPounced = false;
					return ACT_INVALID;
				}
			}
			else
				RestartMainSequence();
			return TerrorActivityFromOriginal( 771 );
		}
		m_bPounced = false;
		return ACT_INVALID;
	}
	m_bPounced = false;
	RestartMainSequence();
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
	return TerrorActivityFromOriginal( pWeapon && pWeapon->IsDualWielding() ? 701 : 700 );
}

Activity CTerrorPlayerAnimState::CalcJockeyVictimActivity()
{
	if ( !m_bJockeyRidden )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer->m_jockeyAttacker.Get() || pPlayer->IsIncapacitated() )
	{
		m_bJockeyRidden = false;
		return ACT_INVALID;
	}
	if ( GetOriginalMainActivity() != 799 || pPlayer->GetCycle() > 0.99000001f )
		RestartMainSequence();
	return TerrorActivityFromOriginal( 799 );
}

Activity CTerrorPlayerAnimState::CalcHunterPounceActivity()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( pPlayer->m_isAttemptingToPounce )
	{
		if ( GetOriginalMainActivity() == 747 )
		{
			if ( pPlayer->GetCycle() > 0.99000001f )
			{
				RestartMainSequence();
				return TerrorActivityFromOriginal( 748 );
			}
			return TerrorActivityFromOriginal( 747 );
		}
		if ( GetOriginalMainActivity() != 748 )
		{
			RestartMainSequence();
			return TerrorActivityFromOriginal( 747 );
		}
		if ( !pPlayer->m_pounceVictim.Get() )
		{
			if ( pPlayer->GetCycle() > 0.99000001f )
				RestartMainSequence();
			return TerrorActivityFromOriginal( 748 );
		}
		m_bHunterPounce = false;
		return ACT_INVALID;
	}
	if ( !m_bHunterPounce )
		return ACT_INVALID;
	if ( !pPlayer->m_pounceVictim.Get() )
	{
		m_bHunterPounce = false;
		return ACT_INVALID;
	}
	if ( pPlayer->GetCycle() > 0.99000001f || GetOriginalMainActivity() != 764 )
		RestartMainSequence();
	return TerrorActivityFromOriginal( 764 );
}

Activity CTerrorPlayerAnimState::CalcJockeyRideActivity()
{
	if ( !m_bJockeyRide )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer->m_jockeyVictim.Get() )
	{
		m_bJockeyRide = false;
		return ACT_INVALID;
	}
	if ( GetOriginalMainActivity() != 798 || pPlayer->GetCycle() > 0.99000001f )
		RestartMainSequence();
	return TerrorActivityFromOriginal( 798 );
}

Activity CTerrorPlayerAnimState::CalcSmokerTongueActivity()
{
	if ( !m_bTongueAttack )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !m_bTongueChoke )
	{
		if ( m_bTongueWallHit )
		{
			if ( pPlayer->GetCycle() > 0.99000001f || GetOriginalMainActivity() != 755 )
				RestartMainSequence();
			return TerrorActivityFromOriginal( 755 );
		}
		if ( GetOriginalMainActivity() == 750 )
		{
			if ( pPlayer->GetCycle() > 0.99000001f )
			{
				if ( !pPlayer->m_tongueVictim.Get() )
				{
					DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_ATTACK_END );
					return ACT_INVALID;
				}
				RestartMainSequence();
				return TerrorActivityFromOriginal( 751 );
			}
		}
		else if ( GetOriginalMainActivity() == 751 )
		{
			if ( pPlayer->GetCycle() > 0.99000001f )
				RestartMainSequence();
			return TerrorActivityFromOriginal( 751 );
		}
		else
			RestartMainSequence();
		return TerrorActivityFromOriginal( 750 );
	}
	if ( GetOriginalMainActivity() == 752 )
	{
		if ( pPlayer->GetCycle() > 0.99000001f )
		{
			RestartMainSequence();
			return TerrorActivityFromOriginal( 753 );
		}
		return TerrorActivityFromOriginal( 752 );
	}
	if ( GetOriginalMainActivity() != 753 )
	{
		RestartMainSequence();
		return TerrorActivityFromOriginal( 752 );
	}
	if ( pPlayer->GetCycle() > 0.99000001f )
		RestartMainSequence();
	return TerrorActivityFromOriginal( 753 );
}

Activity CTerrorPlayerAnimState::CalcLadderActivity()
{
	if ( GetTerrorPlayer()->GetMoveType() != MOVETYPE_LADDER )
		return ACT_INVALID;
	Vector velocity;
	GetOuterAbsVelocity( velocity );
	if ( velocity.z > 0.0f )
		return ACT_CLIMB_UP;
	if ( velocity.z == 0.0f )
		return GetOriginalMainActivity() == TERROR_ORIGINAL_ACT_CLIMB_DOWN ? ACT_CLIMB_DOWN : ACT_CLIMB_UP;
	return ACT_CLIMB_DOWN;
}

Activity CTerrorPlayerAnimState::CalcPushActivity()
{
	return GetTerrorPlayer()->m_pushEntity.Get() && GetOriginalMainActivity() != 704 ?
		TerrorActivityFromOriginal( 704 ) : ACT_INVALID;
}

Activity CTerrorPlayerAnimState::CalcStaggerActivity()
{
	if ( m_nStaggerDirection == -1 )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !( pPlayer->m_staggerTimer.HasStarted() &&
		gpGlobals->curtime < pPlayer->m_staggerTimer.m_timestamp.Get() ) )
	{
		m_nStaggerDirection = -1;
		pPlayer->ClearStagger();
		return ACT_INVALID;
	}
	ResetUpperBodyPoseParameters();
	int base = -1;
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
	if ( pWeapon )
	{
		switch ( pWeapon->GetWeaponID() )
		{
		case TERROR_WEAPON_PISTOL:
		case TERROR_WEAPON_PISTOL_MAGNUM: base = 633; break;
		case TERROR_WEAPON_CHAINSAW: base = 625; break;
		case TERROR_WEAPON_MELEE:
			base = TerrorActivityToOriginal( static_cast< CTerrorMeleeWeapon * >( pWeapon )->GetMeleeShoveActivity() );
			break;
		default: base = 621; break;
		}
	}
	int selected;
	switch ( m_nStaggerDirection )
	{
	case 0: case 1: case 2: case 3: selected = base + m_nStaggerDirection; break;
	case 4: selected = 768; break;
	case 5: selected = 767; break;
	case 6: selected = 765; break;
	case 7: selected = 766; break;
	default: return ACT_INVALID;
	}
	if ( GetOriginalMainActivity() != selected )
	{
		RestartMainSequence();
		return TerrorActivityFromOriginal( selected );
	}
	if ( pPlayer->GetCycle() <= 0.99000001f )
		return TerrorActivityFromOriginal( selected );
	m_nStaggerDirection = -1;
	pPlayer->ClearStagger();
	return ACT_INVALID;
}

void CTerrorPlayerAnimState::UpdateChainsawGestures()
{
	const int slot = GESTURE_SLOT_ATTACK_AND_RELOAD;
	const Activity startup = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_CHAINSAW_STARTUP );
	const Activity idleAttack = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_IDLE_ATTACK_CHAINSAW );
	const Activity shoot = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_SHOOT_CHAINSAW );
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( GetTerrorPlayer()->GetActiveWeapon() );
	if ( pWeapon && pWeapon->GetWeaponID() == TERROR_WEAPON_CHAINSAW )
	{
		if ( m_bChainsawStartup )
		{
			if ( m_aGestureSlots[slot].m_iActivity == startup )
			{
				if ( GetGestureSlotLayer( slot )->m_flCycle > 0.99000001f )
					m_bChainsawStartup = false;
			}
			else
				RestartGesture( slot, startup, true, false );
		}
		CChainsaw *pSaw = dynamic_cast< CChainsaw * >( pWeapon );
		if ( pSaw->m_iState == CChainsaw::ACTIVE )
		{
			if ( m_aGestureSlots[slot].m_iActivity != idleAttack )
				RestartGesture( slot, idleAttack, true, false );
		}
		else if ( m_aGestureSlots[slot].m_iActivity == idleAttack )
			ResetGestureSlot( slot );
		if ( pSaw->m_bHitting )
		{
			if ( m_aGestureSlots[slot].m_iActivity != shoot )
				RestartGesture( slot, shoot, true, false );
		}
		else if ( m_aGestureSlots[slot].m_iActivity == shoot )
			ResetGestureSlot( slot );
	}
	else
	{
		if ( m_aGestureSlots[slot].m_iActivity == shoot )
			ResetGestureSlot( slot );
		m_bChainsawStartup = false;
	}
}

static TerrorOriginalActivity ChargerDownPoundActivity( CTerrorPlayer *pVictim )
{
	switch ( pVictim->GetSurvivorCharacter() )
	{
	case SurvivorCharacter_Producer: return TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_DOWN_PRODUCER;
	case SurvivorCharacter_Coach: return TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_DOWN_COACH;
	default: return TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_DOWN;
	}
}

// S342280 owns the attacker animation AND the actual victim 77/78 events.
Activity CTerrorPlayerAnimState::CalcChargerPummelActivity()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !m_bChargerPummel )
	{
		if ( !m_bChargerPound )
			return ACT_INVALID;
		if ( !pPlayer->m_pummelVictim.Get() )
		{
			m_bChargerPound = false;
			return ACT_INVALID;
		}
		const TerrorOriginalActivity current = GetOriginalMainActivity();
		const TerrorOriginalActivity down = ChargerDownPoundActivity(
			static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() ) );
		if ( current >= 537 && current <= 541 )
		{
			if ( !( pPlayer->GetCycle() > 0.99000001f ) )
				return m_eCurrentMainSequenceActivity;
			if ( !static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() )->IsIncapacitated() )
			{
				// At most three candidates; the ordered native choice/removal
				// and RNG calls do not require a heap allocation.
				CUtlVectorFixedGrowable< TerrorOriginalActivity, 3 > choices;
				choices.AddToTail( down );
				const Vector center = pPlayer->WorldSpaceCenter();
				const Vector top = pPlayer->GetAbsOrigin() + Vector( 0.0f, 0.0f, 158.0f );
				trace_t trace;
				UTIL_TraceLine( pPlayer->GetAbsOrigin(), top, 0x46004003u,
					pPlayer, COLLISION_GROUP_NONE, &trace );
				if ( trace.fraction < 1.0f && trace.fraction * 158.0f >= 148.0f &&
					trace.fraction * 158.0f <= 158.0f )
					choices.AddToTail( TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_UP );
				Vector forward;
				pPlayer->GetVectors( &forward, NULL, NULL );
				UTIL_TraceLine( center, center + forward * 62.0f, 0x46004003u,
					pPlayer, COLLISION_GROUP_NONE, &trace );
				if ( trace.fraction < 1.0f && trace.fraction * 62.0f >= 50.0f &&
					trace.fraction * 62.0f <= 62.0f )
					choices.AddToTail( TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_NORTH );
				RestartMainSequence();
				if ( choices.Count() > 1 && RandomInt( 1, 100 ) <= 50 )
					choices.FindAndRemove( current );
				return TerrorActivityFromOriginal( choices[RandomInt( 0, choices.Count() - 1 )] );
			}
		}
		RestartMainSequence();
		return TerrorActivityFromOriginal( down );
	}
	if ( GetOriginalMainActivity() == 535 && pPlayer->GetCycle() > 0.99000001f )
	{
		if ( !pPlayer->m_pummelVictim.Get() )
		{
			m_bChargerPummel = false;
			return ACT_INVALID;
		}
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUND_START );
		// S342280: re-read at the native post-event boundary, never cache across it.
		CTerrorPlayer *victim = static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() );
		if ( victim )
			victim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUNDED );
		RestartMainSequence();
		victim = static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() );
		return victim ? TerrorActivityFromOriginal( ChargerDownPoundActivity( victim ) ) : ACT_INVALID;
	}
	if ( GetOriginalMainActivity() != 535 )
		RestartMainSequence();
	if ( pPlayer->GetCycle() > 0.2f && !m_bChargerPummelVictimNotified && pPlayer->m_pummelVictim.Get() )
	{
		m_bChargerPummelVictimNotified = true;
		static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() )->DoTerrorAnimationEvent(
			TERROR_ANIMEVENT_CHARGER_PUMMELED );
	}
	return TerrorActivityFromOriginal( 535 );
}

Activity CTerrorPlayerAnimState::CalcChargerPummelVictimActivity()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( m_bChargerPummeled )
	{
		if ( pPlayer->m_pummelAttacker.Get() )
		{
			m_bChargerSlammedWall = false;
			if ( GetOriginalMainActivity() != 527 )
				RestartMainSequenceIfUnlocked();
			return TerrorActivityFromOriginal( 527 );
		}
		if ( !pPlayer->IsIncapacitated() )
		{
			if ( GetOriginalMainActivity() == 526 )
			{
				if ( pPlayer->GetCycle() > 0.99000001f )
				{
					m_bChargerPummeled = false;
					return ACT_IDLE;
				}
			}
			else
				RestartMainSequenceIfUnlocked();
			return TerrorActivityFromOriginal( 526 );
		}
		m_bChargerPummeled = false;
		return ACT_INVALID;
	}
	if ( !m_bChargerPounded )
		return ACT_INVALID;
	const int current = GetOriginalMainActivity();
	if ( !pPlayer->m_pummelAttacker.Get() )
	{
		if ( !pPlayer->IsIncapacitated() && !m_bJockeyRidden )
		{
			if ( GetOriginalMainActivity() != 526 )
			{
				RestartMainSequenceIfUnlocked();
				return TerrorActivityFromOriginal( 526 );
			}
			if ( !( pPlayer->GetCycle() > 0.99000001f ) )
				return TerrorActivityFromOriginal( 526 );
		}
		m_bChargerPounded = false;
		return ACT_INVALID;
	}
	CTerrorPlayer *pAttacker = static_cast< CTerrorPlayer * >( pPlayer->m_pummelAttacker.Get() );
	if ( !pAttacker->IsAlive() )
	{
		m_bChargerPummeled = false;
		m_bChargerPounded = false;
		m_bChargerCarried = false;
		return ACT_INVALID;
	}
	if ( pPlayer->IsIncapacitated() )
		return TerrorActivityFromOriginal( 533 );
	if ( pPlayer->GetCycle() > 0.99000001f || current < 531 || current > 534 )
	{
		RestartMainSequenceIfUnlocked();
		switch ( LookupTerrorOriginalActivity( pAttacker->GetTerrorAnimState()->GetCurrentMainActivity() ) )
		{
		case 537: return TerrorActivityFromOriginal( 531 );
		case 538: return TerrorActivityFromOriginal( 532 );
		case 539: case 540: case 541: break;
		default: Msg( "I don't know how to be a victim\n" ); break;
		}
		return TerrorActivityFromOriginal( 534 );
	}
	return TerrorActivityFromOriginal( current );
}

Activity CTerrorPlayerAnimState::CalcChargerImpactActivity()
{
	if ( !m_bChargerImpact )
		return ACT_INVALID;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( GetOriginalMainActivity() == 524 || GetOriginalMainActivity() == 525 )
	{
		const Vector &velocity = pPlayer->GetAbsVelocity();
		const float speed = velocity.Length();
		const float threshold = velocity.x == 0.0f && velocity.y == 0.0f ? 15.0f : 5.0f;
		if ( pPlayer->GetGroundEntity() || ( pPlayer->GetCycle() < 0.25f && speed < threshold ) )
		{
			RestartMainSequence();
			pPlayer->Knockdown( 3 );
			return TerrorActivityFromOriginal( 526 );
		}
	}
	if ( pPlayer->GetCycle() > 0.99000001f )
	{
		RestartMainSequence();
		switch ( GetOriginalMainActivity() )
		{
		case 524: return TerrorActivityFromOriginal( 525 );
		case 525:
			pPlayer->Knockdown( 3 );
			return TerrorActivityFromOriginal( 526 );
		case 526:
			m_bChargerImpact = false;
			return ACT_INVALID;
		default: break;
		}
	}
	const int current = GetOriginalMainActivity();
	if ( current >= 524 && current <= 526 )
		return m_eCurrentMainSequenceActivity;
	RestartMainSequence();
	return TerrorActivityFromOriginal( 524 );
}

Activity CTerrorPlayerAnimState::CalcSpecialMoveActivity()
{
	// S340460 prioritizes throw368, then climb372, then Tank action376.
	int selected = -1;
	int *pCompletedData;
	if ( m_nThrowAnimation != -1 )
	{
		selected = ( m_nThrowAnimation != 0 ) + 761;
		pCompletedData = &m_nThrowAnimation;
	}
	else if ( m_nClimbAnimation != -1 )
	{
		switch ( m_nClimbAnimation )
		{
		case 0: selected = 719; break;
		case 1: selected = 720; break;
		case 2: selected = 722; break;
		case 3: selected = 724; break;
		case 4: selected = 729; break;
		case 5: selected = 731; break;
		case 6: selected = 734; break;
		case 7: selected = 736; break;
		default: break;
		}
		pCompletedData = &m_nClimbAnimation;
	}
	else if ( m_nTankSpecialAnimation != -1 )
	{
		switch ( m_nTankSpecialAnimation )
		{
		case 0: selected = 795; break;
		case 1: selected = 794; break;
		case 2: selected = 792; break;
		case 3: selected = 793; break;
		default: break;
		}
		pCompletedData = &m_nTankSpecialAnimation;
	}
	else
		return ACT_INVALID;
	if ( GetOriginalMainActivity() != selected )
	{
		RestartMainSequence();
		return TerrorActivityFromOriginal( selected );
	}
	if ( GetTerrorPlayer()->GetCycle() <= 0.99000001f )
		return TerrorActivityFromOriginal( selected );
	*pCompletedData = -1;
	return ACT_INVALID;
}

extern ConVar survivor_ledge_grab_health;

Activity CTerrorPlayerAnimState::CalcIncapacitatedActivity()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer->IsIncapacitated() )
		return ACT_INVALID;
	if ( pPlayer->m_isHangingFromLedge )
	{
		if ( pPlayer->m_reviveOwner.Get() )
		{
			if ( GetOriginalMainActivity() != 710 && GetOriginalMainActivity() != 711 )
			{
				RestartMainSequence();
				return TerrorActivityFromOriginal( pPlayer->m_Local.m_bDucked ? 711 : 710 );
			}
			return m_eCurrentMainSequenceActivity;
		}
		const int current = GetOriginalMainActivity();
		if ( current < 712 || current > 715 )
			RestartMainSequence();
		else if ( current != 712 || pPlayer->GetCycle() > 0.99000001f )
		{
			const float percent = ( (float)pPlayer->GetHealth() / survivor_ledge_grab_health.GetFloat() ) * 100.0f;
			if ( percent >= 33.299999f )
			{
				if ( percent >= 66.599998f )
					return TerrorActivityFromOriginal( 713 );
				if ( GetOriginalMainActivity() != 714 )
				{
					pPlayer->GetMusic()->OnLedgeHangOneHand();
					pPlayer->SpeakIfAllowed( "PlayerLedgeHangMiddle", 1, NULL, NULL, 0, NULL );
				}
				return TerrorActivityFromOriginal( 714 );
			}
			if ( GetOriginalMainActivity() != 715 )
			{
				pPlayer->GetMusic()->OnLedgeHangFingers();
				pPlayer->SpeakIfAllowed( "PlayerLedgeHangEnd", 1, NULL, NULL, 0, NULL );
			}
			return TerrorActivityFromOriginal( 715 );
		}
		return TerrorActivityFromOriginal( 712 );
	}
	const int current = GetOriginalMainActivity();
	if ( current == 773 )
	{
		if ( pPlayer->GetCycle() <= 0.99000001f )
			return TerrorActivityFromOriginal( 773 );
		RestartMainSequence();
	}
	else if ( current != 534 && current != 533 )
	{
		if ( !pPlayer->m_reviveOwner.Get() )
		{
			if ( current == 700 || current == 701 )
			{
				if ( pPlayer->GetCycle() > 0.99000001f )
					RestartMainSequence();
				return m_eCurrentMainSequenceActivity;
			}
			if ( current == 20 )
			{
				if ( !( pPlayer->GetCycle() > 0.99000001f ) )
					return ACT_DIESIMPLE;
				RestartMainSequence();
			}
			else
			{
				RestartMainSequence();
				return ACT_DIESIMPLE;
			}
		}
		else
		{
			if ( GetOriginalMainActivity() != ( pPlayer->m_Local.m_bDucked ? 698 : 697 ) )
				RestartMainSequence();
			return TerrorActivityFromOriginal( pPlayer->m_Local.m_bDucked ? 698 : 697 );
		}
	}
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
	return TerrorActivityFromOriginal( pWeapon && pWeapon->IsDualWielding() ? 701 : 700 );
}
#endif

// S33D910/C27E7B0: real airborne history, distinct from event7's jump latch.
bool CTerrorPlayerAnimState::HandleJumping( Activity &idealActivity )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	const bool bWasAirborne = m_bWasAirborne;
	const bool bAirborne = !( pPlayer->GetFlags() & FL_ONGROUND );
	const TerrorOriginalActivity current = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	if ( current >= TERROR_ORIGINAL_ACT_TERROR_HIT_BY_TANKPUNCH &&
		current <= TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW )
		return false;
	if ( bAirborne )
	{
		if ( !bWasAirborne )
		{
			m_flAirborneStartTime = gpGlobals->curtime;
			CBaseAbility *pAbility = pPlayer->GetCustomAbility();
			if ( pPlayer->GetTeamNumber() == TEAM_INFECTED && pAbility &&
				FClassnameIs( pAbility, "ability_lunge" ) && pAbility->IsActive() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
					TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_HUNTER_LUNGE ), true, false );
		}
		if ( m_bJumping )
		{
			Vector velocity;
			GetOuterAbsVelocity( velocity );
			if ( velocity.z < 0.0f )
				m_bJumping = false;
		}
	}
	if ( bWasAirborne && !bAirborne && gpGlobals->curtime > m_flAirborneStartTime + 0.2f )
	{
		RestartGesture( GESTURE_SLOT_JUMP,
			TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_JUMP_LANDING ), true, false );
		m_bJumping = false;
	}
	m_bWasAirborne = bAirborne;
	if ( gpGlobals->curtime > m_flAirborneStartTime + 1.0f || !bAirborne )
		return false;
	idealActivity = ACT_JUMP;
	return true;
}

// S33D8C0/C27DBB0 consume updated airborne history, not the jump-event latch.
bool CTerrorPlayerAnimState::HandleDucking( Activity &idealActivity )
{
	if ( m_bWasAirborne || !GetTerrorPlayer()->m_Local.m_bDucked )
		return false;
	idealActivity = GetOuterXYSpeed() >= 0.5f ? ACT_RUN_CROUCH : ACT_CROUCHIDLE;
	return true;
}

bool CTerrorPlayerAnimState::HandleMoving( Activity &idealActivity )
{
#ifdef CLIENT_DLL
	// C280B60 calculates only gesture side effects; the received sequence
	// remains authoritative. Preserve ordered35/40 queries and old layer cycle.
	const Activity movement = UntranslateActivity( m_eCurrentMainSequenceActivity );
	CWeaponCSBase *pWeapon = static_cast< CWeaponCSBase * >( GetTerrorPlayer()->GetActiveWeapon() );
	if ( pWeapon )
	{
		static const TerrorPlayerAnimEvent events[] =
		{
			TERROR_ANIMEVENT_MELEE_SHOVE,
			TERROR_ANIMEVENT_FIRE_PRIMARY
		};
		for ( int i = 0; i < ARRAYSIZE( events ); ++i )
		{
			const Activity idle = pWeapon->GetPlayerAttackActivity( events[i], ACT_IDLE );
			const Activity moving = pWeapon->GetPlayerAttackActivity( events[i], ACT_RUN );
			const Activity desired = pWeapon->GetPlayerAttackActivity( events[i], movement );
			CAnimationLayer *pLayer = GetGestureSlotLayer( GESTURE_SLOT_ATTACK_AND_RELOAD );
			if ( ( IsGestureSlotPlaying( GESTURE_SLOT_ATTACK_AND_RELOAD, idle ) && desired == moving ) ||
				( IsGestureSlotPlaying( GESTURE_SLOT_ATTACK_AND_RELOAD, moving ) && desired == idle ) )
			{
				const float cycle = pLayer->m_flCycle;
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, desired, true, false );
				pLayer->m_flCycle = cycle;
			}
		}
	}
	return false;
#else
	// S33D810: a ridden survivor cannot choose walk from IN_SPEED.
	if ( GetOuterXYSpeed() >= 0.5f )
		idealActivity = !GetTerrorPlayer()->m_jockeyAttacker.Get() &&
			( GetTerrorPlayer()->m_nButtons & IN_SPEED ) ? ACT_WALK : ACT_RUN;
	else
		idealActivity = ACT_IDLE;
	return true;
#endif
}

bool CTerrorPlayerAnimState::HandleSwimming( Activity &idealActivity )
{
	// S33B990/C27DAD0 are genuine Terror swimming nullsubs.
	return false;
}

Activity CTerrorPlayerAnimState::TranslateActivity( Activity activity )
{
	// S33DA40/C27E8F0 transform locomotion before the weapon's actual
	// translator. Action/gesture activities are not blindly remapped.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer )
		return activity;
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
	const int original = LookupTerrorOriginalActivity( activity );
	if ( !pWeapon )
	{
		const int team = pPlayer->GetTeamNumber();
		return ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) && original == 1 ?
			TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_IDLE_CALM_PISTOL ) : activity;
	}
#ifdef CLIENT_DLL
	const float health = static_cast< float >( pPlayer->GetHealth() );
	const float totalHealth = pPlayer->GetHealthBuffer() + health;
#else
	const int health = pPlayer->GetHealth();
	const float totalHealth = static_cast< float >(
		static_cast< double >( health ) + pPlayer->GetHealthBuffer() );
#endif
	// GetHealthBuffer can emit a real player_hurt callback before this read.
	const int team = pPlayer->GetTeamNumber();
	const bool bSurvivor = team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
	int desired = original;
	extern ConVar survivor_limp_health;
	if ( bSurvivor )
	{
		if ( pPlayer->IsGoingToDie() )
		{
			if ( original == 1 ) desired = 508;
			else if ( original == 6 || original == 10 ) desired = 510;
		}
		else if ( survivor_limp_health.GetFloat() > totalHealth )
		{
			if ( original == 1 ) desired = 508;
			else if ( original == 6 ) desired = 510;
			else if ( original == 10 ) desired = 511;
		}
		else if ( pPlayer->m_isCalm )
		{
			if ( original == 1 ) desired = 512;
			else if ( original == 6 ) desired = 513;
			else if ( original == 10 ) desired = 514;
		}
	}
	if ( original == 10 && pPlayer->GetTeamNumber() == TEAM_INFECTED )
	{
#ifdef CLIENT_DLL
		const int zombieClass = pPlayer->m_zombieClass;
#else
		const int zombieClass = pPlayer->GetZombieClass();
#endif
		if ( zombieClass == 6 && pPlayer->GetCustomAbility() && pPlayer->GetCustomAbility()->IsActive() )
			return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_CHARGER_CHARGE );
	}
	switch ( desired )
	{
	case 1: case 6: case 10: case 12: case 29: case 45:
	case 508: case 510: case 511: case 512: case 513: case 514: case 702:
		return pWeapon->TranslatePlayerActivity( TerrorActivityFromOriginal( desired ) );
	default:
		return activity;
	}
}

void CTerrorPlayerAnimState::RestartMainSequence()
{
#ifndef CLIENT_DLL
	GetTerrorPlayer()->m_fServerAnimStartTime = gpGlobals->curtime;
	GetTerrorPlayer()->ResetSequenceInfo();
#endif
	BaseClass::RestartMainSequence();
}

#ifdef CLIENT_DLL
// C280920 maintains the received main sequence; it does not select a new one.
void CTerrorPlayerAnimState::UpdateReceivedMainSequence()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	const TerrorOriginalActivity activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	if ( pPlayer->GetCycle() > 0.99000001f )
	{
		if ( activity != TERROR_ORIGINAL_ACT_CROUCHIDLE &&
			activity != TERROR_ORIGINAL_ACT_TERROR_TANKPUNCH_LAND &&
			activity != TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW &&
			activity != TERROR_ORIGINAL_ACT_TERROR_CHARGER_PUMMELED &&
			activity != TERROR_ORIGINAL_ACT_TERROR_POUNCED_TO_STAND &&
			!( activity >= TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD &&
				activity <= TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_MELEE ) )
		{
			RestartMainSequence();
		}
	}

	if ( activity >= TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_NORTH &&
		activity <= TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUND_DOWN )
	{
		CTerrorPlayer *pPummelVictim = static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() );
		if ( pPummelVictim )
		{
			const TerrorOriginalActivity victimActivity = LookupTerrorOriginalActivity( pPummelVictim->GetTerrorActivity() );
			if ( victimActivity >= TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUNDED_NORTH &&
				victimActivity <= TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUNDED_DOWN )
			{
				// C280920: keep the native second handle resolution, skipping only NULL use.
				pPummelVictim = static_cast< CTerrorPlayer * >( pPlayer->m_pummelVictim.Get() );
				if ( pPummelVictim )
					pPummelVictim->SetCycle( pPlayer->GetCycle() );
			}
		}
	}

	if ( CTerrorPlayer *pCarryVictim = static_cast< CTerrorPlayer * >( pPlayer->m_carryVictim.Get() ) )
		pCarryVictim->SetCycle( pPlayer->GetCycle() );

	if ( activity == TERROR_ORIGINAL_ACT_TERROR_JOCKEY_RIDE )
	{
		CTerrorPlayer *pJockeyVictim = static_cast< CTerrorPlayer * >( pPlayer->m_jockeyVictim.Get() );
		if ( pJockeyVictim && LookupTerrorOriginalActivity(
			pJockeyVictim->GetTerrorActivity() ) == TERROR_ORIGINAL_ACT_TERROR_JOCKEY_RIDDEN )
		{
			pJockeyVictim = static_cast< CTerrorPlayer * >( pPlayer->m_jockeyVictim.Get() );
			if ( pJockeyVictim )
				pPlayer->SetCycle( pJockeyVictim->GetCycle() );
		}
	}
	// C280920 returns the base activity calculation for its actual local
	// jump/duck/gesture side effects; C146320 then caches the received activity.
	BaseClass::CalcMainActivity();
}
#endif

void CTerrorPlayerAnimState::ComputeSequences( CStudioHdr *pStudioHdr )
{
#ifdef CLIENT_DLL
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	UpdateReceivedMainSequence();
	const Activity selected = pPlayer->GetSequenceActivity( pPlayer->GetSequence() );
	if ( selected != m_eCurrentMainSequenceActivity )
	{
		const Activity oldBase = UntranslateActivity( m_eCurrentMainSequenceActivity );
		const Activity newBase = UntranslateActivity( selected );
		if ( ( oldBase != ACT_WALK && oldBase != ACT_RUN ) ||
			 ( newBase != ACT_WALK && newBase != ACT_RUN ) )
			RestartMainSequence();
		m_eCurrentMainSequenceActivity = selected;
	}
	if ( pPlayer->m_fLastServerAnimUpdateTime != pPlayer->m_fServerAnimStartTime )
	{
		pPlayer->m_fLastServerAnimUpdateTime = pPlayer->m_fServerAnimStartTime;
		RestartMainSequence();
	}
	UpdateInterpolators();
	ComputeGestureSequence( pStudioHdr );
#else
	BaseClass::ComputeSequences( pStudioHdr );
#endif
}

double CTerrorPlayerAnimState::GetCurrentMaxGroundSpeed()
{
	// S33C040/C27E270 reject INVALID before the virtual inverse policy.
	if ( m_eCurrentMainSequenceActivity == ACT_INVALID )
		return 0.0;
	const TerrorOriginalActivity activity = LookupTerrorOriginalActivity(
		UntranslateActivity( m_eCurrentMainSequenceActivity ) );
	switch ( activity )
	{
	case TERROR_ORIGINAL_ACT_IDLE:
	case TERROR_ORIGINAL_ACT_WALK:
		return GetTerrorPlayer()->GetWalkTopSpeed();
	case TERROR_ORIGINAL_ACT_RUN_CROUCH:
		return GetTerrorPlayer()->GetCrouchTopSpeed();
	case TERROR_ORIGINAL_ACT_RUN:
	case TERROR_ORIGINAL_ACT_JUMP:
	case TERROR_ORIGINAL_ACT_RUN_RIFLE:
		return GetTerrorPlayer()->GetPlayerMaxSpeed();
	default:
		return 0.0f;
	}
}

bool CTerrorPlayerAnimState::ShouldFaceAim()
{
	// S340E40/C2815D0: raw main and raw slot0 domains, never the inverse map.
	const int gesture = LookupTerrorOriginalActivity( m_aGestureSlots[GESTURE_SLOT_ATTACK_AND_RELOAD].m_iActivity );
	switch ( gesture )
	{
	case 613: case 762: case 790:
	case 952: case 953: case 954: case 955: case 956: case 957: case 959: case 960:
		return true;
	}
	const int activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	switch ( activity )
	{
	case 719: case 720: case 722: case 724: case 729: case 731: case 734: case 736:
	case 739: case 745: case 747: case 748: case 750: case 753: case 761: case 762:
		return true;
	}
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( pPlayer->m_positionEntity.Get() )
		return true;
#ifdef CLIENT_DLL
	const bool bHanging = activity >= 710 && activity <= 715;
#else
	const bool bHanging = pPlayer->IsHangingFromLedge();
#endif
	if ( pPlayer->IsIncapacitated() && !bHanging )
		return false;
	if ( IsGestureSlotActive( GESTURE_SLOT_ATTACK_AND_RELOAD ) && pPlayer->GetActiveWeapon() &&
		static_cast< CWeaponCSBase * >( pPlayer->GetActiveWeapon() )->GetWeaponID() == TERROR_WEAPON_MELEE )
		return true;
	if ( bHanging || IsLedgeClimbAnimation() || pPlayer->GetMoveType() == MOVETYPE_LADDER )
		return true;
#ifdef CLIENT_DLL
	if ( activity == 773 || activity == 775 || activity == 779 || activity == 783 || activity == 958 ||
		( activity >= 770 && activity <= 771 ) || activity == 535 || activity == 527 ||
		( activity >= 537 && activity <= 541 ) || pPlayer->IsUseActionAnimationBlocked() ||
		( activity >= 621 && activity <= 636 ) || ( activity >= 765 && activity <= 768 ) ||
		( activity >= 750 && ( activity <= 753 || activity == 755 ) ) ||
		( activity >= 747 && ( activity <= 748 || activity == 764 ) ) ||
		activity == 719 || activity == 720 || activity == 722 || activity == 724 ||
		activity == 729 || activity == 731 || activity == 734 || activity == 736 )
		return true;
#else
	if ( m_bTongueVictim || m_bPounced || m_bChargerPummel || m_bChargerPummeled ||
		m_bChargerPound || m_bChargerPounded || m_nStaggerDirection != -1 ||
		m_bTongueAttack || m_bHunterPounce || m_nClimbAnimation != -1 )
		return true;
#endif
	if ( pPlayer->m_carryAttacker.Get() || pPlayer->m_jockeyAttacker.Get() ||
		pPlayer->m_jockeyVictim.Get() || pPlayer->IsPerformingOtherUseAction() || pPlayer->IsUseActionTarget() )
		return true;
	if ( pPlayer->m_reviveTarget.Get() || pPlayer->m_reviveOwner.Get() || TerrorGameRules()->IsInIntro() )
		return true;
	return false;
}

double CTerrorPlayerAnimState::GetFeetYawRateScale()
{
	// S340D90/C2814D0 preserve server latches versus client raw activity policy.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
#ifdef CLIENT_DLL
	const int activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	if ( ( activity >= 710 && activity <= 715 ) || pPlayer->GetMoveType() == MOVETYPE_LADDER ||
		( activity >= 750 && ( activity <= 753 || activity == 755 ) ) ||
		( activity >= 747 && ( activity <= 748 || activity == 764 ) ) ||
		activity == 527 || pPlayer->IsUseActionAnimationBlocked() )
		return 5.0;
#else
	if ( pPlayer->IsHangingFromLedge() || pPlayer->GetMoveType() == MOVETYPE_LADDER ||
		m_bTongueAttack || m_bHunterPounce || m_bChargerPummeled || m_bChargerPounded )
		return 5.0;
#endif
	const int gesture = LookupTerrorOriginalActivity( m_aGestureSlots[GESTURE_SLOT_ATTACK_AND_RELOAD].m_iActivity );
	if ( gesture == 1216 || gesture == 1233 || pPlayer->m_carryAttacker.Get() ||
		CTerrorPlayer::IsClimbingActivity( m_eCurrentMainSequenceActivity ) )
		return 5.0;
	if ( ( pPlayer->GetTeamNumber() == TEAM_INFECTED && pPlayer->GetZombieClass() == 8 ) || GetOuterXYSpeed() > 0.5f )
		return 2.0;
	return 1.0;
}

bool CTerrorPlayerAnimState::ShouldLockEyeAngles()
{
	// S33C7C0/C27EB30 lock input BEFORE sequence selection, using prior state.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
#ifdef CLIENT_DLL
	const int activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	if ( activity >= 747 && ( activity <= 748 || activity == 764 ) && !pPlayer->m_isAttemptingToPounce )
		return true;
	return ( activity >= 621 && activity <= 636 ) || ( activity >= 765 && activity <= 768 ) ||
		activity == 535 || activity == 527 || ( activity >= 537 && activity <= 541 ) ||
		LookupTerrorOriginalActivity( pPlayer->GetTerrorAnimState()->GetCurrentMainActivity() ) == 529 ||
		pPlayer->IsUseActionAnimationBlocked() || activity == 530;
#else
	return ( m_bHunterPounce && !pPlayer->m_isAttemptingToPounce ) || m_nStaggerDirection != -1 ||
		m_bChargerPummel || m_bChargerPummeled || m_bChargerPound ||
		LookupTerrorOriginalActivity( pPlayer->GetTerrorAnimState()->GetCurrentMainActivity() ) == 529 ||
		m_bChargerPounded || m_bChargerSlammedGround;
#endif
}

void CTerrorPlayerAnimState::EstimateFeetYaw()
{
	// S341390/C281AB0 sample velocity before the incapacitation query.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	Vector velocity;
	GetOuterAbsVelocity( velocity );
	const float flSpeed = sqrtf( velocity.y * velocity.y + velocity.x * velocity.x );
	if ( pPlayer->IsIncapacitated() )
		m_flCurrentFeetYaw = m_flGoalFeetYaw;
	if ( !m_bCurrentFeetYawInitialized )
	{
		m_flCurrentFeetYaw = m_flGoalFeetYaw = m_flEyeYaw;
		m_bCurrentFeetYawInitialized = true;
		m_flLastAimTurnTime = 0.0f;
	}
#ifdef CLIENT_DLL
	C_BaseMountedWeapon *pMountedWeapon = pPlayer->GetMountedWeapon();
#else
	CBaseMountedWeapon *pMountedWeapon = pPlayer->GetMountedWeapon();
#endif
	if ( pMountedWeapon )
		m_flGoalFeetYaw = pMountedWeapon->GetAbsAngles().y;
	else
	{
		if ( flSpeed > 0.5f && flSpeed > static_cast< double >( GetCurrentMaxGroundSpeed() ) * 0.89999998f )
		{
			velocity.z = 0.0f;
			QAngle movementAngles;
			VectorAngles( velocity, movementAngles );
			const float flMovementYaw = movementAngles.y;
			const float flDelta = AngleNormalize( flMovementYaw - m_flEyeYaw );
			m_flGoalFeetYaw = flMovementYaw;
			if ( flDelta > 100.0f || flDelta < -100.0f )
			{
				m_flGoalFeetYaw += 180.0f;
				if ( m_flGoalFeetYaw >= 360.0f )
					m_flGoalFeetYaw -= 360.0f;
			}
		}
		if ( ShouldFaceAim() || ( gpGlobals->curtime - m_flLastAimTurnTime > mp_feetmaxyawrate.GetFloat() &&
			!pPlayer->IsIncapacitated() ) )
			m_flGoalFeetYaw = m_flEyeYaw;
	}

	const float flDelta = AngleNormalize( m_flGoalFeetYaw - m_flEyeYaw );
	const int team = pPlayer->GetTeamNumber();
	float flLimit = 45.0f;
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		flLimit = pPlayer->IsIncapacitated() ? 360.0f : 90.0f;
	if ( fabs( flDelta ) > flLimit )
		m_flGoalFeetYaw = m_flEyeYaw + ( flDelta <= 0.0f ? -flLimit : flLimit );
	if ( pPlayer->m_positionEntity.Get() )
		m_flCurrentFeetYaw = m_flGoalFeetYaw = m_flEyeYaw;
	if ( m_flCurrentFeetYaw != m_flGoalFeetYaw )
	{
		const float flRateCvar = mp_feetyawrate.GetFloat();
		const float flTurnRate = GetFeetYawRateScale() * flRateCvar;
		const float flCapCvar = mp_feetyawrate_max.GetFloat();
		const float flTurnCap = GetFeetYawRateScale() * flCapCvar;
		ConvergeSurvivorFeetYaw( m_flGoalFeetYaw, m_flEyeYaw, flTurnRate, flTurnCap,
			gpGlobals->frametime, m_flCurrentFeetYaw );
	}
}

void CTerrorPlayerAnimState::ComputePoseParam_MoveYaw( CStudioHdr *pStudioHdr )
{
	// S341730/C281E50: the feet helper takes its own, later velocity sample.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( !pPlayer )
		return;
	Vector velocity;
	GetOuterAbsVelocity( velocity );
	velocity.z = 0.0f;
	const float flSpeed = sqrtf( velocity.y * velocity.y + velocity.x * velocity.x );
	EstimateFeetYaw();
	m_angRender.Init( 0.0f, m_flCurrentFeetYaw, 0.0f );

#ifdef CLIENT_DLL
	const bool bJockeyRide = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity ) == 798;
#else
	const bool bJockeyRide = m_bJockeyRide;
#endif
	if ( bJockeyRide )
	{
		Vector desire = pPlayer->m_jockeyDesiredMove;
		QAngle desiredAngles;
		VectorAngles( desire, desiredAngles );
		const double flRelativeYaw = AngleNormalize( m_flCurrentFeetYaw - desiredAngles.y );
		float flDesiredX, flDesiredY;
		if ( pPlayer->m_jockeyRotatedDesire )
		{
			const double flRadians = flRelativeYaw * 0.017453292f;
			const float flDesiredSpeed = sqrtf( ( desire.z * desire.z + desire.y * desire.y ) + desire.x * desire.x );
			flDesiredX = cos( flRadians ) * flDesiredSpeed;
			flDesiredY = flDesiredSpeed * sin( flRadians );
		}
		else
		{
			flDesiredX = desire.x;
			flDesiredY = desire.y;
		}
		const float flDeltaX = flDesiredX - m_flJockeyMoveX;
		const float flDeltaY = flDesiredY - m_flJockeyMoveY;
		float flRate = gpGlobals->frametime * 10.0f;
		if ( flRate > 1.0f )
			flRate = 1.0f;
#ifdef CLIENT_DLL
		m_flJockeyMoveX += flDeltaX * flRate;
		m_flJockeyMoveY += flDeltaY * flRate;
#else
		m_flJockeyMoveY += flDeltaY * flRate;
		m_flJockeyMoveX += flDeltaX * flRate;
#endif
		pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iMoveX, m_flJockeyMoveX );
		pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iMoveY, m_flJockeyMoveY );
		CTerrorPlayer *pVictim = static_cast< CTerrorPlayer * >( pPlayer->m_jockeyVictim.Get() );
		if ( pVictim )
		{
			CStudioHdr *pVictimHdr = pVictim->GetModelPtr();
			pVictim->SetPoseParameter( pVictimHdr,
				pVictim->GetTerrorAnimState()->m_nJockeyXPoseParameter, m_flJockeyMoveX );
			pVictim->SetPoseParameter( pVictimHdr,
				pVictim->GetTerrorAnimState()->m_nJockeyYPoseParameter, m_flJockeyMoveY );
		}
		return;
	}

	QAngle movementAngles;
	VectorAngles( velocity, movementAngles );
	const float flYaw = AngleNormalize( m_flCurrentFeetYaw - movementAngles.y );
	float flMoveX = 0.0f;
	float flMoveY = 0.0f;
	if ( flSpeed > 0.5f && GetCurrentMaxGroundSpeed() > 0.0 )
	{
		// The second virtual query is intentional. Only the bound comparison
		// uses the float spill; an in-range ratio remains wide through sin/cos.
		double flScale = static_cast< double >( flSpeed ) / GetCurrentMaxGroundSpeed();
		const float flStoredScale = flScale;
		if ( flScale > 1.0 )
			flScale = 1.0;
		else if ( flStoredScale < 0.0099999998f )
			flScale = 0.0099999998f;
		const double flRadians = static_cast< double >( flYaw ) * 0.017453292f;
		flMoveX = cos( flRadians ) * flScale;
		flMoveY = flScale * sin( flRadians );
	}
	pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iMoveX, flMoveX );
	pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iMoveY, flMoveY );
	m_DebugAnimData.m_vecMoveYaw.Init( flMoveX, flMoveY );
}

void CTerrorPlayerAnimState::ComputePoseParam_AimPitch( CStudioHdr *pStudioHdr )
{
	// S:0x33BB20: body first, head follows the remaining pitch; no sign flip.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	float flHead, flBody;
	ReadTerrorAimPoseParameters( pPlayer, m_nHeadPitchPoseParameter, m_PoseParameterData.m_iAimPitch, flHead, flBody );
	const float flNewBody = flBody + SurvivorAimStep( m_flEyePitch - flBody, 12.0f, m_flAimUpdateMultiplier );
	const float flNewHead = flHead + SurvivorAimStep( m_flEyePitch - flNewBody - flHead, 12.0f, m_flAimUpdateMultiplier );
	pPlayer->SetPoseParameter( pStudioHdr, m_nHeadPitchPoseParameter, flNewHead );
	pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iAimPitch, flNewBody );
}

void CTerrorPlayerAnimState::UpdateEyeYawFromAction()
{
	// S3408B0/C280C60: ordered physical/partner-facing overrides precede poses.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	if ( TerrorGameRules()->IsInIntro() )
	{
		m_flEyeYaw = pPlayer->GetAbsAngles().y;
		return;
	}
	Vector direction;
#ifdef CLIENT_DLL
	const int activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	const bool bLedgeFacing = ( activity >= 710 && activity <= 715 ) ||
		activity == 719 || activity == 720 || activity == 722 || activity == 724 ||
		activity == 729 || activity == 731 || activity == 734 || activity == 736;
#else
	const bool bLedgeFacing = pPlayer->IsHangingFromLedge() || m_nClimbAnimation != -1;
#endif
	if ( bLedgeFacing )
	{
		const Vector &hangNormal = pPlayer->m_hangNormal;
		direction = -hangNormal;
	}
	else if ( pPlayer->GetMoveType() == MOVETYPE_LADDER )
		direction = -pPlayer->GetLadderNormal();
	else if ( pPlayer->m_tongueVictim.Get() )
	{
		const Vector &origin = pPlayer->GetAbsOrigin();
		direction = pPlayer->m_tongueVictim.Get()->GetAbsOrigin() - origin;
	}
	else if ( pPlayer->m_tongueOwner.Get() )
	{
		const Vector &origin = pPlayer->m_tongueOwner.Get()->GetAbsOrigin();
		direction = pPlayer->GetAbsOrigin() - origin;
	}
	else
	{
#ifdef CLIENT_DLL
		const bool bJockeyRide = activity == 798;
		const bool bPounced = activity >= 770 && activity <= 771;
		const bool bPummeled = activity == 527 || pPlayer->IsUseActionAnimationBlocked();
#else
		const bool bJockeyRide = m_bJockeyRide;
		const bool bPounced = m_bPounced;
		const bool bPummeled = m_bChargerPummeled || m_bChargerPounded;
#endif
		if ( bJockeyRide && pPlayer->m_jockeyVictim.Get() )
		{
			CTerrorPlayer *pVictim = static_cast< CTerrorPlayer * >( pPlayer->m_jockeyVictim.Get() );
			m_flEyeYaw = AngleNormalize( pVictim->GetTerrorAnimState()->m_flEyeYaw );
			return;
		}
		CTerrorPlayer *pFacingPlayer = NULL;
		if ( bPounced && pPlayer->m_pounceAttacker.Get() )
			pFacingPlayer = static_cast< CTerrorPlayer * >( pPlayer->m_pounceAttacker.Get() );
		else if ( bPummeled && pPlayer->m_pummelAttacker.Get() )
			pFacingPlayer = static_cast< CTerrorPlayer * >( pPlayer->m_pummelAttacker.Get() );
		else if ( pPlayer->m_carryAttacker.Get() )
			pFacingPlayer = static_cast< CTerrorPlayer * >( pPlayer->m_carryAttacker.Get() );
		if ( pFacingPlayer )
		{
			// Native +128 returns the other state's EYE yaw228, not render yaw240.
			const float flOppositeYaw = static_cast< double >( pFacingPlayer->GetTerrorAnimState()->m_flEyeYaw ) + 180.0;
			m_flEyeYaw = AngleNormalize( flOppositeYaw );
			return;
		}
		if ( pPlayer->IsPerformingOtherUseAction() )
		{
			CBaseEntity *pTarget = pPlayer->m_useActionTarget.Get();
			if ( !pTarget )
				return;
			const Vector &origin = pPlayer->GetAbsOrigin();
			direction = pTarget->GetAbsOrigin() - origin;
		}
		else if ( pPlayer->m_reviveTarget.Get() )
		{
			const Vector &origin = pPlayer->GetAbsOrigin();
			direction = pPlayer->m_reviveTarget.Get()->GetAbsOrigin() - origin;
		}
		else if ( pPlayer->m_reviveOwner.Get() )
		{
			const Vector &origin = pPlayer->GetAbsOrigin();
			direction = pPlayer->m_reviveOwner.Get()->GetAbsOrigin() - origin;
		}
		else
			return;
	}
	QAngle angles;
	VectorAngles( direction, angles );
	m_flEyeYaw = angles.y;
}

void CTerrorPlayerAnimState::ComputePoseParam_AimYaw( CStudioHdr *pStudioHdr )
{
	// S3408B0/C280C60 use partner/normal overrides, then body and head gains.
	UpdateEyeYawFromAction();
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	float flHead, flBody;
	ReadTerrorAimPoseParameters( pPlayer, m_nHeadYawPoseParameter, m_PoseParameterData.m_iAimYaw, flHead, flBody );
	float flDesired = AngleNormalize( m_flEyeYaw - m_flCurrentFeetYaw );
	if ( !pPlayer->m_usingMountedWeapon )
	{
		if ( flDesired > 90.0f )
			flDesired = 180.0f - flDesired;
		else if ( flDesired < -90.0f )
			flDesired = -180.0f - flDesired;
	}
	const float flRate = GetOuterXYSpeed() <= 0.5f ? 3.0f : 5.0f;
	float flBodyStep = 2.0f * ( flDesired - flBody ) * m_flAimUpdateMultiplier * flRate * gpGlobals->frametime;
	if ( fabs( flBodyStep ) > fabs( flDesired - flBody ) )
		flBodyStep = flDesired - flBody;
	const float flNewBody = flBody + flBodyStep;
	const float flNewHead = flHead + SurvivorAimStep( flDesired - flNewBody - flHead, 20.0f, m_flAimUpdateMultiplier );
	pPlayer->SetPoseParameter( pStudioHdr, m_nHeadYawPoseParameter, flNewHead );
	pPlayer->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iAimYaw, flNewBody );
}

double CTerrorPlayerAnimState::GetLadderDismountPose()
{
	// S33C500/C2811C0 scan one-unit slices, including the final low slice.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	const Vector direction = -pPlayer->GetLastLadderNormal();
	Vector start = pPlayer->GetAbsOrigin();
	Vector end = start + direction * 64.0f;
	Vector mins = pPlayer->GetPlayerMins();
	Vector maxs = pPlayer->GetPlayerMaxs();
#ifdef CLIENT_DLL
	// The native client shrinks X/Y; the server deliberately does not.
	mins.x += 1.0f;
	mins.y += 1.0f;
	maxs.x -= 1.0f;
	maxs.y -= 1.0f;
#endif
	const float flOriginZ = start.z;
	const float flTopZ = maxs.z + start.z - 1.0f;
	maxs.z = 1.0f;
	float flScanZ = flTopZ;
	while ( flScanZ >= flOriginZ )
	{
		flScanZ -= 1.0f;
		start.z = end.z = flScanZ;
		CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
		trace_t trace;
		UTIL_TraceHull( start, end, mins, maxs, pPlayer->PlayerSolidMask( false ) & 0xFFFCFFFFu, &filter, &trace );
		if ( trace.fraction < 1.0f )
		{
			if ( trace.contents & CONTENTS_LADDER )
				return ( flTopZ - flScanZ ) / ( flTopZ - flOriginZ );
			if ( physprops )
			{
				surfacedata_t *pSurface = physprops->GetSurfaceData( trace.surface.surfaceProps );
				if ( pSurface && pSurface->game.climbable )
					return ( flTopZ - flScanZ ) / ( flTopZ - flOriginZ );
			}
		}
	}
	return 0.0;
}

// S341AC0/C2822D0 extract authored movement after the single sequence/pose pass.
void CTerrorPlayerAnimState::UpdateAnimationMovement( CStudioHdr *pStudioHdr )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	const Activity activity = m_eCurrentMainSequenceActivity;
	const float flCycle = pPlayer->GetCycle();
	const int nSequence = pPlayer->GetSequence();
	if ( ( CTerrorPlayer::IsAttackBlockingActivity( activity ) || CTerrorPlayer::IsClimbingActivity( activity ) ) &&
		flCycle >= m_flPreviousMovementCycle && nSequence >= 0 && nSequence < pStudioHdr->GetNumSeq() )
	{
		Vector movement( vec3_origin );
		QAngle angles( 0.0f, 0.0f, 0.0f );
		Studio_SeqMovement( pStudioHdr, nSequence, m_flPreviousMovementCycle, flCycle,
			pPlayer->GetPoseParameterArray(), movement, angles );
#ifdef CLIENT_DLL
		const int original = LookupTerrorOriginalActivity( activity );
		const bool bHanging = original >= 710 && original <= 715;
#else
		const bool bHanging = pPlayer->m_isHangingFromLedge;
#endif
		if ( !bHanging )
			VectorYawRotate( movement, m_flEyeYaw, movement );
		m_vecAnimationMovement += movement;
		m_flPreviousMovementCycle = flCycle;
	}
	else
	{
		ResetAnimationMovement();
		m_flPreviousMovementCycle = 0.0f;
	}
}

void CTerrorPlayerAnimState::UpdatePlaybackAndSpecialPoses( CStudioHdr *pStudioHdr )
{
	// Tail of S341AC0/C2822D0, after authored motion and before aim multiplier.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	float flPlayback = 1.0f;
	const Activity climbGesture = TerrorActivityFromOriginal( 716 );
	if ( pPlayer->GetMoveType() == MOVETYPE_LADDER )
	{
		Vector velocity;
		GetOuterAbsVelocity( velocity );
#ifdef CLIENT_DLL
		C_FuncElevator *pElevator = static_cast< C_FuncElevator * >( pPlayer->GetElevator() );
		if ( pElevator && !pPlayer->IsLocalPlayer() )
			velocity.z -= pElevator->GetCurrentSpeed();
#endif
		const double flDismount = GetLadderDismountPose();
		if ( m_nDismountPoseParameter >= 0 )
			pPlayer->SetPoseParameter( pStudioHdr, m_nDismountPoseParameter, flDismount );
		flPlayback = fabs( velocity.z ) * 0.0099999998f;
		const int team = pPlayer->GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			!IsGestureSlotPlaying( GESTURE_SLOT_SWIM, climbGesture ) )
			RestartGesture( GESTURE_SLOT_SWIM, climbGesture, true, true );
	}
	else if ( IsGestureSlotPlaying( GESTURE_SLOT_SWIM, climbGesture ) )
		ResetGestureSlot( GESTURE_SLOT_SWIM, 0.0f );

#ifdef CLIENT_DLL
	if ( pPlayer->m_usingMountedGun && pPlayer->m_terrorFireInterval.GetStartTime() > 0.0f )
	{
		const float flElapsed = pPlayer->m_terrorFireInterval.GetElapsedTime();
		if ( flElapsed < 0.30000001f )
			flPlayback = z_minigun_fire_anim_speed.GetFloat();
	}
#else
	if ( pPlayer->m_usingMountedGun && pPlayer->GetTimeSinceWeaponFired() < 0.30000001f )
		flPlayback = z_minigun_fire_anim_speed.GetFloat();
#endif
	pPlayer->SetPlaybackRate( flPlayback );

	const int activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	if ( activity == 747 || activity == 748 || activity == 525 )
	{
		Vector velocity;
		GetOuterAbsVelocity( velocity );
		const float flRawSpeed = velocity.Length2D();
		float flSpeed;
		if ( flRawSpeed <= 600.0f )
			flSpeed = flRawSpeed >= 10.0f ? flRawSpeed : 10.0f;
		else
			flSpeed = 600.0f;
		const float flVerticalLimit = 750.0f - ( ( flSpeed - 10.0f ) * 400.0f ) * 0.0016949152f;
		float flVerticalFraction;
		if ( -flVerticalLimit == flVerticalLimit )
			flVerticalFraction = velocity.z < flVerticalLimit ? 0.0f : 1.0f;
		else
			flVerticalFraction = ( velocity.z - -flVerticalLimit ) / ( flVerticalLimit - -flVerticalLimit );
		float flHorizontalFraction = flSpeed;
		if ( flSpeed <= 600.0f )
		{
			if ( flSpeed < 0.0f )
				flHorizontalFraction = 0.0f;
		}
		else
			flHorizontalFraction = 600.0f;
		flHorizontalFraction *= 0.0020000001f;
		pPlayer->SetPoseParameter( pStudioHdr, m_nVerticalVelocityPoseParameter, 1.0f - flVerticalFraction );
		pPlayer->SetPoseParameter( pStudioHdr, m_nHorizontalVelocityPoseParameter, flHorizontalFraction );
	}
	if ( ( activity == 779 || activity == 775 ) && pPlayer->m_tongueOwner.Get() && m_nTongueAnglePoseParameter >= 0 )
	{
		const Vector &origin = pPlayer->GetAbsOrigin();
		Vector direction = pPlayer->GetTonguePullPosition( false ) - origin;
		direction.z += g_pGameRules->GetViewVectors()->m_vView.z * 0.5f;
		VectorNormalize( direction );
		pPlayer->SetPoseParameter( pStudioHdr, m_nTongueAnglePoseParameter, direction.z );
	}
}

double CTerrorPlayerAnimState::ReadDebugPoseParameter( int nPoseParameter, bool bPhysical )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
#ifdef CLIENT_DLL
	// C27ECC0 de-normalizes for its display; C27FFA0 deliberately dumps raw
	// normalized storage. Range lookup precedes the getter in the display.
	if ( bPhysical )
	{
		float flMin, flMax;
		pPlayer->GetPoseParameterRange( nPoseParameter, flMin, flMax );
		const float flRange = flMax - flMin;
		return static_cast< double >( flMin ) +
			static_cast< double >( flRange ) * pPlayer->GetPoseParameter( nPoseParameter );
	}
#endif
	return pPlayer->GetPoseParameter( nPoseParameter );
}

void CTerrorPlayerAnimState::ShowTerrorDebugState( int nPlayerIndex, CStudioHdr *pStudioHdr, bool bDump )
{
	// S33DF00/S33ECE0 and C27ECC0/C27FFA0 share the output, not the client
	// physical-vs-normalized pose policy or the server's private latches.
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	Vector velocity;
	GetOuterAbsVelocity( velocity );
	char characterName[128];
	const int team = pPlayer->GetTeamNumber();
	Q_strncpy( characterName,
		team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ?
		GetSurvivorCharacterDisplayName( pPlayer->GetSurvivorCharacter() ) :
		GetZombieClassName( pPlayer->GetZombieClass() ), sizeof( characterName ) );
#ifdef CLIENT_DLL
	const char *pSide = "CLIENT";
	int nLine = 5;
#else
	const char *pSide = "SERVER";
	int nLine = 25;
#endif
	if ( bDump )
	{
		TerrorDebugPrintf( NULL, "\n" );
		TerrorDebugPrintf( NULL, "--- Dumping state for entity %d - (frame: %d) %s\n",
			nPlayerIndex, gpGlobals->framecount, pSide );
		TerrorDebugPrintf( NULL, "   %s\n", characterName );
	}
	else
	{
		engine->Con_NPrintf( nLine++, "-------------- %s - %s (%d): frame %d (%dms) --------------",
			pSide, characterName, nPlayerIndex, gpGlobals->framecount,
			static_cast< int >( static_cast< double >( gpGlobals->frametime ) * 1000.0 ) );
	}

#define TERROR_ANIM_STATE_PRINT( format, ... ) \
	do \
	{ \
		if ( bDump ) \
			TerrorDebugPrintf( NULL, "   " format "\n", __VA_ARGS__ ); \
		else \
			engine->Con_NPrintf( nLine++, format, __VA_ARGS__ ); \
	} while ( 0 )

	TERROR_ANIM_STATE_PRINT( "%s (%2.2f) %2.2f", ActivityList_NameForIndex( m_eCurrentMainSequenceActivity ),
		pPlayer->GetCycle(), static_cast< float >( pPlayer->m_fServerAnimStartTime ) );
	TERROR_ANIM_STATE_PRINT( "%s (%d)", GetSequenceName( pStudioHdr, pPlayer->GetSequence() ), pPlayer->GetSequence() );
	const float flOriginZ = pPlayer->GetAbsOrigin().z;
	const float flOriginY = pPlayer->GetAbsOrigin().y;
	const float flOriginX = pPlayer->GetAbsOrigin().x;
	TERROR_ANIM_STATE_PRINT( "Loc: %2.2f %2.2f %2.2f", flOriginX, flOriginY, flOriginZ );
	const float flRotationY = pPlayer->GetAbsAngles().y;
	const float flRotationX = pPlayer->GetAbsAngles().x;
	const float flRotationZ = pPlayer->GetAbsAngles().z;
	// Both native diagnostics print Z, X, Y despite the unlabeled format.
	TERROR_ANIM_STATE_PRINT( "Rot: %2.2f %2.2f %2.2f", flRotationZ, flRotationX, flRotationY );
	const float flSpeed = sqrtf( velocity.y * velocity.y + velocity.x * velocity.x );
	const double flMaxSpeed = GetCurrentMaxGroundSpeed();
	TERROR_ANIM_STATE_PRINT( "Speed: %2.2f (%2.2f max)", flSpeed, flMaxSpeed );
	TERROR_ANIM_STATE_PRINT( "EyeYaw: %2.2f, FeetYaw: %2.2f, GoalFeetYaw: %2.2f",
		m_flEyeYaw, m_flCurrentFeetYaw, m_flGoalFeetYaw );
	TERROR_ANIM_STATE_PRINT( "%s", "--- PoseParams ---" );
	const bool bPhysical = !bDump;
	const float flMoveY = ReadDebugPoseParameter( m_PoseParameterData.m_iMoveY, bPhysical );
	const double flMoveX = ReadDebugPoseParameter( m_PoseParameterData.m_iMoveX, bPhysical );
	TERROR_ANIM_STATE_PRINT( "Move: %2.2f %2.2f", flMoveX, flMoveY );
	const float flBodyYaw = ReadDebugPoseParameter( m_PoseParameterData.m_iAimYaw, bPhysical );
	const double flHeadYaw = ReadDebugPoseParameter( m_nHeadYawPoseParameter, bPhysical );
	TERROR_ANIM_STATE_PRINT( "Yaw: %2.2f %2.2f", flHeadYaw, flBodyYaw );
	const float flBodyPitch = ReadDebugPoseParameter( m_PoseParameterData.m_iAimPitch, bPhysical );
	const double flHeadPitch = ReadDebugPoseParameter( m_nHeadPitchPoseParameter, bPhysical );
	TERROR_ANIM_STATE_PRINT( "Pitch: %2.2f %2.2f", flHeadPitch, flBodyPitch );
	TERROR_ANIM_STATE_PRINT( "Dismount: %2.2f", ReadDebugPoseParameter( m_nDismountPoseParameter, bPhysical ) );
	const float flVertical = ReadDebugPoseParameter( m_nVerticalVelocityPoseParameter, bPhysical );
	const float flHorizontal = ReadDebugPoseParameter( m_nHorizontalVelocityPoseParameter, bPhysical );
	const double flTongue = ReadDebugPoseParameter( m_nTongueAnglePoseParameter, bPhysical );
	TERROR_ANIM_STATE_PRINT( "TongleAngle: %2.2f, H/V Velocities: %2.2f %2.2f", flTongue, flHorizontal, flVertical );
	TERROR_ANIM_STATE_PRINT( "%s", "--- Flags ---" );
#ifdef CLIENT_DLL
	const int activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	TERROR_ANIM_STATE_PRINT( "Dead: %s, InAir: %s, Stagger: %s",
		activity == 20 || activity == 669 || activity == 667 ? "YES" : "NO",
		m_bWasAirborne ? "YES" : "NO", IsAnimationBusy() ? "YES" : "NO" );
	TERROR_ANIM_STATE_PRINT( "Charged: %s, Pummeled: %s, Pounded: %s, Pummeling: %s, Pounding: %s",
		activity >= 524 && activity <= 526 ? "YES" : "NO", activity == 527 ? "YES" : "NO",
		activity >= 531 && activity <= 534 ? "YES" : "NO", activity == 535 ? "YES" : "NO",
		activity >= 537 && activity <= 541 ? "YES" : "NO" );
	TERROR_ANIM_STATE_PRINT( "TankPunched: %s, Pounced: %s, Tongued: %s",
		activity >= 521 && activity <= 523 ? "YES" : "NO", activity >= 770 && activity <= 771 ? "YES" : "NO",
		activity == 773 || activity == 775 || activity == 779 || activity == 783 || activity == 958 ? "YES" : "NO" );
	TERROR_ANIM_STATE_PRINT( "Pouncing: %s, Tonguing: %s, Charging: %s, TongueReeling: %s, TongueAttacking: %s",
		activity >= 747 && ( activity <= 748 || activity == 764 ) ? "YES" : "NO",
		activity >= 750 && ( activity <= 753 || activity == 755 ) ? "YES" : "NO",
		activity == 10 ? "YES" : "NO", activity == 752 || activity == 753 ? "YES" : "NO",
		activity == 755 ? "YES" : "NO" );
	const bool bClimbing = activity == 719 || activity == 720 || activity == 722 || activity == 724 ||
		activity == 729 || activity == 731 || activity == 734 || activity == 736;
	// C27ECC0/C27FFA0 retain the inverted rage test and stagger-as-rock flag.
	TERROR_ANIM_STATE_PRINT( "RockThrow: %s, Climb: %s, Rage: %s", IsAnimationBusy() ? "YES" : "NO",
		bClimbing ? "YES" : "NO", activity < 792 || activity > 795 ? "YES" : "NO" );
#else
	TERROR_ANIM_STATE_PRINT( "Dead: %s, InAir: %s, Stagger: %s",
		m_bTerrorDying ? "YES" : "NO", m_bWasAirborne ? "YES" : "NO", m_nStaggerDirection != -1 ? "YES" : "NO" );
	TERROR_ANIM_STATE_PRINT( "Charged: %s, Pummeled: %s, Pounded: %s, Pummeling: %s, Pounding: %s",
		m_bChargerImpact ? "YES" : "NO", m_bChargerPummeled ? "YES" : "NO", m_bChargerPounded ? "YES" : "NO",
		m_bChargerPummel ? "YES" : "NO", m_bChargerPound ? "YES" : "NO" );
	TERROR_ANIM_STATE_PRINT( "TankPunched: %s, Pounced: %s, Tongued: %s",
		m_bHitByTank ? "YES" : "NO", m_bPounced ? "YES" : "NO", m_bTongueVictim ? "YES" : "NO" );
	TERROR_ANIM_STATE_PRINT( "Pouncing: %s, Tonguing: %s, Charging: %s, TongueReeling: %s, TongueAttacking: %s",
		m_bHunterPounce ? "YES" : "NO", m_bTongueAttack ? "YES" : "NO", m_bLunging ? "YES" : "NO",
		m_bTongueChoke ? "YES" : "NO", m_bTongueWallHit ? "YES" : "NO" );
	// S33DF00/S33ECE0 also report the stagger direction as RockThrow.
	TERROR_ANIM_STATE_PRINT( "RockThrow: %s, Climb: %s, Rage: %s", m_nStaggerDirection != -1 ? "YES" : "NO",
		m_nClimbAnimation != -1 ? "YES" : "NO", m_nTankSpecialAnimation != -1 ? "YES" : "NO" );
#endif
#undef TERROR_ANIM_STATE_PRINT

	if ( !bDump )
	{
		Vector position = pPlayer->GetAbsOrigin() + Vector( 0.0f, 0.0f, 3.0f );
		QAngle angles( 0.0f, m_flCurrentFeetYaw, 0.0f );
		Vector forward, right, up;
		AngleVectors( angles, &forward, &right, &up );
		Vector halfRight = ( right * 10.0f ) * 0.5f;
		debugoverlay->AddTriangleOverlay( position + halfRight, position - halfRight,
			position + forward * 80.0f, 255, 0, 0, 255, false, 0.0099999998f );
		angles.y = m_flGoalFeetYaw;
		AngleVectors( angles, &forward, &right, &up );
		halfRight = ( right * 10.0f ) * 0.5f;
		debugoverlay->AddTriangleOverlay( position + halfRight, position - halfRight,
			position + forward * 80.0f, 255, 255, 0, 255, false, 0.0099999998f );
		angles.y = m_flEyeYaw;
		position.z += 50.0f;
		AngleVectors( angles, &forward, &right, &up );
		halfRight = ( right * 10.0f ) * 0.5f;
		debugoverlay->AddTriangleOverlay( position + halfRight, position - halfRight,
			position + forward * 80.0f, 255, 255, 255, 255, false, 0.0099999998f );
	}
}

void CTerrorPlayerAnimState::DumpAllTerrorAnimationStates()
{
	// S3412C0/C281940 retain the different admitted server/client rosters.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *pBasePlayer = UTIL_PlayerByIndex( i );
		if ( !pBasePlayer || !pBasePlayer->IsPlayer() )
			continue;
#ifdef CLIENT_DLL
		if ( pBasePlayer->IsDormant() || !g_PR->IsConnected( i ) )
			continue;
#else
		if ( !pBasePlayer->edict() || pBasePlayer->entindex() == 0 || !pBasePlayer->IsConnected() )
			continue;
#endif
		CTerrorPlayer *pPlayer = static_cast< CTerrorPlayer * >( pBasePlayer );
		CStudioHdr *pStudioHdr = pPlayer->GetModelPtr();
		if ( pStudioHdr && pStudioHdr->IsValid() )
			pPlayer->GetTerrorAnimState()->ShowTerrorDebugState( i, pStudioHdr, true );
	}
}

void CTerrorPlayerAnimState::Update( float eyeYaw, float eyePitch )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	CStudioHdr *pStudioHdr = pPlayer->GetModelPtr();
	if ( !pStudioHdr || !pStudioHdr->IsValid() )
		return;
	const int nPlayerIndex = pPlayer->entindex();
	if ( anim_showstate.GetInt() == nPlayerIndex )
		ShowTerrorDebugState( nPlayerIndex, pStudioHdr, false );
	if ( !ShouldUpdateAnimState() )
	{
		ClearAnimationState();
		return;
	}

	// S341AC0/C2822D0 preserve prior eye angles during the native action lock.
	if ( !ShouldLockEyeAngles() )
	{
		const float flEyeYaw = AngleNormalize( eyeYaw );
		if ( m_flEyeYaw != flEyeYaw )
			m_flLastAimTurnTime = gpGlobals->curtime;
		m_flEyeYaw = flEyeYaw;
		m_flEyePitch = AngleNormalize( eyePitch );
	}

	// S:0x341AC0 ->0x124000: main selection stores the untranslated activity,
	// then ground-speed interpolation and seven-slot cleanup each run once.
	// The owner has already advanced native animation before this update.
	ComputeSequences( pStudioHdr );
#ifndef CLIENT_DLL
	UpdateIdleGesture();
#endif
	if ( SetupSurvivorPoseParameters( pStudioHdr ) )
	{
		ComputePoseParam_AimPitch( pStudioHdr );
		ComputePoseParam_AimYaw( pStudioHdr );
		ComputePoseParam_MoveYaw( pStudioHdr );
	}
	if ( CTerrorPlayer::IsAttackBlockingActivity( m_eCurrentMainSequenceActivity ) )
		pPlayer->SetMoveType( MOVETYPE_CUSTOM );
	UpdateAnimationMovement( pStudioHdr );
	UpdatePlaybackAndSpecialPoses( pStudioHdr );

	// Original writes this AFTER aiming, so slot-0 gain changes take effect
	// on the next update, including the last frame of a completed gesture.
	m_flAimUpdateMultiplier = IsGestureSlotActive( GESTURE_SLOT_ATTACK_AND_RELOAD ) ? 5.0f : 1.0f;
#ifdef CLIENT_DLL
	if ( pPlayer->ShouldDrawLocalPlayerModel() )
		pPlayer->SetPlaybackRate( 1.0f );
#endif
	if ( s_bDumpTerrorAnimationStates )
	{
		DumpAllTerrorAnimationStates();
		s_bDumpTerrorAnimationStates = false;
	}
	if ( mp_showgestureslots.GetInt() == nPlayerIndex )
	{
		DebugGestureInfo();
#ifdef CLIENT_DLL
		const int nHeaderLine = 41;
		engine->Con_NPrintf( nHeaderLine, "%s", "CLIENT" );
#else
		const int nHeaderLine = 34;
		engine->Con_NPrintf( nHeaderLine, "%s", "SERVER" );
#endif
		for ( int i = 0; i < GESTURE_SLOT_COUNT; ++i )
		{
			const int activity = pPlayer->m_NetGestureActivity[i];
			engine->Con_NPrintf( nHeaderLine + i + 1, "NetGesture %d : (%1.3f) %s", i,
				pPlayer->m_NetGestureStartTime[i], activity == ACT_INVALID ? "INVALID" : ActivityList_NameForIndex( activity ) );
		}
	}
}

#ifndef CLIENT_DLL
void CTerrorPlayerAnimState::DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData )
{
	if ( TerrorGameRules()->IsInIntro() )
		return;
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	const Activity movement = UntranslateActivity( m_eCurrentMainSequenceActivity );
	const bool bCrouched = movement == ACT_CROUCHIDLE || movement == ACT_RUN_CROUCH;
	Activity activity = ACT_INVALID;
	switch ( event )
	{
	case TERROR_ANIMEVENT_JUMP:
		m_bJumping = true;
		return;
	case TERROR_ANIMEVENT_LAND:
		if ( pPlayer->GetTeamNumber() == TEAM_INFECTED && pPlayer->GetZombieClass() == 3 && nData == 1 )
			RestartGesture( GESTURE_SLOT_JUMP, TerrorActivityFromOriginal( 741 ), true, false );
		else if ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			RestartGesture( GESTURE_SLOT_JUMP, TerrorActivityFromOriginal( 662 ), true, false );
		return;
	case TERROR_ANIMEVENT_DYING:
		m_bTerrorDying = true;
		return;
	case TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE:
	case TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE_ALT:
		// The actual caller supplies a target Activity, not an original ID.
		if ( !IsGestureSlotActive( GESTURE_SLOT_ATTACK_AND_RELOAD ) )
			RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, static_cast< Activity >( nData ), true, false );
		return;
	case TERROR_ANIMEVENT_ATTACK:
	case TERROR_ANIMEVENT_FIRE_PRIMARY:
	case TERROR_ANIMEVENT_FIRE_PRIMARY_ALT:
	case TERROR_ANIMEVENT_FIRE_GRENADE_LAUNCHER:
	case TERROR_ANIMEVENT_MELEE_SWEEP:
	case TERROR_ANIMEVENT_MELEE_SHOVE:
	case TERROR_ANIMEVENT_MELEE_STRAIGHT:
	case TERROR_ANIMEVENT_MELEE_STOMP:
		if ( pPlayer->GetTeamNumber() == TEAM_INFECTED )
		{
			const int zombieClass = pPlayer->GetZombieClass();
			if ( event == TERROR_ANIMEVENT_MELEE_SWEEP && zombieClass == 8 )
				activity = TerrorActivityFromOriginal( 763 );
			else if ( event != TERROR_ANIMEVENT_FIRE_PRIMARY_ALT )
				activity = TerrorActivityFromOriginal( zombieClass == 8 ? 790 : 613 );
			else if ( zombieClass == 1 || zombieClass == 2 )
				activity = TerrorActivityFromOriginal( 695 );
		}
		else if ( !pPlayer->m_usingMountedWeapon )
		{
			CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase * >( pPlayer->GetActiveWeapon() );
			if ( pWeapon )
				activity = pWeapon->GetPlayerAttackActivity( event, movement );
		}
		break;
	case TERROR_ANIMEVENT_RELOAD:
	case TERROR_ANIMEVENT_RELOAD_LOOP:
	case TERROR_ANIMEVENT_RELOAD_END:
	case TERROR_ANIMEVENT_RELOAD_START:
		if ( !m_bUseGestureActive )
		{
			CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase * >( pPlayer->GetActiveWeapon() );
			activity = pWeapon->GetPlayerReloadActivity( event, movement );
		}
		break;
	case TERROR_ANIMEVENT_DEPLOY:
		{
			CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase * >( pPlayer->GetActiveWeapon() );
			if ( pWeapon )
				activity = pWeapon->GetPlayerDeployActivity( event, movement );
		}
		break;
	case TERROR_ANIMEVENT_CHAINSAW_STARTUP:
		m_bChainsawStartup = true;
		return;
	case TERROR_ANIMEVENT_HEAL_SELF:
		activity = TerrorPlayerActivity( bCrouched ? "ACT_TERROR_CROUCH_HEAL_SELF" : "ACT_TERROR_HEAL_SELF" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_HEAL_OTHER:
		activity = TerrorPlayerActivity( bCrouched ? "ACT_TERROR_CROUCH_HEAL_FRIEND" : "ACT_TERROR_HEAL_FRIEND" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_REVIVE_START_LOW:
		activity = TerrorPlayerActivity( bCrouched ? "ACT_TERROR_CROUCH_HEAL_INCAPACITATED" : "ACT_TERROR_HEAL_INCAPACITATED" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_REVIVE_START_HIGH:
		activity = TerrorPlayerActivity( bCrouched ? "ACT_TERROR_CROUCH_HEAL_INCAPACITATED_ABOVE" : "ACT_TERROR_HEAL_INCAPACITATED_ABOVE" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_DEFIBRILLATE:
		activity = TerrorPlayerActivity( bCrouched ? "ACT_TERROR_CROUCH_USE_DEFIBRILLATOR" : "ACT_TERROR_USE_DEFIBRILLATOR" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_RETURN_TO_LIFE:
		m_bReturnToLifePending = true;
		return;
	case TERROR_ANIMEVENT_DEPLOY_UPGRADE:
		activity = TerrorPlayerActivity( bCrouched ? "ACT_TERROR_CROUCH_USE_UPGRADEPACK" : "ACT_TERROR_USE_UPGRADEPACK" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_POUR_GAS:
		activity = TerrorPlayerActivity( "ACT_TERROR_USE_GAS_CAN" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_POUR_COLA:
		activity = TerrorPlayerActivity( "ACT_TERROR_USE_COLA" );
		m_bUseGestureActive = true;
		break;
	case TERROR_ANIMEVENT_CANCEL_ATTACK_GESTURE:
		ResetGestureSlot( GESTURE_SLOT_ATTACK_AND_RELOAD );
		return;
	case TERROR_ANIMEVENT_CANCEL_USE:
	case TERROR_ANIMEVENT_CANCEL_GAS_USE:
	case TERROR_ANIMEVENT_CANCEL_COLA_USE:
		if ( m_bUseGestureActive )
		{
			m_bUseGestureActive = false;
			ResetGestureSlot( GESTURE_SLOT_ATTACK_AND_RELOAD );
		}
		return;
	case TERROR_ANIMEVENT_REVIVE_TARGET_START:
	case TERROR_ANIMEVENT_REVIVE_TARGET_END:
	case TERROR_ANIMEVENT_REVIVE_SUCCESS:
		// Native 47..49 fall through to the base's unrecognized-event path.
		return;
	case TERROR_ANIMEVENT_POUNCE_LAND:
		// S333FCA sends83; both33CB40 and native base124220 leave it unchanged.
		return;
	case TERROR_ANIMEVENT_FLINCH_FRONT:
	case TERROR_ANIMEVENT_FLINCH_BACK:
		{
			const bool bTank = pPlayer && pPlayer->GetTeamNumber() == TEAM_INFECTED && pPlayer->GetZombieClass() == 8;
			if ( bTank )
			{
				bool bSuppressed = pPlayer->IsIncapacitated() ||
					LookupTerrorOriginalActivity( m_aGestureSlots[GESTURE_SLOT_ATTACK_AND_RELOAD].m_iActivity ) == 763;
				if ( !bSuppressed )
				{
					const int flinch = LookupTerrorOriginalActivity( m_aGestureSlots[GESTURE_SLOT_FLINCH].m_iActivity );
					bSuppressed = flinch == 622 || flinch == 621 || flinch == 623 || flinch == 624;
				}
				if ( bSuppressed )
					return;
			}
			// Keep the positive >= test: a NaN layer cycle must not pass it.
			if ( IsGestureSlotActive( GESTURE_SLOT_FLINCH ) &&
				!( GetGestureSlotLayer( GESTURE_SLOT_FLINCH )->m_flCycle >= 0.5f ) )
				return;
			if ( m_bJockeyRidden )
				return;
			int selected;
			if ( pPlayer && pPlayer->GetTeamNumber() == TEAM_INFECTED &&
				pPlayer->GetZombieClass() == 3 && pPlayer->m_Local.m_bDucked )
			{
				switch ( pPlayer->LastHitGroup() )
				{
				case 1: case 2: case 3: selected = event == TERROR_ANIMEVENT_FLINCH_FRONT ? 127 : 128; break;
				case 4: case 6: selected = 129; break;
				case 5: case 7: selected = 130; break;
				default: return;
				}
			}
			else if ( pPlayer->m_isHangingFromLedge )
				selected = 694;
			else if ( bTank )
			{
				switch ( pPlayer->LastHitGroup() )
				{
				case 1: case 2: selected = 117; break;
				case 3: case 6: case 7: selected = 118; break;
				case 4: selected = 119; break;
				case 5: selected = 120; break;
				default: return;
				}
			}
			else
				selected = 693;
			RestartGesture( GESTURE_SLOT_FLINCH, TerrorActivityFromOriginal( selected ), true, false );
		}
		return;
	case TERROR_ANIMEVENT_GRENADE_PIN_PULL:
	case TERROR_ANIMEVENT_GRENADE_RELEASE:
		{
			CBaseCSGrenade *pGrenade = dynamic_cast< CBaseCSGrenade * >( pPlayer->GetActiveWeapon() );
			if ( pGrenade )
			{
				const bool bRelease = event == TERROR_ANIMEVENT_GRENADE_RELEASE;
				RestartGesture( bRelease ? GESTURE_SLOT_ATTACK_AND_RELOAD : GESTURE_SLOT_GRENADE,
					pGrenade->GetPlayerGrenadeActivity( movement, bRelease ), true, false );
			}
		}
		return;
	case TERROR_ANIMEVENT_CANCEL_GRENADE:
		ResetGestureSlot( GESTURE_SLOT_GRENADE );
		return;
	case TERROR_ANIMEVENT_ITEM_PICKUP:
		activity = TerrorActivityFromOriginal( 699 );
		break;
	case TERROR_ANIMEVENT_STAGGER:
		ResetGestureSlots();
		m_nStaggerDirection = nData + ( pPlayer->m_pounceVictim.Get() ? 4 : 0 );
		return;
	case TERROR_ANIMEVENT_CLEAR_STAGGER:
		m_nStaggerDirection = -1;
		return;
	case TERROR_ANIMEVENT_SPIT:
		m_bSpitting = true;
		return;
	case TERROR_ANIMEVENT_CHARGE_START:
		m_bLunging = true;
		return;
	case TERROR_ANIMEVENT_CHARGE_END:
		m_bLunging = false;
		return;
	case TERROR_ANIMEVENT_CHARGER_PUMMEL_START:
		m_bChargerPummel = true;
		return;
	case TERROR_ANIMEVENT_CHARGER_POUND_START:
		m_bChargerPummel = false;
		m_bChargerPound = true;
		return;
	case TERROR_ANIMEVENT_CHARGER_IMPACT_GROUND:
		activity = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_SLAM_GROUND );
		break;
	case TERROR_ANIMEVENT_CHARGER_IMPACT:
		pPlayer->ClearStagger();
		m_bChargerCarried = false;
		m_bChargerImpact = true;
		return;
	case TERROR_ANIMEVENT_CHARGER_PUMMELED:
		pPlayer->ClearStagger();
		ResetGestureSlots();
		RestartMainSequence();
		m_bChargerPummelVictimNotified = false;
		m_bChargerImpact = false;
		m_bChargerPummeled = true;
		return;
	case TERROR_ANIMEVENT_CHARGER_POUNDED:
		pPlayer->ClearStagger();
		ResetGestureSlots();
		m_bChargerPummelVictimNotified = false;
		m_bChargerPounded = true;
		m_bChargerImpact = false;
		m_bChargerPummeled = false;
		m_bChargerSlammedWall = false;
		m_bChargerSlammedGround = false;
		return;
	case TERROR_ANIMEVENT_CHARGER_CARRIED:
		pPlayer->ClearStagger();
		m_bChargerCarried = true;
		return;
	case TERROR_ANIMEVENT_CHARGER_IMPACT_WALL_VICTIM:
		m_nStaggerDirection = -1;
		m_bChargerSlammedWall = true;
		return;
	case TERROR_ANIMEVENT_CHARGER_IMPACT_GROUND_VICTIM:
		m_bChargerSlammedGround = true;
		return;
	case TERROR_ANIMEVENT_POUNCE_ACQUIRE:
		m_bHunterPounce = true;
		return;
	case TERROR_ANIMEVENT_JOCKEY_RIDE:
		m_bJockeyRide = true;
		return;
	case TERROR_ANIMEVENT_JOCKEY_RIDDEN:
		pPlayer->ClearStagger();
		m_bJockeyRidden = true;
		return;
	case TERROR_ANIMEVENT_POUNCED:
		pPlayer->ClearStagger();
		m_bPounced = true;
		return;
	case TERROR_ANIMEVENT_TONGUE_PREPARE:
		activity = TerrorActivityFromOriginal( 749 );
		break;
	case TERROR_ANIMEVENT_TONGUE_ATTACK_START:
		m_bTongueAttack = true;
		m_bTongueChoke = false;
		m_bTongueWallHit = false;
		return;
	case TERROR_ANIMEVENT_TONGUE_CHOKE_START:
		m_bTongueAttack = true;
		m_bTongueChoke = true;
		m_bTongueWallHit = false;
		return;
	case TERROR_ANIMEVENT_TONGUE_WALL_HIT:
		m_bTongueAttack = true;
		m_bTongueChoke = false;
		m_bTongueWallHit = true;
		return;
	case TERROR_ANIMEVENT_TONGUE_ATTACK_END:
		m_bTongueAttack = false;
		m_bTongueChoke = false;
		m_bTongueWallHit = false;
		return;
	case TERROR_ANIMEVENT_TONGUE_GRABBED:
		pPlayer->ClearStagger();
		m_bTongueVictim = true;
		return;
	case TERROR_ANIMEVENT_THROW:
		m_nThrowAnimation = nData;
		return;
	case TERROR_ANIMEVENT_CLIMB:
		m_nClimbAnimation = nData;
		return;
	case TERROR_ANIMEVENT_TANK_SPECIAL:
		m_nTankSpecialAnimation = nData;
		return;
	case TERROR_ANIMEVENT_HIT_BY_TANK:
		m_bHitByTank = true;
		m_bPounced = false;
		return;
	case TERROR_ANIMEVENT_FIDGET:
		if ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			RestartGesture( GESTURE_SLOT_FLINCH, TerrorActivityFromOriginal( 579 ), true, false );
		return;
	}
	if ( activity != ACT_INVALID )
		RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, activity, true, false );
}
#endif

#ifdef CLIENT_DLL
void CTerrorPlayerAnimState::OnReceivedGesture( int iGestureSlot, Activity activity )
{
	if ( activity == ACT_INVALID )
		ResetGestureSlot( iGestureSlot, 0.5f );
	else
		RestartGesture( iGestureSlot, activity, true, false );
}

int CTerrorPlayerAnimState::SelectGestureSequence( int iGestureSlot, Activity activity, CBasePlayer *pPlayer )
{
	const int iSequence = GetTerrorPlayer()->m_NetGestureSequence[iGestureSlot];
	return iSequence > 0 ? iSequence : BaseClass::SelectGestureSequence( iGestureSlot, activity, pPlayer );
}
#else
void CTerrorPlayerAnimState::OnGestureStarted( int iGestureSlot, Activity activity, int iSequence )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	pPlayer->m_NetGestureSequence.Set( iGestureSlot, iSequence );
	pPlayer->m_NetGestureActivity.Set( iGestureSlot, activity );
	pPlayer->m_NetGestureStartTime.Set( iGestureSlot, gpGlobals->curtime );
}

void CTerrorPlayerAnimState::OnGestureStopped( int iGestureSlot )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayer();
	pPlayer->m_NetGestureSequence.Set( iGestureSlot, ACT_INVALID );
	pPlayer->m_NetGestureActivity.Set( iGestureSlot, ACT_INVALID );
}
#endif

#ifdef CLIENT_DLL
// C27E4C0 reads the actual received main activity, not server direction380.
bool CTerrorPlayerAnimState::IsAnimationBusy() const
{
	const TerrorOriginalActivity activity = LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity );
	return ( activity >= TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD &&
			 activity <= TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_MELEE ) ||
		( activity >= TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_L &&
		  activity <= TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_FORWARD );
}
#endif

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#if defined( CLIENT_DLL ) && defined( L4D2 )
bool CTerrorPlayerAnimState::IsGrenadePresentationSuppressed()
{
	return GetTerrorPlayer()->m_bGrenadePresentationSuppressed;
}
#endif
