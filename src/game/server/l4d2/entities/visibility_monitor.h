//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_VISIBILITY_MONITOR_H
#define L4D2_VISIBILITY_MONITOR_H
#ifdef _WIN32
#pragma once
#endif

class CBaseEntity;
class CBasePlayer;

// Predicate false leaves the observer unmarked. Callback false suppresses
// entity_visible, but still marks that observer as having seen the entity.
typedef bool ( *VisibilityMonitorCallback_t )( CBaseEntity *pEntity, CBasePlayer *pObserver );

void VisibilityMonitor_AddEntity( CBaseEntity *pEntity, float flRange,
	VisibilityMonitorCallback_t pCallback = NULL, VisibilityMonitorCallback_t pPredicate = NULL );
void VisibilityMonitor_RemoveEntity( CBaseEntity *pEntity );

#endif // L4D2_VISIBILITY_MONITOR_H
