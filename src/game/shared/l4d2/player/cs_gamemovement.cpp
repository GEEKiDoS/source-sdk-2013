//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/cs_gamemovement.h"
#include "movevars_shared.h"
#include "in_buttons.h"
#ifndef CLIENT_DLL
#include "env_player_surface_trigger.h"
#include "vphysics_interface.h"
#endif
#include "l4d2/terror_physics_math.h"
#include "tier0/memdbgon.h"

CCSGameMovement::CCSGameMovement()
{
	m_pCSPlayer = NULL;
}

void CCSGameMovement::ProcessMovement( CBasePlayer *pPlayer, CMoveData *pMove )
{
	m_pCSPlayer = ToCSPlayer( pPlayer );
	BaseClass::ProcessMovement( pPlayer, pMove );
	m_pCSPlayer = NULL;
}


void CCSGameMovement::CategorizePosition( bool bForceMoveToEndPos )
{
	// Ordinary normal-axis policy from server 0xF6FB0 / client 0x101320.
	// Terror's wrappers (0x2D22A0 / 0x276770) retain this ground decision.
	player->m_surfaceFriction = 1.0f;
	CheckWater();
	if ( player->IsObserver() )
		return;
	if ( player->GetMoveType() == MOVETYPE_NOCLIP )
	{
		SetGroundEntity( NULL );
		return;
	}

	Vector vecOrigin = mv->GetAbsOrigin();
	Vector vecPoint = vecOrigin;
	vecPoint.z -= 2.0f;

	float flZVelocity = mv->m_vecVelocity.z;
	bool bMovingUp = flZVelocity > 0.0f;
	bool bMovingUpRapidly = flZVelocity > 140.0f;
	CBaseEntity *pGround = player->GetGroundEntity();
	if ( bMovingUpRapidly && pGround )
		bMovingUpRapidly = flZVelocity - pGround->GetAbsVelocity().z > 140.0f;

	bool bMoveToEndPos = ( player->GetMoveType() == MOVETYPE_WALK &&
		pGround && player->GetWaterLevel() < WL_Eyes ) || bForceMoveToEndPos;
	if ( bMoveToEndPos )
		vecPoint.z -= player->m_Local.m_flStepSize;
	if ( bMovingUpRapidly || ( bMovingUp && player->GetMoveType() == MOVETYPE_LADDER ) )
	{
		SetGroundEntity( NULL );
		return;
	}

	// Lift the bottom without raising the top. Unlike the SDK's unraised
	// trace, this still finds a floor when the origin is exactly on its plane.
	Vector vecStart = vecOrigin;
	vecStart.z += 4.0f;
	Vector vecMins = GetPlayerMins();
	Vector vecMaxs = GetPlayerMaxs();
	vecMaxs.z -= 4.0f;
	unsigned int nMask = PlayerSolidMask();
	trace_t trace;
	TryTouchGround( vecStart, vecPoint, vecMins, vecMaxs, nMask, COLLISION_GROUP_PLAYER_MOVEMENT, trace );
	if ( !trace.m_pEnt || trace.plane.normal.z < GetStandableNormal() )
	{
		// 0xF50B0 / 0x100F60 retry the full hull in quadrants from the
		// unraised origin, preserving the first trace's endpos and fraction.
		TryTouchGroundInQuadrants( vecOrigin, vecPoint, nMask, COLLISION_GROUP_PLAYER_MOVEMENT, trace );
	}

	if ( !trace.m_pEnt || trace.plane.normal.z < GetStandableNormal() )
	{
		SetGroundEntity( NULL );
		if ( flZVelocity > 0.0f )
			player->m_surfaceFriction = 0.25f;
		bMoveToEndPos = false;
	}
	else
	{
		SetGroundEntity( &trace );
	}

#ifndef CLIENT_DLL
	if ( !player->IsInAVehicle() )
	{
		IPhysicsSurfaceProps *pSurfaceProps = MoveHelper()->GetSurfaceProps();
		surfacedata_t *pSurface = pSurfaceProps->GetSurfaceData( trace.surface.surfaceProps );
		char chMaterial = pSurface->game.material;
		if ( !player->GetGroundEntity() )
			chMaterial = 0;
		if ( player->m_chPreviousTextureType != chMaterial )
			CEnvPlayerSurfaceTrigger::SetPlayerSurface( player, chMaterial );
		player->m_chPreviousTextureType = chMaterial;
	}
#endif

	if ( bMoveToEndPos && !trace.startsolid && trace.fraction > 0.0f && trace.fraction < 1.0f )
		mv->SetAbsOrigin( trace.endpos );
}

