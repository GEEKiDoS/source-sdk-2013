//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_MODE_H
#define TERROR_MODE_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/KeyValues.h"
#include "tier1/utldict.h"
class IConVar;

// Mounted mode metadata, not the text of mp_gamemode. The matchmaking service
// owns this dictionary in retail. This game-owned reader uses the same resources.
class CTerrorModeDescription
{
public:
	CTerrorModeDescription();
	~CTerrorModeDescription();
	bool Init();
	bool Reload(); // Native mission_reload: dictionary only, not mounts/convars.
	void InitMissions();
	void ShutdownMissions();
	void InheritAllMissionData();
	void Shutdown();
	KeyValues *FindMode( const char *name ) const;
	KeyValues *GetCurrentMode() const;
	KeyValues *GetModeDictionary() const { return m_pModes; } // Borrowed real mounted metadata; native constructor's all-mode traversal.
	KeyValues *GetMissionDictionary() const { return m_pMissions; }
	KeyValues *FindMission( const char *name ) const { return m_pMissions ? m_pMissions->FindKey( name ) : NULL; }
	KeyValues *GetChapterInfo( KeyValues *settings, KeyValues **mission ) const;
	KeyValues *GetChapterInfoByBspName( KeyValues *settings, const char *mapName, KeyValues **mission ) const;
	const char *GetName() const;
	const char *GetBaseName() const;
	bool IsBaseMode( const char *base ) const;
	bool IsPlayerControlledZombies() const;
	bool HasDifficulty() const;
	void OnModeChanged( IConVar *var, const char *oldValue );

private:
	void LoadMissionFile( const char *path, bool builtIn, uint64 workshopID );
	void InheritMissionData( const char *source, const char *destination );
	void RevertConVars( KeyValues *mode, const char *name );
	void ApplyConVars( KeyValues *mode, const char *name );
	KeyValues *m_pModes;
	KeyValues *m_pMissions;
	CUtlDict< KeyValues *, unsigned short > m_MissionFiles;
	bool m_bChangingMode;
	bool m_bCallbackInstalled;
};
extern CTerrorModeDescription g_TerrorModes;

void TerrorGameModeChanged( IConVar *var, const char *oldValue, float oldFloat );
#ifndef CLIENT_DLL
void TerrorDifficultyChanged( IConVar *var, const char *oldValue, float oldFloat );
#endif
#endif
