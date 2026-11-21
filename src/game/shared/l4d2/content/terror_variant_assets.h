//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_VARIANT_ASSETS_H
#define TERROR_VARIANT_ASSETS_H
#pragma once

#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

namespace TerrorVariantAssets
{
struct Model
{
	CUtlString role;
	CUtlString vanilla;
	CUtlString relocated;
};

struct Sound
{
	CUtlString vanilla;
	CUtlString relocated;
};

struct Variant
{
	CUtlString weapon;
	CUtlString key;
	CUtlVector<Model> models;
	CUtlVector<Sound> sounds;
	CUtlString soundScript;
};

bool IsKey( const char *key );
void Rebuild( CUtlVector<Variant> &variants );
void RegisterSoundOverrides( const CUtlVector<Variant> &variants );
}
#endif