float CCSGameMovement::GetSpeedModifier() const
{
	if ( m_pCSPlayer && ( player->GetFlags() & FL_ONGROUND ) )
		return m_pCSPlayer->GetVelocityModifier();
	return 1.0f;
}

void CCSGameMovement::ReduceTimers()
{
	// 0x23A260 reduces stamina before the inherited millisecond timers.
	if ( m_pCSPlayer && m_pCSPlayer->GetStamina() > 0.0f )
	{
		float flStamina = m_pCSPlayer->GetStamina() - gpGlobals->frametime * 1000.0f;
		m_pCSPlayer->SetStamina( MAX( flStamina, 0.0f ) );
	}
	BaseClass::ReduceTimers();
}

bool CCSGameMovement::CanAccelerate()
{
	if ( !m_pCSPlayer )
		return true;
	if ( m_pCSPlayer->State_Get() == STATE_ACTIVE || m_pCSPlayer->State_Get() == STATE_GHOST )
		return player->m_flWaterJumpTime == 0.0f;
	return player->IsObserver();
}


// S239060/C22A2D0 records the genuine successful ladder interaction.
bool CCSGameMovement::LadderMove()
{
	const bool bOnLadder = BaseClass::LadderMove();
	if ( bOnLadder )
		m_pCSPlayer->RecordLadderInfo( mv->GetAbsOrigin(), player->m_vecLadderNormal,
			mv->m_vecVelocity, mv->m_vecViewAngles );
	return bOnLadder;
}

bool CCSGameMovement::OnLadder( trace_t &trace )
{
	if ( trace.plane.normal.z == 1.0f )
		return false;
	return BaseClass::OnLadder( trace );
}

