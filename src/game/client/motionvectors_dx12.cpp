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
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "rendertexture.h"
#include "shaderapi/ishaderapi.h"
#include "iclientrenderable.h"
#include "tier1/interface.h"
#include "renderparm.h"

ConVar r_motionvectors( "r_motionvectors", "1", FCVAR_ARCHIVE,
	"DX12 motion vectors: 0 off, 1 automatic for SDK/installed consumers, 2 force for unsupported custom map/VGUI readers" );

static CTextureReference s_MotionVectorTexture;
static CTextureReference s_TileRaw;
static CTextureReference s_TileDilated;
static IMaterial *s_pTileReduce = nullptr;
static IMaterial *s_pTileDilate = nullptr;
static bool s_bTilesReady = false;
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
	s_TileRaw.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_MotionBlurTileRaw", 512, 512, RT_SIZE_LITERAL,
		IMAGE_FORMAT_R32F, MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_TileDilated.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_MotionBlurTiles", 512, 512, RT_SIZE_LITERAL,
		IMAGE_FORMAT_R32F, MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	if ( !s_pTileReduce )
	{
		KeyValues *pReduce = new KeyValues( "DX12_MotionBlurTile" );
		pReduce->SetInt( "$pass", 0 );
		s_pTileReduce = pMaterialSystem->CreateMaterial( "dx12/motion_blur_tile_reduce", pReduce );
		KeyValues *pDilate = new KeyValues( "DX12_MotionBlurTile" );
		pDilate->SetInt( "$pass", 1 );
		s_pTileDilate = pMaterialSystem->CreateMaterial( "dx12/motion_blur_tile_dilate", pDilate );
	}
	s_bTilesReady = s_TileRaw.IsValid() && s_TileDilated.IsValid() &&
		s_pTileReduce && !s_pTileReduce->IsErrorMaterial() && s_pTileDilate && !s_pTileDilate->IsErrorMaterial();
}

void MotionVectorsDX12_ShutdownRenderTarget()
{
	s_MotionVectorTexture.Shutdown();
	s_TileRaw.Shutdown();
	s_TileDilated.Shutdown();
	if ( s_pTileReduce )
	{
		s_pTileReduce->DecrementReferenceCount();
		s_pTileReduce = nullptr;
	}
	if ( s_pTileDilate )
	{
		s_pTileDilate->DecrementReferenceCount();
		s_pTileDilate = nullptr;
	}
	s_bTilesReady = false;
	s_bDX12 = false;
	s_pShaderAPI = nullptr;
	s_nConsumerFrame = s_nPreviousMainPassFrame = -1;
	s_bFrameHasConsumer = false;
	s_nMotionVectorFrame = -1;
	s_flPreviousMainPassTime = -1.0;
	s_flCurrentMainPassDelta = 0.0f;
}

static void SetTileTexture( IMaterial *pMaterial, const char *pName, ITexture *pTexture )
{
	bool bFound = false;
	if ( IMaterialVar *pVar = pMaterial->FindVar( pName, &bFound, false ) )
		pVar->SetTextureValue( pTexture );
}

static void SetTileVec( IMaterial *pMaterial, const char *pName, const float *pValue )
{
	bool bFound = false;
	if ( IMaterialVar *pVar = pMaterial->FindVar( pName, &bFound, false ) )
		pVar->SetVecValue( pValue, 4 );
}

static void SetTileFloat( IMaterial *pMaterial, const char *pName, float flValue )
{
	bool bFound = false;
	if ( IMaterialVar *pVar = pMaterial->FindVar( pName, &bFound, false ) )
		pVar->SetFloatValue( flValue );
}

bool MotionVectorsDX12_BuildBlurTiles( IMatRenderContext *pRenderContext, float flMaxBlurLength, int &nTileCountX, int &nTileCountY )
{
	nTileCountX = nTileCountY = 0;
	if ( !s_bTilesReady || !pRenderContext || !s_MotionVectorTexture.IsValid() )
		return false;
	const int nVelW = s_MotionVectorTexture->GetActualWidth();
	const int nVelH = s_MotionVectorTexture->GetActualHeight();
	if ( nVelW <= 0 || nVelH <= 0 )
		return false;
	const int nTileX = ( nVelW + 15 ) / 16;
	const int nTileY = ( nVelH + 15 ) / 16;
	if ( nTileX > 512 || nTileY > 512 )
		return false;
	const float flSearch = MAX( 0.5f * flMaxBlurLength, 3.f / MIN( nVelW, nVelH ) );
	const float flSizes[4] = { static_cast<float>( nVelW ), static_cast<float>( nVelH ), static_cast<float>( nTileX ), static_cast<float>( nTileY ) };
	SetTileTexture( s_pTileReduce, "$velocity", s_MotionVectorTexture );
	SetTileTexture( s_pTileReduce, "$tileout", s_TileRaw );
	SetTileVec( s_pTileReduce, "$sizes", flSizes );
	SetTileTexture( s_pTileDilate, "$tilein", s_TileRaw );
	SetTileTexture( s_pTileDilate, "$tileout", s_TileDilated );
	SetTileVec( s_pTileDilate, "$sizes", flSizes );
	SetTileFloat( s_pTileDilate, "$radius", flSearch );
	pRenderContext->DrawScreenSpaceRectangle( s_pTileReduce, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
	pRenderContext->DrawScreenSpaceRectangle( s_pTileDilate, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
	nTileCountX = nTileX;
	nTileCountY = nTileY;
	return true;
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
