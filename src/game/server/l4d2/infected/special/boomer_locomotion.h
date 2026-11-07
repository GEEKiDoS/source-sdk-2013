//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOOMER_LOCOMOTION_H
#define L4D2_BOOMER_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/Player/NextBotPlayerLocomotion.h"
class BoomerLocomotion : public PlayerLocomotion
{
public:
	explicit BoomerLocomotion( INextBot *bot ) : PlayerLocomotion( bot ) {}
	virtual void Reset() OVERRIDE {} // Native42E310 deliberately does not call base Reset.
	virtual void Update() OVERRIDE { PlayerLocomotion::Update(); }
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;
};
#endif
