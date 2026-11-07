//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game-owned preparation of unchanged mounted L4D2 materials.
//
//=============================================================================//

#ifndef TERROR_MATERIALS_H
#define TERROR_MATERIALS_H
#ifdef _WIN32
#pragma once
#endif

struct model_t;
class IMaterial;

// Pair at FRAME_NET_UPDATE_START/END. Reuse the existing nested material batch
// to coalesce model preparation; particle material admission remains synchronous.
void Terror_BeginNetworkMaterialPreparation();
void Terror_EndNetworkMaterialPreparation();

// Call on the main thread when a gameplay model is assigned, before its first
// draw or material-proxy bind. All skins are enumerated, not just the active
// skin. The existing IMaterial objects and mounted files are not replaced.
// False means that a reached material could not be prepared; it is not a
// declaration of shader/proxy support. The game system prepares loaded world/
// static-prop materials at LevelInitPreEntity and re-prepares on GPU, DX bucket
// or native resource invalidation, retaining particle overrides. Pending trees
// commit under the material-system lock without a global resource reload.
// Inside a network cohort, true can mean queued; its end completes preparation
// before rendering, after entity updates and absolute-query suppression end.
bool Terror_PrepareModelMaterials( const model_t *pModel );

// Prepare the actual indexed parent and its authored Subrect model companion
// before submitting a studio/static-prop decal, including inside a network batch.
bool Terror_PrepareDecalMaterial( int nDecalIndex );

// First use of a reached SpriteCard particle material by the L4D2 renderer.
// Shader-name consumers require this preparation to finish before return.
bool Terror_PrepareParticleMaterial( IMaterial *pMaterial, const char *pShaderOverride );

#endif // TERROR_MATERIALS_H
