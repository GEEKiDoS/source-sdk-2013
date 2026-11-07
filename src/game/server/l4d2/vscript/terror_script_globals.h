//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SCRIPT_GLOBALS_H
#define TERROR_SCRIPT_GLOBALS_H
#ifdef _WIN32
#pragma once
#endif
#include "vscript/ivscript.h"

void Script_GetInfectedStats( HSCRIPT table );
void Script_StartAssault();
bool Script_ZSpawn( HSCRIPT table );
const char *Script_GetCharacterDisplayName( HSCRIPT entity );
HSCRIPT Script_GetPlayerFromCharacter( int character );
float Script_GetCurrentFlowDistanceForPlayer( HSCRIPT entity );
float Script_GetCurrentFlowPercentForPlayer( HSCRIPT entity );
float Script_GetFlowDistanceForPosition( Vector position );
float Script_GetFlowPercentForPosition( Vector position, bool fromStart );
float Script_GetMaxFlowDistance();
float Script_GetAverageSurvivorFlowDistance();
void Script_GetInvTable( HSCRIPT entity, HSCRIPT table );
void Script_RestoreTable( const char *name, HSCRIPT table );
void Script_SaveTable( const char *name, HSCRIPT table );
void Script_ClearSavedTables();
void Script_DropSpit( Vector position );
void Script_DropFire( Vector position );
void Script_LoadMOTD();
bool Script_CommandABot( HSCRIPT table );
void Script_ReapplyInfectedFlags( int flags, HSCRIPT entity );
bool Script_HasPlayerControlledZombies();
void Script_ForceVersusStart();
void Script_ForceSurvivalStart();
void Script_RushVictim( HSCRIPT entity, float range );
bool Script_IsMissionFinalMap();
HSCRIPT Script_FindRescueAreaTrigger();
int Script_GetDifficulty();
const char *Script_GetDifficultyString();
int Script_GetScavengeItemsGoal();
int Script_GetScavengeItemsRemaining();
void Script_SetScavengeItemsGoal( int count );
void Script_SetScavengeItemsRemaining( int count );
bool Script_HasConfigurableDifficultySetting();

// Existing native registrations whose SDK names had different/dead bodies.
// These are real native bodies, not aliases to the SDK helpers.
bool Script_TraceLine( HSCRIPT table );
void Script_QueueSpeak( HSCRIPT entity, const char *concept, float delay, const char *context );
#endif