// S23AD70/C22B610 regrabs a nearby ladder from the real standing memo.
void CCSGameMovement::OnFinishWalkMove( bool bOnGround )
{
	if ( bOnGround )
	{
		m_pCSPlayer->RecordLastStandingInfo( mv->GetAbsOrigin(), mv->m_vecVelocity, mv->m_vecViewAngles );
		return;
	}
	if ( mv->GetAbsOrigin().z > m_pCSPlayer->m_lastStandingPosition.z )
		return;
	Vector direction = mv->GetAbsOrigin() - m_pCSPlayer->m_lastStandingPosition;
	if ( direction.x == 0.0f && direction.y == 0.0f )
		return;
	direction.z = 0.0f;
	const float flDistance = VectorNormalize( direction );
	if ( flDistance > 64.0f )
		return;
	const float flReach = flDistance + 5.0f;
	const Vector destination = m_pCSPlayer->m_lastStandingPosition - direction * flReach;
	trace_t trace;
	TracePlayerBBox( mv->GetAbsOrigin(), destination, ( PlayerSolidMask() & 0xDFFEFFFFu ) | 0x20000000u,
		COLLISION_GROUP_PLAYER_MOVEMENT, trace );
	if ( trace.fraction != 1.0f && OnLadder( trace ) && trace.plane.normal.z != 1.0f &&
		m_pCSPlayer->CanRegrabLadder( trace.endpos, trace.plane.normal ) )
	{
		player->SetMoveType( MOVETYPE_LADDER );
		player->SetMoveCollide( MOVECOLLIDE_DEFAULT );
		player->m_vecLadderNormal = trace.plane.normal;
		player->m_ladderSurfaceProps = trace.surface.surfaceProps;
		mv->m_vecVelocity.Init();
		const Vector retry = m_pCSPlayer->m_lastStandingPosition - direction * flReach;
		TracePlayerBBox( mv->GetAbsOrigin(), retry, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace );
		mv->SetAbsOrigin( trace.endpos );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern bool g_bMovementOptimizations;
ConVar punch_angle_decay_rate( "punch_angle_decay_rate", "20", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );

// S239C80/C229660. This is independent of the parameter speed/angle passes.
void CCSGameMovement::CheckDuckUntilOnGround()
{
	if ( m_pCSPlayer->IsDuckUntilOnGround() && !player->GetGroundEntity() )
	{
		if ( mv->m_nButtons & IN_DUCK )
			m_pCSPlayer->SetDuckUntilOnGround( false );
		else
			mv->m_nButtons |= IN_DUCK;
	}
}

// S239D00/C22A250. No call to SDK CheckParameters belongs in any of these passes.
void CCSGameMovement::CheckParameters()
{
	CheckDuckUntilOnGround();
	if ( mv->m_nButtons & IN_SPEED )
		mv->m_flClientMaxSpeed = 100.0f;
	CheckSpeedParameters();
	CheckAngleParameters();
	CheckGroundObstacle();
}


// S238C80/C2298D0. Speed is evaluated exactly once, before angle/punch work.
void CCSGameMovement::CheckSpeedParameters()
{
	const MoveType_t moveType = player->GetMoveType();
	if ( moveType == MOVETYPE_ISOMETRIC || moveType == MOVETYPE_NOCLIP || moveType == MOVETYPE_OBSERVER )
		return;

	const float speedSquared = ( mv->m_flForwardMove * mv->m_flForwardMove +
		mv->m_flSideMove * mv->m_flSideMove ) + mv->m_flUpMove * mv->m_flUpMove;
	if ( mv->m_flClientMaxSpeed != 0.0f )
		mv->m_flMaxSpeed = mv->m_flMaxSpeed <= mv->m_flClientMaxSpeed ? mv->m_flMaxSpeed : mv->m_flClientMaxSpeed;
	float surfaceFactor = 1.0f;
	if ( player->m_pSurfaceData )
		surfaceFactor = player->m_pSurfaceData->game.maxSpeedFactor;
	const double constraintFactor = ComputeConstraintSpeedFactor();
	if ( (double)surfaceFactor > constraintFactor )
		surfaceFactor = (float)constraintFactor;
	mv->m_flMaxSpeed = (float)( (double)GetSpeedModifier() * (double)surfaceFactor * (double)mv->m_flMaxSpeed );

	if ( g_bMovementOptimizations )
	{
		if ( speedSquared != 0.0f && speedSquared > mv->m_flMaxSpeed * mv->m_flMaxSpeed )
		{
			const double ratio = (double)mv->m_flMaxSpeed / sqrt( (double)speedSquared );
			mv->m_flForwardMove = (float)( (double)mv->m_flForwardMove * ratio );
			mv->m_flSideMove = (float)( (double)mv->m_flSideMove * ratio );
			mv->m_flUpMove = (float)( (double)mv->m_flUpMove * ratio );
		}
	}
	else
	{
		const double speedWide = sqrt( (double)speedSquared );
		const float speed = (float)speedWide;
		if ( speedWide != 0.0 && speed > mv->m_flMaxSpeed )
		{
			const float ratio = mv->m_flMaxSpeed / speed;
			mv->m_flForwardMove *= ratio;
			mv->m_flSideMove *= ratio;
			mv->m_flUpMove *= ratio;
		}
	}
}

// S238E60/C229AB0. Native IsDead here is raw health<=0, NOT SDK IsDead's
// health-and-life conjunction. No SDK frozen/train command-clearing pass.
void CCSGameMovement::CheckAngleParameters()
{
	DecayPunchAngle();
	if ( !IsDead() )
	{
		QAngle angles = mv->m_vecAngles;
		const QAngle &punch = player->GetPunchAngle();
		angles.x = punch.x + angles.x;
		angles.y = punch.y + angles.y;
		angles.z = punch.z + angles.z;
		if ( player->GetMoveType() == MOVETYPE_ISOMETRIC || player->GetMoveType() == MOVETYPE_NOCLIP )
			mv->m_vecAngles.z = 0.0f;
		else
		{
			const float rollSpeed = sv_rollspeed.GetFloat();
			const float rollAngle = sv_rollangle.GetFloat();
			mv->m_vecAngles.z = CalcRoll( angles, mv->m_vecVelocity, rollAngle, rollSpeed );
		}
		mv->m_vecAngles.x = angles.x;
		mv->m_vecAngles.y = angles.y;
	}
	else
	{
		mv->m_vecAngles = mv->m_vecOldAngles;
	}
	if ( IsDead() )
		player->SetViewOffset( g_pGameRules->GetViewVectors()->m_vDeadViewHeight );
	mv->m_vecAngles.y = AngleNormalize( mv->m_vecAngles.y );
}

// S238FA0/C229BF0, genuine virtual byte280 base responsibility.
void CCSGameMovement::CheckGroundObstacle()
{
	if ( m_pCSPlayer && ( player->IsObserver() || player->GetMoveType() == MOVETYPE_LADDER ) )
		return;
	int levels = 0;
	CBaseEntity *ground = player->GetGroundEntity();
	if ( !ground )
		return;
	while ( ground->IsPlayer() )
	{
		if ( levels >= 1000 )
			break;
		ground = ground->GetGroundEntity();
		++levels;
		if ( !ground )
			break;
	}
	if ( levels == 1000 )
		Warning( "BUG: CCSGameMovement::CheckParameters - too many stacking levels.\n" );
	else if ( levels <= 1 )
		return;
	mv->m_flForwardMove = mv->m_flMaxSpeed * 3.0f;
	mv->m_flSideMove = 0.0f;
	mv->m_nButtons = 0;
	mv->m_nImpulseCommand = 0;
}

// S239310/C22AF90. Preserve three separately guarded component notifications.
void CCSGameMovement::DecayPunchAngle()
{
	if ( !m_pCSPlayer )
		return;
	Vector direction( m_pCSPlayer->m_Local.m_vecPunchAngle->x, m_pCSPlayer->m_Local.m_vecPunchAngle->y, m_pCSPlayer->m_Local.m_vecPunchAngle->z );
	float length = (float)TerrorPhysicsNormalize( direction ); // Native return spills before decay.
	length -= ( length * 0.5f + punch_angle_decay_rate.GetFloat() ) * gpGlobals->frametime;
	if ( !( length > 0.0f ) ) length = 0.0f; // native COMISS/JBE includes NaN.
	const float pitch = length * direction.x;
	const float yaw = (float)( (double)direction.y * (double)length );
	const float roll = (float)( (double)length * (double)direction.z );
	m_pCSPlayer->m_Local.m_vecPunchAngle.SetX( pitch );
	m_pCSPlayer->m_Local.m_vecPunchAngle.SetY( yaw );
	m_pCSPlayer->m_Local.m_vecPunchAngle.SetZ( roll );
}

// S2398B0/C22A700, reached by native jump setup. This changes only the
// real crouch flags, original view offset and move origin on successful trace.
bool CCSGameMovement::SetCrouchForJump( bool duck )
{
	const Vector view = duck ? g_pGameRules->GetViewVectors()->m_vDuckView : g_pGameRules->GetViewVectors()->m_vView;
	const Vector mins = duck ? g_pGameRules->GetViewVectors()->m_vDuckHullMin : g_pGameRules->GetViewVectors()->m_vHullMin;
	const Vector maxs = duck ? g_pGameRules->GetViewVectors()->m_vDuckHullMax : g_pGameRules->GetViewVectors()->m_vHullMax;
	const Vector origin = mv->GetAbsOrigin() + player->GetViewOffset() - view;
	trace_t trace;
	if ( duck )
	{
		UTIL_TraceHull( origin, origin, mins, maxs, PlayerSolidMask(), player, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.allsolid || trace.startsolid || trace.fraction == 0.0f )
			return false;
		player->AddFlag( FL_DUCKING );
	}
	else
	{
		Vector end = origin;
		end.z -= player->m_Local.m_flStepSize;
		UTIL_TraceHull( origin, end, mins, maxs, PlayerSolidMask(), player, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.allsolid || trace.startsolid || trace.fraction == 1.0f )
			return false;
		UTIL_TraceHull( origin, origin, mins, maxs, PlayerSolidMask(), player, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.allsolid || trace.startsolid || trace.fraction != 1.0f )
			return false;
		player->RemoveFlag( FL_DUCKING );
	}
	player->m_Local.m_bDucked = duck;
	player->m_Local.m_bDucking = false;
	player->m_Local.m_bInDuckJump = false;
	player->m_Local.m_nDuckTimeMsecs = 0;
	player->SetViewOffset( GetPlayerViewOffset( duck ) );
	mv->SetAbsOrigin( origin );
	return true;
}

// S23A6C0/C22AA00. Jump crouching is released on descent, before ground contact.
void CCSGameMovement::Duck()
{
	const int buttonsReleased = ( mv->m_nOldButtons ^ mv->m_nButtons ) & mv->m_nOldButtons;
	const bool inAir = !player->GetGroundEntity() && player->GetMoveType() != MOVETYPE_LADDER;
	const bool inDuck = ( player->GetFlags() & FL_DUCKING ) != 0;
	const bool duckJump = player->m_Local.m_nJumpTimeMsecs > 0;
	const bool duckJumpTime = player->m_Local.m_nDuckJumpTimeMsecs > 0;
	if ( mv->m_nButtons & IN_DUCK )
		mv->m_nOldButtons |= IN_DUCK;
	else
		mv->m_nOldButtons &= ~IN_DUCK;

	if ( IsDead() )
	{
		if ( player->GetFlags() & FL_DUCKING )
			FinishUnDuck();
		return;
	}
	HandleDuckingSpeedCrop();

	if ( inAir )
	{
		if ( mv->m_vecVelocity.z < 0.0f && m_pCSPlayer->IsDuckUntilOnGround() &&
			SetCrouchForJump( false ) )
			m_pCSPlayer->SetDuckUntilOnGround( false );
		return;
	}
	if ( m_pCSPlayer->IsDuckUntilOnGround() )
		m_pCSPlayer->SetDuckUntilOnGround( false );

	if ( !( mv->m_nButtons & IN_DUCK ) && !player->m_Local.m_bDucking &&
		!inDuck && !duckJump && !player->m_Local.m_bDucked )
	{
		if ( !IsDead() && !player->IsObserver() && player->m_Local.m_nDuckJumpTimeMsecs == 0 )
		{
			const Vector viewOffset = GetPlayerViewOffset( player->m_Local.m_bDucked );
			if ( viewOffset.z != player->GetViewOffset().z )
				player->SetViewOffset( viewOffset );
		}
		return;
	}

	if ( ( mv->m_nButtons & IN_DUCK ) || duckJump )
	{
		if ( ( mv->m_nButtons & IN_DUCK ) && !player->m_Local.m_bDucking &&
			!( player->GetFlags() & FL_DUCKING ) )
		{
			player->m_Local.m_nDuckTimeMsecs = 1000;
			player->m_Local.m_bDucking = true;
			OnStartDuck();
		}
		if ( !duckJump )
		{
			if ( player->m_Local.m_bDucking && !duckJumpTime )
			{
				const int elapsed = MAX( 0, (int)( 1000u - (unsigned int)player->m_Local.m_nDuckTimeMsecs ) );
				if ( elapsed > 400 || inDuck || inAir )
					FinishDuck();
				else
				{
					const float fraction = clamp( (float)elapsed * 0.0025f, 0.0f, 1.0f );
					SetDuckedEyeOffset( SimpleSpline( fraction ) );
				}
			}
			return;
		}
		if ( inDuck )
		{
			if ( !( mv->m_nButtons & IN_DUCK ) )
			{
				trace_t trace;
				if ( CanUnDuckJump( trace ) )
				{
					FinishUnDuckJump( trace );
					player->m_Local.m_nDuckJumpTimeMsecs = (int)( ( 1.0f - trace.fraction ) * 200.0f + 800.0f );
				}
			}
		}
		else
		{
			StartUnDuckJump();
		}
		return;
	}

	if ( player->m_Local.m_bInDuckJump )
	{
		trace_t trace;
		if ( CanUnDuckJump( trace ) )
		{
			FinishUnDuckJump( trace );
			if ( trace.fraction < 1.0f )
				player->m_Local.m_nDuckJumpTimeMsecs = (int)( ( 1.0f - trace.fraction ) * 200.0f + 800.0f );
		}
	}
	if ( duckJumpTime )
		return;
	if ( player->m_Local.m_bAllowAutoMovement || inAir ||
		player->m_Local.m_bDucking || player->m_Local.m_bDucked )
	{
		if ( buttonsReleased & IN_DUCK )
		{
			if ( inDuck )
				player->m_Local.m_nDuckTimeMsecs = 1000;
			else if ( player->m_Local.m_bDucking && !player->m_Local.m_bDucked )
			{
				const int elapsed = (int)( 1000u - (unsigned int)player->m_Local.m_nDuckTimeMsecs );
				const float fraction = clamp( (float)elapsed * 0.0025f, 0.0f, 1.0f );
				player->m_Local.m_nDuckTimeMsecs = 800 - (int)( (double)fraction * -200.0 );
			}
		}
		if ( CanUnduck() )
		{
			if ( player->m_Local.m_bDucking || player->m_Local.m_bDucked )
			{
				const int elapsed = MAX( 0, (int)( 1000u - (unsigned int)player->m_Local.m_nDuckTimeMsecs ) );
				if ( elapsed > 200 || inAir )
					FinishUnDuck();
				else
				{
					const float fraction = clamp( (float)elapsed * 0.005f, 0.0f, 1.0f );
					SetDuckedEyeOffset( SimpleSpline( 1.0f - fraction ) );
					player->m_Local.m_bDucking = true;
				}
			}
		}
		else if ( player->m_Local.m_nDuckTimeMsecs != 1000 )
		{
			SetDuckedEyeOffset( 1.0f );
			player->m_Local.m_nDuckTimeMsecs = 1000;
			player->m_Local.m_bDucked = true;
			player->m_Local.m_bDucking = false;
		}
	}
}

// S2390A0/C229FF0. The input crop also applies to airborne ducking.
void CCSGameMovement::HandleDuckingSpeedCrop()
{
	if ( !( m_iSpeedCropped & SPEED_CROPPED_DUCK ) &&
		( ( mv->m_nButtons & IN_DUCK ) || player->m_Local.m_bDucking ||
			( player->GetFlags() & FL_DUCKING ) ) )
	{
		mv->m_flForwardMove *= 0.34f;
		mv->m_flSideMove *= 0.34f;
		mv->m_flUpMove *= 0.34f;
		m_iSpeedCropped |= SPEED_CROPPED_DUCK;
	}
}

// S239480/C22A510. Airborne expansion uses half the hull-size difference.
void CCSGameMovement::FinishUnDuck()
{
	Vector origin = mv->GetAbsOrigin();
	if ( player->GetGroundEntity() )
	{
		const CViewVectors *normal = g_pGameRules->GetViewVectors();
		const CViewVectors *duck = g_pGameRules->GetViewVectors();
		origin += duck->m_vDuckHullMin - normal->m_vHullMin;
	}
	else
	{
		const CViewVectors *normalMin = g_pGameRules->GetViewVectors();
		const CViewVectors *normalMax = g_pGameRules->GetViewVectors();
		const Vector normalSize = normalMax->m_vHullMax - normalMin->m_vHullMin;
		const CViewVectors *duckMin = g_pGameRules->GetViewVectors();
		const CViewVectors *duckMax = g_pGameRules->GetViewVectors();
		const Vector duckSize = duckMax->m_vDuckHullMax - duckMin->m_vDuckHullMin;
		origin += ( normalSize - duckSize ) * -0.5f;
	}
	player->m_Local.m_bDucked = false;
	player->RemoveFlag( FL_DUCKING );
	player->m_Local.m_bDucking = false;
	player->m_Local.m_bInDuckJump = false;
	player->SetViewOffset( GetPlayerViewOffset( false ) );
	player->m_Local.m_nDuckTimeMsecs = 0;
	mv->SetAbsOrigin( origin );
	CategorizePosition( false );
}

// S2396B0/C22A070. Entering crouch retains the full airborne hull delta.
void CCSGameMovement::FinishDuck()
{
	const CViewVectors *normalMin = g_pGameRules->GetViewVectors();
	const CViewVectors *normalMax = g_pGameRules->GetViewVectors();
	const Vector normalSize = normalMax->m_vHullMax - normalMin->m_vHullMin;
	const CViewVectors *duckMin = g_pGameRules->GetViewVectors();
	const CViewVectors *duckMax = g_pGameRules->GetViewVectors();
	const Vector duckSize = duckMax->m_vDuckHullMax - duckMin->m_vDuckHullMin;
	const Vector delta = normalSize - duckSize;
	player->SetViewOffset( GetPlayerViewOffset( true ) );
	player->AddFlag( FL_DUCKING );
	player->m_Local.m_bDucking = false;
	if ( !player->m_Local.m_bDucked )
	{
		Vector origin = mv->GetAbsOrigin();
		if ( player->GetGroundEntity() )
		{
			const CViewVectors *normal = g_pGameRules->GetViewVectors();
			const CViewVectors *duck = g_pGameRules->GetViewVectors();
			origin -= duck->m_vDuckHullMin - normal->m_vHullMin;
		}
		else
		{
			origin += delta;
		}
		mv->SetAbsOrigin( origin );
		player->m_Local.m_bDucked = true;
	}
	FixPlayerCrouchStuck( true );
	CategorizePosition( false );
}

// S239110/C22A310. Trace the standing hull at the same expansion position.
bool CCSGameMovement::CanUnduck()
{
	const Vector start = mv->GetAbsOrigin();
	Vector origin = start;
	if ( player->GetGroundEntity() )
	{
		const CViewVectors *normal = g_pGameRules->GetViewVectors();
		const CViewVectors *duck = g_pGameRules->GetViewVectors();
		origin += duck->m_vDuckHullMin - normal->m_vHullMin;
	}
	else
	{
		const CViewVectors *normalMin = g_pGameRules->GetViewVectors();
		const CViewVectors *normalMax = g_pGameRules->GetViewVectors();
		const Vector normalSize = normalMax->m_vHullMax - normalMin->m_vHullMin;
		const CViewVectors *duckMin = g_pGameRules->GetViewVectors();
		const CViewVectors *duckMax = g_pGameRules->GetViewVectors();
		const Vector duckSize = duckMax->m_vDuckHullMax - duckMin->m_vDuckHullMin;
		origin += ( normalSize - duckSize ) * -0.5f;
	}
	const unsigned int mask = PlayerSolidMask();
	const Vector &maxs = g_pGameRules->GetViewVectors()->m_vHullMax;
	const Vector &mins = g_pGameRules->GetViewVectors()->m_vHullMin;
	trace_t trace;
	UTIL_TraceHull( start, origin, mins, maxs, mask, player, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
	return !trace.startsolid && trace.fraction == 1.0f;
}


#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
