//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 motion-vector render-target and client pass helpers.
//
//===========================================================================//
#include "cbase.h"
#include "motionvectors_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "rendertexture.h"
#include "shaderapi/ishaderapi.h"
#include "iclientrenderable.h"
#include "tier1/interface.h"
#include "renderparm.h"

ConVar r_dx12_motionvectors( "r_dx12_motionvectors", "1", FCVAR_ARCHIVE, "DX12: build _rt_MotionVectors after the opaque pass" );

static CTextureReference s_MotionVectorTexture;
static bool s_bDX12 = false;
static IShaderAPI *s_pShaderAPI = nullptr;
static int s_nMotionVectorFrame = -1;
static double s_flPreviousMainPassTime = -1.0;
static float s_flCurrentMainPassDelta = 0.0f;

void MotionVectorsDX12_RecordMainPassBegin()
{
	const double flNow = Plat_FloatTime();
	s_flCurrentMainPassDelta = ( s_flPreviousMainPassTime >= 0.0 ) ? float( flNow - s_flPreviousMainPassTime ) : 0.0f;
	s_flPreviousMainPassTime = flNow;
	s_nMotionVectorFrame = gpGlobals ? gpGlobals->framecount : -1;
}

void MotionVectorsDX12_CreateRenderTarget( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_bDX12 = shaderDLL && !V_stricmp( shaderDLL, "stdshader_dx12" );
	if ( !s_bDX12 )
		return;

	CreateInterfaceFn f = Sys_GetFactory( "shaderapidx12" );
	s_pShaderAPI = f ? static_cast< IShaderAPI * >( f( SHADERAPI_INTERFACE_VERSION, nullptr ) ) : nullptr;

	s_MotionVectorTexture.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_MotionVectors", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
}

void MotionVectorsDX12_ShutdownRenderTarget()
{
	s_MotionVectorTexture.Shutdown();
	s_nMotionVectorFrame = -1;
	s_flPreviousMainPassTime = -1.0;
	s_flCurrentMainPassDelta = 0.0f;
}

bool MotionVectorsDX12_Enabled()
{
	return s_bDX12 && r_dx12_motionvectors.GetBool() && s_MotionVectorTexture.IsValid() &&
		( !s_pShaderAPI || s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_MOTION_STATUS ) >= 0 );
}

bool MotionVectorsDX12_FrameValid( float *pDeltaSeconds )
{
	if ( pDeltaSeconds )
		*pDeltaSeconds = s_flCurrentMainPassDelta;

	return gpGlobals && s_nMotionVectorFrame == gpGlobals->framecount && MotionVectorsDX12_Enabled();
}

ITexture *MotionVectorsDX12_RenderTarget()
{
	return s_MotionVectorTexture;
}

int MotionVectorsDX12_ObjectKey( IClientRenderable *pRenderable )
{
	if ( !pRenderable )
		return 0;

	IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();
	return pUnknown ? pUnknown->GetRefEHandle().ToInt() : 0;
}
