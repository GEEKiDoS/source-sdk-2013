//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SURVIVOR_RETREAT_PATH_H
#define L4D2_SURVIVOR_RETREAT_PATH_H
#ifdef _WIN32
#pragma once
#endif

#include "nav.h"
class SurvivorBot;
class CBaseEntity;
class CNavArea;
class CNavLadder;
class ILocomotion;

// Original SurvivorLegsRetreat's bounded adjacency scratch and directed
// retreat search. This is not the generic NextBot RetreatPath policy.
class SurvivorRetreatPathBuilder
{
public:
	SurvivorRetreatPathBuilder( SurvivorBot *me, CBaseEntity *threat, float retreatRange );
	CNavArea *ComputePath();
private:
	void CollectAdjacentAreas( CNavArea *area );
	void AddAdjacentArea( CNavArea *area, CNavLadder *ladder, NavTraverseType how );
	float Cost( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder );
	SurvivorBot *m_me;
	ILocomotion *m_mover;
	Vector m_toThreat;
	CBaseEntity *m_threat;
	float m_retreatRange;
	struct AdjInfo
	{
		CNavArea *area;
		CNavLadder *ladder;
		NavTraverseType how;
	};
	enum { MAX_ADJ_AREAS = 64 };
	AdjInfo m_adjacent[MAX_ADJ_AREAS];
	int m_adjacentCount;
};

#endif
