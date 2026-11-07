//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_BOT_PATH_COST_H
#define SURVIVOR_BOT_PATH_COST_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/Path/NextBotPath.h"
class SurvivorBot;
class ILocomotion;

class SurvivorBotPathCost : public IPathCost
{
public:
	explicit SurvivorBotPathCost( SurvivorBot *bot, bool shortestPath = false );
	virtual float operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder, const CFuncElevator *elevator, float length ) const OVERRIDE;

private:
	SurvivorBot *m_bot;
	ILocomotion *m_locomotion;
	bool m_allowDamagingAreas;
	bool m_shortestPath;
};

#endif // SURVIVOR_BOT_PATH_COST_H
