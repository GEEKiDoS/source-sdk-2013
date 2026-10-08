//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 motion-vector render-target and client pass helpers.
//
//===========================================================================//
#include "cbase.h"
#include "motionvectors_dx12.h"
#include "upscaler_dx12.h"
#include "framegen_dx12.h"
#include "iviewrender.h"
#include "ScreenSpaceEffects.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "rendertexture.h"
#include "shaderapi/ishaderapi.h"
#include "iclientrenderable.h"
#include "tier1/interface.h"
#include "renderparm.h"

ConVar r_motionvectors( "r_motionvectors", "1", FCVAR_ARCHIVE,
	"DX12 motion vectors: 0 off, 1 automatic for SDK/installed consumers, 2 force for unsupported custom map/VGUI readers" );

static CTextureReference s_MotionVectorTexture;
static bool s_bDX12 = false;
static IShaderAPI *s_pShaderAPI = nullptr;
static int s_nMotionVectorFrame = -1;
static int s_nConsumerFrame = -1;
static bool s_bFrameHasConsumer = false;
static int s_nPreviousMainPassFrame = -1;
static double s_flPreviousMainPassTime = -1.0;
static float s_flCurrentMainPassDelta = 0.0f;

class CMotionVectorsDX12GameSystem : public CAutoGameSystem
{
public:
	CMotionVectorsDX12GameSystem() : CAutoGameSystem( "CMotionVectorsDX12GameSystem" ) {}
	virtual void LevelInitPostEntity() { ResetHistory(); }
	virtual void LevelShutdownPostEntity() { ResetHistory(); }
private:
	static void ResetHistory()
	{
		s_nConsumerFrame = s_nMotionVectorFrame = s_nPreviousMainPassFrame = -1;
		s_bFrameHasConsumer = false;
		s_flPreviousMainPassTime = -1.0;
		s_flCurrentMainPassDelta = 0.0f;
	}
};
static CMotionVectorsDX12GameSystem s_MotionVectorsDX12GameSystem;

void MotionVectorsDX12_RecordMainPassBegin()
{
	const double flNow = Plat_FloatTime();
	const int nFrame = gpGlobals ? gpGlobals->framecount : -1;
	s_flCurrentMainPassDelta = ( nFrame >= 0 && s_nPreviousMainPassFrame == nFrame - 1 && s_flPreviousMainPassTime >= 0.0 ) ?
		float( flNow - s_flPreviousMainPassTime ) : 0.0f;
	s_nPreviousMainPassFrame = nFrame;
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
	s_bDX12 = false;
	s_pShaderAPI = nullptr;
	s_nConsumerFrame = s_nPreviousMainPassFrame = -1;
	s_bFrameHasConsumer = false;
	s_nMotionVectorFrame = -1;
	s_flPreviousMainPassTime = -1.0;
	s_flCurrentMainPassDelta = 0.0f;
}


static bool HasCustomMotionVectorReader()
{
	// Inspectors are engine-owned and can bind their selected target only after the 3D pass.
	// Do not infer "unused" from last frame's backend texture-bind counts.
	static ConVarRef mat_texture_list( "mat_texture_list", true );
	static ConVarRef mat_drawTexture( "mat_drawTexture", true );
	static ConVarRef cl_drawmaterial( "cl_drawmaterial", true );
	if ( ( mat_texture_list.IsValid() && mat_texture_list.GetBool() ) ||
		( mat_drawTexture.IsValid() && mat_drawTexture.GetString()[0] ) ||
		( cl_drawmaterial.IsValid() && cl_drawmaterial.GetString()[0] ) )
		return true;

	// Overlay inputs/proxies can change at their late bind; any active overlay is conservative.
	if ( view && view->GetScreenOverlayMaterial() )
		return true;

	// Unclassified registered effects conservatively keep motion live when active.
	// SDK effects can declare no motion input independently of their enable state.
	for ( CScreenSpaceEffectRegistration *pReg = CScreenSpaceEffectRegistration::s_pHead; pReg; pReg = pReg->m_pNext )
	{
		if ( pReg->m_pEffect && pReg->m_pEffect->RequiresMotionVectors() && pReg->m_pEffect->IsEnabled() )
			return true;
	}
	return false;
}

void MotionVectorsDX12_BeginFrame( bool bVelocityBlurEligible )
{
	s_nConsumerFrame = gpGlobals ? gpGlobals->framecount : -1;
	// Capture eligibility before either provider dispatch consumes its latch. Enabled is never
	// used to choose a provider, and later blur/FrameValid queries retain this RenderView's latch.
	s_bFrameHasConsumer = s_bDX12 && r_motionvectors.GetBool() && s_MotionVectorTexture.IsValid() &&
		( r_motionvectors.GetInt() >= 2 || UpscalerDX12_RequiresMotionVectors() || FrameGenDX12_RequiresMotionVectors() ||
		  bVelocityBlurEligible || HasCustomMotionVectorReader() );
	if ( !s_bFrameHasConsumer )
	{
		s_nMotionVectorFrame = -1;
		s_flCurrentMainPassDelta = 0.0f;
	}
}

bool MotionVectorsDX12_Enabled()
{
	// Automatic mode covers verified SDK/installed readers and conservative active debug paths.
	// Arbitrary custom map/VGUI/material retargeting has no observable input epoch: force mode
	// is required for those unsupported readers. Never expose a previous frame's consumer latch.
	return gpGlobals && s_nConsumerFrame == gpGlobals->framecount && s_bFrameHasConsumer &&
		s_bDX12 && r_motionvectors.GetBool() && s_MotionVectorTexture.IsValid() &&
		( !s_pShaderAPI || s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_MOTION_STATUS ) >= 0 );
}

bool MotionVectorsDX12_FrameValid( float *pDeltaSeconds )
{
	const bool bValid = gpGlobals && s_nMotionVectorFrame == gpGlobals->framecount && MotionVectorsDX12_Enabled();
	if ( pDeltaSeconds )
		*pDeltaSeconds = bValid ? s_flCurrentMainPassDelta : 0.0f;
	return bValid;
}

CON_COMMAND_F( r_motionvectors_report, "Report DX12 motion consumer/producer frame latches (diagnostic only)", FCVAR_CHEAT )
{
	float flDelta = 0.0f;
	const bool bValid = MotionVectorsDX12_FrameValid( &flDelta );
	Msg( "DX12 motion latch: mode %d frame %d consumerFrame %d consumer %d producedFrame %d enabled %d valid %d delta %.6f upscaler %d framegen %d custom %d status %d\n",
		r_motionvectors.GetInt(), gpGlobals ? gpGlobals->framecount : -1, s_nConsumerFrame, s_bFrameHasConsumer,
		s_nMotionVectorFrame, MotionVectorsDX12_Enabled(), bValid, flDelta, UpscalerDX12_RequiresMotionVectors(),
		FrameGenDX12_RequiresMotionVectors(), HasCustomMotionVectorReader(),
		s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_MOTION_STATUS ) : 0 );
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
