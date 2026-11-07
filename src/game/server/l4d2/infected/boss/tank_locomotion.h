//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TANK_LOCOMOTION_H
#define L4D2_TANK_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/Player/NextBotPlayerLocomotion.h"
#include "l4d2/terror_shareddefs.h"
class TankLocomotion : public PlayerLocomotion
{
public:
	explicit TankLocomotion( INextBot *bot );
	virtual void Reset() OVERRIDE;
	virtual float GetMaxJumpHeight() const OVERRIDE { return 200.0f; } // S:472160
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *obstacle, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;
	virtual bool IsOnGround() const OVERRIDE;
	virtual bool ClimbUpToLedge( const Vector &landingGoal, const Vector &landingForward, const CBaseEntity *obstacle ) OVERRIDE;
	virtual void OnAnimationActivityComplete( int activity ) OVERRIDE;
	virtual void OnAnimationActivityInterrupted( int activity ) OVERRIDE;
};
bool IsTankClimbingActivity( Activity activity ); // native2C76C0, translated original domain
bool IsTankMotionControlledActivity( Activity activity ); // native2C75D0
#endif
