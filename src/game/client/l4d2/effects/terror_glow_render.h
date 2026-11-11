//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_RENDER_H
#define TERROR_GLOW_RENDER_H
#ifdef _WIN32
#pragma once
#endif

class IMaterialSystem;
class IMaterialSystemHardwareConfig;
class CViewSetup;
class C_BaseEntity;
struct ClientModelRenderInfo_t;
struct DrawModelState_t;
struct matrix3x4_t;

// Ordinary native one-argument model submission. Native0x800 suppresses only
// per-entity stencil policy; the target SDK must not receive that raw bit.
// This does NOT enter the unlit/flex/decal glow pass or change GPU stencil,
// material, caller blend or color. Returns the real DrawModel result.
int TerrorModel_DrawModel( C_BaseEntity *pEntity, int nativeFlags );
bool TerrorModel_SkipEntityStencil();

// Native render submission preserves virtual game checks while mapping the
// skip-lighting/flex/stencil/decal flags to the target's exposed draw state.
void TerrorGlow_DrawModel( C_BaseEntity *pEntity, unsigned char instanceAlpha,
	int nativeFlags = 0x10000e01 );
bool TerrorGlow_DrawStudioModel( ClientModelRenderInfo_t *pInfo,
	DrawModelState_t *pState, matrix3x4_t *pBoneToWorld );
bool TerrorGlow_IsModelPass();
void TerrorGlow_Render( const CViewSetup &view, int slot );
void TerrorGlow_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void TerrorGlow_ShutdownRenderTargets();

// GPU stages of C:10273210 / C:102755F0.
void TerrorGlow_BeginColorBuffer( const CViewSetup &view );
void TerrorGlow_EndColorBuffer();

// Main-view silhouette mask: both pass and zfail are REPLACE in the native
// entity/group passes. The viewmodel128 scope independently requests KEEP.
// Entity/group references and write masks are native2/8; test mask is all bits.
void TerrorGlow_BeginStencilMask( int reference, bool replaceOnZFail, unsigned int writeMask = 0xffffffff );
void TerrorGlow_EndStencilMask();

// Requires native colored models in _rt_FullFrameFB and the original native
// stencil mask in the current target. Original material/shader parameters
// MUST exist: this implementation never substitutes a different blur kernel.
void TerrorGlow_Composite( const CViewSetup &view, float blurScale, float compositeScale );

#endif // TERROR_GLOW_RENDER_H
