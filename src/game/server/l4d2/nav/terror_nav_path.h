//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_NAV_PATH_H
#define L4D2_TERROR_NAV_PATH_H
#ifdef _WIN32
#pragma once
#endif

class CNavArea;
class Vector;

// S2943A0: optional actual endpoints alter the first/last floor-edge costs.
// closestArea is an area output. Native -1 team means SDK TEAM_ANY.
bool BuildTerrorShortestPath( CNavArea *start, CNavArea *goal, CNavArea **closestArea = NULL,
	float maxPathLength = 0.0f, int teamID = -1, bool ignoreNavBlockers = false,
	const Vector *startPosition = NULL, const Vector *goalPosition = NULL );

// Original Music S2B1630 sums parent-chain centre distances after that exact
// path query. The cost chooses the path; its crouch/jump penalties are NOT the
// returned physical distance. Same-area returns zero before blocked checks.
float GetTerrorNavTravelDistance( CNavArea *start, CNavArea *goal, float maxPathLength = 0.0f );

#endif // L4D2_TERROR_NAV_PATH_H
