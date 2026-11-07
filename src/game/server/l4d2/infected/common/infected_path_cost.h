//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INFECTED_PATH_COST_H
#define L4D2_INFECTED_PATH_COST_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/Path/NextBotPath.h"

class INextBot;
class ILocomotion;
class CTerrorPlayer;

// S:0x3FE2F0/0x3FE430. The same cost is used by common infected, Witch,
// and the player-backed special infected; it is not a generic shortest path.
class InfectedPathCost : public IPathCost
{
public:
	explicit InfectedPathCost( INextBot *bot );
	virtual float operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder,
		const CFuncElevator *elevator, float length ) const OVERRIDE;

private:
	INextBot *m_bot;
	CTerrorPlayer *m_player;
	ILocomotion *m_locomotion;
	float m_actorRandomValue;
	int m_zombieClass;
};

#endif // L4D2_INFECTED_PATH_COST_H
