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
#include "materialsystem/MaterialSystemUtil.h"
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
enum MotionTileVar_t
{
	TILE_INPUT,
	TILE_OUTPUT,
	TILE_SIZES,
	TILE_RADIUS,
	TILE_VAR_COUNT
};

static const char *const s_TileReduceVarNames[] = { "$velocity", "$tileout", "$sizes" };
static const char *const s_TileDilateVarNames[] = { "$tilein", "$tileout", "$sizes", "$radius" };

struct MotionTileMaterial_t
{
	CMaterialReference m_Material;
	const char *const *m_pVarNames = nullptr;
	unsigned int m_nVarTokens[TILE_VAR_COUNT] = {};
};

static MotionTileMaterial_t s_TileReduceMaterial;
static MotionTileMaterial_t s_TileDilateMaterial;
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

static bool TileMaterialValid( MotionTileMaterial_t &material )
{
	return material.m_Material.IsValid() && !material.m_Material->IsErrorMaterial();
}

static void CreateTileMaterial( MotionTileMaterial_t &material, IMaterialSystem *pMaterialSystem,
	const char *pName, KeyValues *pKeyValues, const char *const *pVarNames, int nVarCount )
{
	IMaterial *pMaterial = pMaterialSystem->CreateMaterial( pName, pKeyValues );
	material.m_Material.Init( pMaterial );
	if ( pMaterial )
		pMaterial->DecrementReferenceCount();
	material.m_pVarNames = pVarNames;
	if ( !TileMaterialValid( material ) )
		return;
	Assert( nVarCount <= TILE_VAR_COUNT );
	// Render-target initialization runs on the main thread. Seed every required symbol
	// before BuildBlurTiles executes in ordered replay; unseeded lookups race FindSymbol.
	for ( int i = 0; i < nVarCount; ++i )
	{
		IMaterialVar *pVar = material.m_Material->FindVarFast( pVarNames[i], &material.m_nVarTokens[i] );
		AssertMsg1( pVar, "Missing required DX12 motion tile variable %s", pVarNames[i] );
		(void)pVar;
	}
}

static IMaterialVar *GetTileVar( MotionTileMaterial_t &material, MotionTileVar_t var )
{
	Assert( TileMaterialValid( material ) );
	Assert( material.m_nVarTokens[var] != 0 );
	// Revalidate the material's parameter index after reloads, retaining only the
	// main-primed symbol token rather than a potentially stale IMaterialVar pointer.
	IMaterialVar *pVar = material.m_Material->FindVarFast( material.m_pVarNames[var], &material.m_nVarTokens[var] );
	AssertMsg1( pVar, "Missing required DX12 motion tile variable %s", material.m_pVarNames[var] );
	return pVar;
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
	if ( !s_TileReduceMaterial.m_Material.IsValid() )
	{
		KeyValues *pReduce = new KeyValues( "DX12_MotionBlurTile" );
		pReduce->SetInt( "$pass", 0 );
		CreateTileMaterial( s_TileReduceMaterial, pMaterialSystem, "dx12/motion_blur_tile_reduce", pReduce,
			s_TileReduceVarNames, ARRAYSIZE( s_TileReduceVarNames ) );
		KeyValues *pDilate = new KeyValues( "DX12_MotionBlurTile" );
		pDilate->SetInt( "$pass", 1 );
		CreateTileMaterial( s_TileDilateMaterial, pMaterialSystem, "dx12/motion_blur_tile_dilate", pDilate,
			s_TileDilateVarNames, ARRAYSIZE( s_TileDilateVarNames ) );
	}
	s_bTilesReady = s_TileRaw.IsValid() && s_TileDilated.IsValid() &&
		TileMaterialValid( s_TileReduceMaterial ) && TileMaterialValid( s_TileDilateMaterial );
}

void MotionVectorsDX12_ShutdownRenderTarget()
{
	s_MotionVectorTexture.Shutdown();
	s_TileRaw.Shutdown();
	s_TileDilated.Shutdown();
	s_TileReduceMaterial.m_Material.Shutdown();
	s_TileDilateMaterial.m_Material.Shutdown();
	s_TileReduceMaterial.m_pVarNames = s_TileDilateMaterial.m_pVarNames = nullptr;
	memset( s_TileReduceMaterial.m_nVarTokens, 0, sizeof( s_TileReduceMaterial.m_nVarTokens ) );
	memset( s_TileDilateMaterial.m_nVarTokens, 0, sizeof( s_TileDilateMaterial.m_nVarTokens ) );
	s_bTilesReady = false;
	s_bDX12 = false;
	s_pShaderAPI = nullptr;
	s_nConsumerFrame = s_nPreviousMainPassFrame = -1;
	s_bFrameHasConsumer = false;
	s_nMotionVectorFrame = -1;
	s_flPreviousMainPassTime = -1.0;
	s_flCurrentMainPassDelta = 0.0f;
}


