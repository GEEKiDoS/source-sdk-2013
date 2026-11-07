//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef INFO_CHANGELEVEL_H
#define INFO_CHANGELEVEL_H
#ifdef _WIN32
#pragma once
#endif
#include "triggers.h"
#include "util_shared.h"
class CTerrorPlayer;
class CTerrorCheckpoint;

// Retail InfoChangelevel is independent of CChangeLevel/trigger_changelevel.
class InfoChangelevel : public CBaseTrigger
{
public:
	DECLARE_CLASS( InfoChangelevel, CBaseTrigger );
	DECLARE_DATADESC();
	InfoChangelevel();
	virtual bool KeyValue( const char *key, const char *value ) OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void Activate() OVERRIDE;
	bool IsOutward() const;
	const char *GetMapName() const { return m_mapName; }
	const char *GetLandmarkName() const { return m_landmarkName; }
	void InputCheckpointDoorOpened( inputdata_t &data );
	void InputCheckpointDoorClosed( inputdata_t &data );
private:
	struct CheckpointPlayers
	{
		CheckpointPlayers();
		CUtlVector< CTerrorPlayer * > inside;
		CUtlVector< CTerrorPlayer * > outside;
		bool infectedInside;
		bool survivorOutside;
		bool humanInside;
	};
	void TouchChangeLevel( CBaseEntity *other );
	void ChangelevelThink();
	void ChangeLevelSpeechDelay();
	void ChangeLevelNow();
	void CollectCheckpointPlayers( CheckpointPlayers &players, bool includeNextBots );
	void CollectCheckpointPlayer( CheckpointPlayers &players, CBaseCombatCharacter *actor );
	void UpdateCheckpoint( const CheckpointPlayers &players );
	void BeginChangeLevel( CTerrorCheckpoint *checkpoint );
	char m_mapName[32];
	char m_landmarkName[32];
	bool m_changeLevelStarted;             // S+1432
	bool m_checkpointDoorOpen;             // S+1433
	CountdownTimer m_speechTimer;          // S+1436
	COutputEvent m_OnChangeLevel;          // S+1448; not exposed by retail datadesc
};

// S:253590/2535A0/2536A0. This state survives a level change and is reset by
// the real new-session/transition cleanup callers, never by an entity ctor.
const char *GetTerrorTransitionLandmarkName();
void SetTerrorTransitionLandmark( const char *name, const Vector &origin );
void ClearTerrorTransitionLandmark();
Vector GetTerrorTransitionOffset();
void FixupTerrorTransitionPosition( Vector &origin, QAngle &angles );
void ChangeTerrorLevel( const char *map, const char *landmark );
#endif // INFO_CHANGELEVEL_H
