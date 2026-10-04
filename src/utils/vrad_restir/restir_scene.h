//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_SCENE_H
#define RESTIR_SCENE_H
#pragma once

#include "restir_types.h"

class CReSTIRSceneBuilder
{
public:
	// Builds the deterministic CPU scene consumed by the Vulkan backend.
	bool Build( const ReSTIROptions &options, ReSTIRScene &scene );
};

// Returns the material index, creating it on first use. pOutAlphaTested is
// true only when material alpha can affect visibility under textureShadows.
int ReSTIR_GetOrAddMaterial( ReSTIRScene &scene, const char *pMaterialName,
	const Vector &reflectivity, bool textureShadows, bool *pOutAlphaTested );
// Enables per-texel bounce albedo on an already registered lit material (-restir_texturealbedo).
void ReSTIR_LoadMaterialAlbedo( ReSTIRScene &scene, int materialIndex, const char *pMaterialName, int textureWidth, int textureHeight );

#endif // RESTIR_SCENE_H