bool MotionVectorsDX12_BuildBlurTiles( IMatRenderContext *pRenderContext, float flMaxBlurLength, int nRegionWidth, int nRegionHeight, int &nTileCountX, int &nTileCountY )
{
	nTileCountX = nTileCountY = 0;
	if ( !s_bTilesReady || !pRenderContext || !s_MotionVectorTexture.IsValid() )
		return false;
	const int nVelW = MIN( nRegionWidth, s_MotionVectorTexture->GetActualWidth() );
	const int nVelH = MIN( nRegionHeight, s_MotionVectorTexture->GetActualHeight() );
	if ( nVelW <= 0 || nVelH <= 0 )
		return false;
	const int nTileX = ( nVelW + 15 ) / 16;
	const int nTileY = ( nVelH + 15 ) / 16;
	if ( nTileX > 512 || nTileY > 512 )
		return false;
	const float flSearch = MAX( 0.5f * flMaxBlurLength, 3.f / MIN( nVelW, nVelH ) );
	const float flSizes[4] = { static_cast<float>( nVelW ), static_cast<float>( nVelH ), static_cast<float>( nTileX ), static_cast<float>( nTileY ) };
	GetTileVar( s_TileReduceMaterial, TILE_INPUT )->SetTextureValue( s_MotionVectorTexture );
	GetTileVar( s_TileReduceMaterial, TILE_OUTPUT )->SetTextureValue( s_TileRaw );
	GetTileVar( s_TileReduceMaterial, TILE_SIZES )->SetVecValue( flSizes, 4 );
	GetTileVar( s_TileDilateMaterial, TILE_INPUT )->SetTextureValue( s_TileRaw );
	GetTileVar( s_TileDilateMaterial, TILE_OUTPUT )->SetTextureValue( s_TileDilated );
	GetTileVar( s_TileDilateMaterial, TILE_SIZES )->SetVecValue( flSizes, 4 );
	GetTileVar( s_TileDilateMaterial, TILE_RADIUS )->SetFloatValue( flSearch );
	pRenderContext->BeginPIXEvent( PIX_VALVE_ORANGE, "Motion blur tile reduce" );
	pRenderContext->DrawScreenSpaceRectangle( s_TileReduceMaterial.m_Material, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
	pRenderContext->EndPIXEvent();
	pRenderContext->BeginPIXEvent( PIX_VALVE_ORANGE, "Motion blur tile dilate" );
	pRenderContext->DrawScreenSpaceRectangle( s_TileDilateMaterial.m_Material, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
	pRenderContext->EndPIXEvent();
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

bool MotionVectorsDX12_BlurIsStatic( float flBlurScale, float flMaxBlurLength, int nRegionWidth, int nRegionHeight )
{
	if ( r_motionvectors.GetInt() >= 2 || !s_pShaderAPI || !s_bTilesReady || !s_MotionVectorTexture.IsValid() ||
		!IsFinite( flBlurScale ) || !IsFinite( flMaxBlurLength ) ||
		MotionVectorsDX12_FrameEpoch() < 0 ||
		s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_MOTION_BLUR_STATIC ) != 1 )
		return false;
	const int nVelW = MIN( nRegionWidth, s_MotionVectorTexture->GetActualWidth() );
	const int nVelH = MIN( nRegionHeight, s_MotionVectorTexture->GetActualHeight() );
	if ( nVelW <= 0 || nVelH <= 0 || ( nVelW + 15 ) / 16 > 512 || ( nVelH + 15 ) / 16 > 512 )
		return false;
	const float flSearch = MAX( 0.5f * flMaxBlurLength, 3.f / MIN( nVelW, nVelH ) );
	// Wide dilation returns "always gather", not the actual maximum. Keep one tile of
	// slack at its eight-tile limit so CPU/GPU arithmetic at the boundary cannot certify it.
	if ( flSearch * nVelW > 16.f * 6.f || flSearch * nVelH > 16.f * 6.f )
		return false;
	const float flBound = s_pShaderAPI->GetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_MOTION_STATIC_BOUND );
	if ( !IsFinite( flBound ) || flBound < 0.f )
		return false;
	const float flReachable = MIN( flBound * fabsf( flBlurScale ), MAX( flMaxBlurLength, 0.f ) );
	const float flSkipLimit = 0.5f / MAX( nVelW, nVelH );
	return flReachable < flSkipLimit;
}

int MotionVectorsDX12_FrameEpoch()
{
	return s_bDX12 && s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_FRAME_EPOCH ) : -1;
}

CON_COMMAND_F( r_motionvectors_report, "Report DX12 motion consumer/producer frame latches (diagnostic only)", FCVAR_CHEAT )
{
	float flDelta = 0.0f;
	const bool bValid = MotionVectorsDX12_FrameValid( &flDelta );
	Msg( "DX12 motion latch: mode %d frame %d consumerFrame %d consumer %d producedFrame %d enabled %d valid %d delta %.6f upscaler %d framegen %d custom %d status %d static %d bound %.9f\n",
		r_motionvectors.GetInt(), gpGlobals ? gpGlobals->framecount : -1, s_nConsumerFrame, s_bFrameHasConsumer,
		s_nMotionVectorFrame, MotionVectorsDX12_Enabled(), bValid, flDelta, UpscalerDX12_RequiresMotionVectors(),
		FrameGenDX12_RequiresMotionVectors(), HasCustomMotionVectorReader(),
		s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_MOTION_STATUS ) : 0,
		s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_MOTION_BLUR_STATIC ) : 0,
		s_pShaderAPI ? s_pShaderAPI->GetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_MOTION_STATIC_BOUND ) : 0.f );
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
