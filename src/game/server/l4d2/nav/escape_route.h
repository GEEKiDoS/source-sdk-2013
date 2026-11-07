//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ESCAPE_ROUTE_H
#define L4D2_ESCAPE_ROUTE_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "l4d2/nav/terror_nav_area.h"

// S:294100/295210, factory escape_route. The original owner is a real
// CBaseEntity, not a Director member or a second NAV endpoint cache.
class CEscapeRoute : public CBaseEntity
{
public:
	DECLARE_CLASS( CEscapeRoute, CBaseEntity );
	DECLARE_DATADESC();
	CEscapeRoute();
	virtual ~CEscapeRoute();
	virtual void Spawn() OVERRIDE; // Original2936C0 is empty.
	virtual void Precache() OVERRIDE; // Original293D70 is empty.
	virtual int UpdateTransmitState() OVERRIDE;

	void Clear(); // S:2936D0, retains the three arrays.
	void AddArea( TerrorNavArea *area ); // S:293D80
	void Finish(); // S:293E90
	void OnNavAreaDestroyed( TerrorNavArea *area ); // S:293D00
	Vector GetPositionAtFlow( float fraction ) const; // S:295230
	double GetPositionAtFlowDistance( float distance, bool interpolate, Vector *position, TerrorFlowDirection direction ) const; // S:2939B0
	TerrorNavArea *GetAreaAtFlowDistance( float distance, TerrorFlowDirection direction ) const; // S:293BF0
	int GetPointIndexAtFlowDistance( float distance, TerrorFlowDirection direction ) const; // S:293C70
	double GetPointFlowDistance( int index, TerrorFlowDirection direction ) const; // S:293CD0
	float GetLength() const { return m_flLength; }

private:
	void AddPoint( const Vector &point, TerrorNavArea *area ); // S:2936F0
	enum { MAX_ROUTE_POINTS = 512 };
	Vector m_points[MAX_ROUTE_POINTS]; // Native1068, reverse goal-to-start order.
	int m_nPointCount; // Native7212.
	float m_flLength; // Native7216, NOT recomputed from the retained samples.
	TerrorNavArea *m_areas[MAX_ROUTE_POINTS]; // Native7220, raw pointers.
	float m_flowDistances[MAX_ROUTE_POINTS][NUM_TERROR_FLOW_DIRECTIONS]; // Native9268.
	TerrorNavArea *m_lastArea; // Native13364; not cleared by area destruction.
};

CEscapeRoute *GetCriticalPath(); // S:295200, actual most-recent constructor owner.
void ClearWanderingPopulationAroundBoss( TerrorNavArea *area ); // S:272CC0.

#endif // L4D2_ESCAPE_ROUTE_H
