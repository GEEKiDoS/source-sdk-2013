//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_TRANSITION_ENTITIES_H
#define TERROR_TRANSITION_ENTITIES_H
#ifdef _WIN32
#pragma once
#endif
class InfoChangelevel;
// Original globals83B020/34/48 own these records across maps independently
// of Director's saved-player KV records and admission manager.
void SaveTerrorTransitionEntities( InfoChangelevel *changelevel );
void PrecacheTerrorTransitionEntities();
void RestoreTerrorTransitionEntities();
void SetTerrorTransitionEntityRestorePending( bool pending ); // S2539D0, SAME83AF70.
void ClearTerrorTransitionEntities();
#endif
