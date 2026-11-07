//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOSS_ZOMBIE_RETREAT_SEARCH_H
#define L4D2_BOSS_ZOMBIE_RETREAT_SEARCH_H
#ifdef _WIN32
#pragma once
#endif
#include "nav_pathfind.h"
class BossZombiePlayerBot;
class TerrorNavArea;

// Native 424AE0/424C50: search ordering and its asymmetric tie branch are
// intentional. The surrounding native area traversal owns the parent links.
class BossZombieRetreatSearch
{
public:
	explicit BossZombieRetreatSearch( BossZombiePlayerBot *bot );
	bool operator()( CNavArea *area );
	TerrorNavArea *GetArea() const { return m_area; }
	int GetVisibleSurvivorCount() const { return m_visibleSurvivorCount; }
private:
	BossZombiePlayerBot *m_bot;
	TerrorNavArea *m_area;
	int m_visibleSurvivorCount;
	float m_rangeSquared;
};
#endif // L4D2_BOSS_ZOMBIE_RETREAT_SEARCH_H
