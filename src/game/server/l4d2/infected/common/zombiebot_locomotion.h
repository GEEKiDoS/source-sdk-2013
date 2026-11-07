//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ZOMBIEBOT_LOCOMOTION_H
#define L4D2_ZOMBIEBOT_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotGroundLocomotion.h"

class Infected;

// The native ground locomotor supplies hull collision, ground constraints and
// PathFollower integration. Its L4D2 subclass retains the original governor and
// accumulation rules (server RVAs 0x3F8BD0, 0x3F8F50 and 0x3FC4B0).
class ZombieBotLocomotion : public NextBotGroundLocomotion
{
public:
	DECLARE_CLASS( ZombieBotLocomotion, NextBotGroundLocomotion );

	explicit ZombieBotLocomotion( Infected *infected );
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual bool ClimbUpToLedge( const Vector &landingGoal, const Vector &landingForward, const CBaseEntity *obstacle ) OVERRIDE;
	virtual void JumpAcrossGap( const Vector &landingGoal, const Vector &landingForward ) OVERRIDE;
	virtual void Jump() OVERRIDE;
	virtual void FaceTowards( const Vector &target ) OVERRIDE;
	virtual void OnAnimationActivityComplete( int activity ) OVERRIDE;
	virtual float GetRunSpeed() const OVERRIDE;
	virtual float GetWalkSpeed() const OVERRIDE;
	virtual float GetMaxAcceleration() const OVERRIDE;
	virtual float GetMaxDeceleration() const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;

protected:
	virtual float GetGravity() const OVERRIDE;
	virtual float GetFrictionForward() const OVERRIDE;
	virtual float GetFrictionSideways() const OVERRIDE;
	virtual float GetMaxYawRate() const OVERRIDE;

private:
	void ApplyAccumulatedApproach();
	bool TraverseLadder();
};

#endif // L4D2_ZOMBIEBOT_LOCOMOTION_H
