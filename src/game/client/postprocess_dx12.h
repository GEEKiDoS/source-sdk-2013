//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef POSTPROCESS_DX12_H
#define POSTPROCESS_DX12_H
#pragma once

class IMaterialSystem;
class IMaterialSystemHardwareConfig;
class IMatRenderContext;
class ITexture;

struct PostMotionDX12_t
{
	float region[4]; // render width, height, shutter scale, maximum UV length
	float viewModelScale;
};

struct PostGradeDX12_t
{
	float contrast[4]; // center, edge, vignette start and end
	float finish[4]; // grain, phase, top vignette, enabled
	float controller[4]; // x: blurred vignette strength, w: particle mask valid
	float vomitColor1[4]; // RGB tint and refraction scale
	float vomitColor2[4];
	ITexture *pVomit;
	ITexture *pNoise;
	ITexture *pVignette;
};

bool PostProcessDX12_CanFuseGrade( int x, int y, int w, int h, float flBloomScale );
bool PostProcessDX12_HasDOFInput();
void PostProcessDX12_PrepareDOFInput( IMatRenderContext *ctx, ITexture *full, int x, int y, int w, int h, float nearZ, float farZ );

void PostProcessDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
// Main-thread initialization retains all post materials and primes their variable tokens before queued rendering.
void PostProcessDX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void PostProcessDX12_Shutdown();
bool PostProcessDX12_Active();
bool PostProcessDX12_BodycamActive();
// First level init (config.cfg loaded): re-enables mat_postfx_dx12 once for a config saved before it was validated.
void PostProcessDX12_ValidateConfig();
void PostProcessDX12_SubmitFrameConfig();
void PostProcessDX12_Render( IMatRenderContext *pRenderContext, int x, int y, int w, int h, float flBloomScale, bool bColorCorrection,
	float flHorizontalFov, float flAspectRatio, ITexture *pOutput = nullptr, const PostGradeDX12_t *pGrade = nullptr,
	const PostMotionDX12_t *pMotion = nullptr, ITexture *pExposureCopy = nullptr );
int PostProcessDX12_Status();

#endif
