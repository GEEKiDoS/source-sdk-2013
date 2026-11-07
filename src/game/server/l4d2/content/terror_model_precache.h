//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_MODEL_PRECACHE_H
#define L4D2_TERROR_MODEL_PRECACHE_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"
#include "tier1/utlstring.h"

class CBaseAnimating;
class CTerrorPlayer;
class KeyValues;

// S:0x30EA30. Chapter metadata owns the optional variant; the caller owns the
// ordered model list and clears it at the original precache lifecycle point.
void PrecachePlayerModelAndMaterials( CBaseAnimating *actor, const char *model,
	const char *chapterVariant, CUtlVector< CUtlString > *models = NULL );

// S3198C0 model-catalog lifecycle. The sole30EA30 helper above populates it.
void PrecacheTerrorPlayerModels( CTerrorPlayer *actor );
const CUtlVector< CUtlString > &GetTerrorInfectedPlayerModels( int zombieClass );
int GetCurrentTerrorSurvivorSet(); // uncached2DBAE0; GetSurvivorSet is cached2DBB60
int GetTerrorSurvivorCharacterForName( const char *name );
int GetTerrorSurvivorCharacterForModel( const char *model );
const char *GetTerrorSurvivorModel( int character );
const char *GetTerrorSurvivorModelForCharacter( int character );
const char *GetTerrorDefaultPlayerModel();
const char *GetSurvivorBotName( int character );

#endif // L4D2_TERROR_MODEL_PRECACHE_H
