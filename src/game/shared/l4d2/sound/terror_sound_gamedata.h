//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SOUND_GAMEDATA_H
#define TERROR_SOUND_GAMEDATA_H
#ifdef _WIN32
#pragma once
#endif

#include "SoundEmitterSystem/isoundemittersystembase.h"

class KeyValues;
class IFileList;

// Original VSoundEmitter003 actor/macro semantics projected through the SDK's
// existing registry. Model lookup reuses the mounted global_actors owner.
gender_t GetTerrorSoundActorGender( const char *actorModel );
void ExpandTerrorSoundActorString( gender_t gender, const char *input, char *output, int outputBytes );

// UNAVAILABLE is not an entry with no GameData. It means the native-selected
// script could not be recovered, or a native mutation has not been committed.
enum SoundGameDataResult_t
{
	SOUND_GAMEDATA_UNAVAILABLE,
	SOUND_GAMEDATA_ABSENT,
	SOUND_GAMEDATA_PRESENT
};

// Lookup only: no file reads, parsing, random sound selection or allocation.
// The handle is the one actually used by GetParametersForSoundEx, not a fresh
// lookup of the caller's name (the native emitter can use a cached handle).
SoundGameDataResult_t GetSoundGameData( HSOUNDSCRIPTHANDLE handle, KeyValues **pGameData );
KeyValues *GetSoundScriptDefinition( const char *soundName );
KeyValues *GetSoundScriptGameData( const char *soundName );
KeyValues *GetSoundScriptDefinitions( const char *scriptPath );

// Only startup/bank precaching may explicitly load a fixed script. This does
// not add it to the native sound registry or change native sound selection.
// reload retries the raw bank script; native handle GameData retains its
// existing registry lifecycle and references to its own metadata snapshots.
bool CacheSoundScriptDefinitions( const char *scriptPath, bool reload = false );

// CSoundEmitterSystem owns these calls, on BOTH sides, around its existing
// native registry operations. All returned pointers are borrowed and expire
// at the next registry mutation. Consumers must copy retained music fields.
void InitSoundGameData();                         // after successful ModInit/Flush
void PurgeSoundGameData();                        // before ModShutdown/Flush
void InvalidateSoundGameDataHandles();            // before Add/ClearSoundOverrides
void RefreshSoundGameDataHandles();               // after the override phase
void AddSoundGameDataOverrides( const char *scriptPath ); // after native AddSoundOverrides
void ClearSoundGameDataOverrides();               // after native ClearSoundOverrides
void BeginSoundGameDataReload( IFileList *pFiles ); // before native reload
void EndSoundGameDataReload();                    // after native reload

#endif // TERROR_SOUND_GAMEDATA_H
