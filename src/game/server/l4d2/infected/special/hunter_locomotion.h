//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_HUNTER_LOCOMOTION_H
#define L4D2_HUNTER_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/Player/NextBotPlayerLocomotion.h"
class HunterLocomotion : public PlayerLocomotion
{
public:
	explicit HunterLocomotion( INextBot *bot ) : PlayerLocomotion( bot ) {}
	virtual void Reset() OVERRIDE {}
	virtual void Update() OVERRIDE { PlayerLocomotion::Update(); }
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;
};
#endif // L4D2_HUNTER_LOCOMOTION_H
