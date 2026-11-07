//========= Copyright Valve Corporation, All rights reserved. ============//
// Native L4D2 glow GPU passes, C:10273210 / C:102755F0.
// No SDK2013 CGlowObjectManager, bloom replacement, particles, or render color.

#include "cbase.h"
#include "c_baseanimating.h"
#include "view.h"
#include "l4d2/effects/terror_glow_render.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/terror_glow_scene.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "l4d2/clientmode_terror.h"
#include "view_shared.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "filesystem.h"
#include "tier1/KeyValues.h"
#include "istudiorender.h"
#include "model_types.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )
static ConVar cl_glow_blur_scale( "cl_glow_blur_scale", "3", 0x00004008,
	"Controls the size of the halo shown around players and usable items" );
namespace
{
int s_GlowDrawDepth = 0;
// Native flags are carried through game-owned submission, not an engine ABI
// extension. Nested explicit submissions restore the previous metadata only.
bool s_SkipEntityStencil = false;
class CScopedTerrorEntityStencil
{
public:
	explicit CScopedTerrorEntityStencil( int nativeFlags ) : m_previous( s_SkipEntityStencil )
	{
		s_SkipEntityStencil = ( nativeFlags & 0x800 ) != 0;
	}
	~CScopedTerrorEntityStencil() { s_SkipEntityStencil = m_previous; }
private:
	bool m_previous;
};


enum TerrorGlowMaterial_t
{
	GLOW_MATERIAL_COLOR,
	GLOW_MATERIAL_DOWNSAMPLE,
	GLOW_MATERIAL_BLUR_X,
	GLOW_MATERIAL_BLUR_Y,
	GLOW_MATERIAL_COMPOSITE,
	GLOW_MATERIAL_COUNT
};

// Bind the unchanged native VMT resources to their real game-owned shader
// implementations, as CTerrorPostMaterials does for the other native passes.
// Do not depend on an adapted loose VMT winning over the mounted original.
class CTerrorGlowMaterials : public CAutoGameSystem
{
public:
	CTerrorGlowMaterials() : CAutoGameSystem( "TerrorGlowMaterials" ) {}
	IMaterial *Get( TerrorGlowMaterial_t material )
	{
		if ( !m_Materials[material].IsValid() )
		{
			static const char *paths[GLOW_MATERIAL_COUNT] =
			{
				"materials/dev/glow_color.vmt",
				"materials/dev/glow_downsample.vmt",
				"materials/dev/glow_blur_x.vmt",
				"materials/dev/glow_blur_y.vmt",
				"materials/dev/halo_add_to_screen.vmt"
			};
			static const char *shaders[GLOW_MATERIAL_COUNT] =
			{
				"UnlitGeneric", "L4D2_Downsample_nohdr",
				"L4D2_BlurFilterX", "L4D2_BlurFilterY", "L4D2_HaloAdd"
			};
			static const char *names[GLOW_MATERIAL_COUNT] =
			{
				"__l4d2/glow_color", "__l4d2/glow_downsample",
				"__l4d2/glow_blur_x", "__l4d2/glow_blur_y", "__l4d2/halo_add_to_screen"
			};
			KeyValues *pDefinition = new KeyValues( shaders[material] );
			if ( !pDefinition->LoadFromFile( filesystem, paths[material], "GAME" ) )
			{
				static const char *originalNames[GLOW_MATERIAL_COUNT] =
				{
					"dev/glow_color", "dev/glow_downsample",
					"dev/glow_blur_x", "dev/glow_blur_y", "dev/halo_add_to_screen"
				};
				pDefinition->deleteThis();
				m_Materials[material].Init( originalNames[material], TEXTURE_GROUP_OTHER );
				return m_Materials[material];
			}
			pDefinition->SetName( shaders[material] );
			m_Materials[material].Init( names[material], pDefinition );
		}
		return m_Materials[material];
	}
	virtual void Shutdown() override
	{
		for ( int i = 0; i < GLOW_MATERIAL_COUNT; ++i )
			m_Materials[i].Shutdown();
	}
private:
	CMaterialReference m_Materials[GLOW_MATERIAL_COUNT];
};
static CTerrorGlowMaterials g_TerrorGlowMaterials;

ITexture *RequireNativeTarget( const char *pName )
{
	return materials->FindTexture( pName, TEXTURE_GROUP_RENDER_TARGET );
}

void SetTargetViewport( IMatRenderContext *pContext, ITexture *pTarget, int width, int height )
{
	// Native helper C:102718B0. Preserve render-target storage size; only the
	// active viewport changes, including the full-size clear before subviews.
	pContext->SetRenderTarget( pTarget );
	pContext->Viewport( 0, 0, width, height );
}

void PrepareQuarterTarget( IMatRenderContext *pContext, ITexture *pTarget, int width, int height )
{
	if ( pTarget->GetActualWidth() != width || pTarget->GetActualHeight() != height )
	{
		SetTargetViewport( pContext, pTarget, pTarget->GetActualWidth(), pTarget->GetActualHeight() );
		pContext->ClearColor4ub( 0, 0, 0, 0 );
		pContext->ClearBuffers( true, false, false );
	}
	SetTargetViewport( pContext, pTarget, width, height );
}
}

int TerrorModel_DrawModel( C_BaseEntity *pEntity, int nativeFlags )
{
	CScopedTerrorEntityStencil scope( nativeFlags );
	return pEntity->DrawModel( nativeFlags & ~0x800 );
}

bool TerrorModel_SkipEntityStencil()
{
	return s_SkipEntityStencil;
}

void TerrorGlow_DrawModel( C_BaseEntity *pEntity, unsigned char instanceAlpha, int nativeFlags )
{
	CScopedTerrorEntityStencil scope( nativeFlags );
	// C:10045F00 consumes instance alpha only for brush models.
	// C:10030150 never dereferences it for studio models; global render
	// blend remains the caller's (player=1, Witch=alpha, death model=.5).
	C_TerrorPlayer *player = pEntity->IsPlayer() ? ToTerrorPlayer( pEntity ) : NULL;
	const bool ordinaryPlayerDraw = player && ( nativeFlags & STUDIO_RENDER ) && !( nativeFlags & 0x40000000 );
	if ( ordinaryPlayerDraw )
	{
		// C:258570 stamps even the grouped zero-alpha player rejection.
		player->MarkGlowDrawFrame( 0 );
		if ( instanceAlpha == 0 ) return;
	}
	const bool stencil = ordinaryPlayerDraw && !TerrorModel_SkipEntityStencil() &&
		TerrorGlow_ApplyPlayerStencil( player );
	if ( pEntity->GetModel() && modelinfo->GetModelType( pEntity->GetModel() ) == mod_brush )
		render->SetBlend( ( nativeFlags & 0x40000000 ) ? 1.0f : instanceAlpha * 0.0039215689f );
	++s_GlowDrawDepth;
	pEntity->DrawModel( STUDIO_RENDER | ( nativeFlags & STUDIO_WIREFRAME ) );
	--s_GlowDrawDepth;
	if ( stencil ) TerrorGlow_EndStencilMask();
}

bool TerrorGlow_DrawStudioModel( ClientModelRenderInfo_t *pInfo,
	DrawModelState_t *pState, matrix3x4_t *pBoneToWorld )
{
	if ( !s_GlowDrawDepth )
		return false;
	if ( !pState )
		return true;

	// DrawModelSetup has performed the real target model/LOD/bone work.
	// Existing virtual game checks and pre/post callbacks remain intact.
	// Bypass engine lighting/flex/decal work as native flags 0x10000600
	// request; do not pass those incompatible bits to the target engine.
	DrawModelInfo_t info;
	info.m_pStudioHdr = pState->m_pStudioHdr;
	info.m_pHardwareData = pState->m_pStudioHWData;
	info.m_Decals = STUDIORENDER_DECAL_INVALID;
	info.m_Skin = pInfo->skin;
	info.m_Body = pInfo->body;
	info.m_HitboxSet = pInfo->hitboxset;
	info.m_pClientEntity = pState->m_pRenderable;
	info.m_Lod = pState->m_lod;
	info.m_pColorMeshes = NULL;
	info.m_bStaticLighting = false;
	info.m_nLocalLightCount = 0;
	for ( int i = 0; i < ARRAYSIZE( info.m_vecAmbientCube ); ++i )
		info.m_vecAmbientCube[i].Init();

	Vector color;
	render->GetColorModulation( color.Base() );
	g_pStudioRender->SetViewState( CurrentViewOrigin(), CurrentViewRight(),
		CurrentViewUp(), CurrentViewForward() );
	g_pStudioRender->SetColorModulation( color.Base() );
	g_pStudioRender->SetAlphaModulation( render->GetBlend() );
	g_pStudioRender->DrawModel( NULL, info, pBoneToWorld, NULL, NULL, pInfo->origin,
		pState->m_drawFlags | STUDIORENDER_DRAW_NO_FLEXES );
	return true;
}

void TerrorGlow_BeginColorBuffer( const CViewSetup &view )
{
	CMatRenderContextPtr pContext( materials );
	pContext->PushRenderTargetAndViewport();
	SetTargetViewport( pContext, RequireNativeTarget( "_rt_FullFrameFB" ), view.width, view.height );
	pContext->ClearColor4ub( 0, 0, 0, 0 );
	pContext->ClearBuffers( true, false, false );
	g_pStudioRender->ForcedMaterialOverride( g_TerrorGlowMaterials.Get( GLOW_MATERIAL_COLOR ) );
}

void TerrorGlow_EndColorBuffer()
{
	CMatRenderContextPtr pContext( materials );
	pContext->PopRenderTargetAndViewport();
}

void TerrorGlow_BeginStencilMask( int reference, bool replaceOnZFail, unsigned int writeMask )
{
	CMatRenderContextPtr pContext( materials );
	pContext->SetStencilEnable( true );
	pContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilZFailOperation( replaceOnZFail ? STENCILOPERATION_REPLACE : STENCILOPERATION_KEEP );
	pContext->SetStencilPassOperation( STENCILOPERATION_REPLACE );
	pContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_ALWAYS );
	pContext->SetStencilReferenceValue( reference );
	pContext->SetStencilTestMask( 0xffffffff );
	pContext->SetStencilWriteMask( writeMask );
}

void TerrorGlow_EndStencilMask()
{
	// Native ShaderStencilState_t default from C:1002B480.
	CMatRenderContextPtr pContext( materials );
	pContext->SetStencilEnable( false );
	pContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilPassOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_ALWAYS );
	pContext->SetStencilReferenceValue( 0 );
	pContext->SetStencilTestMask( 0xffffffff );
	pContext->SetStencilWriteMask( 0xffffffff );
}

void TerrorGlow_Composite( const CViewSetup &view, float blurScale, float compositeScale )
{
	// Custom shaders retain the original contract; stock shaders stay intact.
	IMaterial *pDownsample = g_TerrorGlowMaterials.Get( GLOW_MATERIAL_DOWNSAMPLE );
	IMaterial *pBlurX = g_TerrorGlowMaterials.Get( GLOW_MATERIAL_BLUR_X );
	IMaterial *pBlurY = g_TerrorGlowMaterials.Get( GLOW_MATERIAL_BLUR_Y );
	IMaterial *pComposite = g_TerrorGlowMaterials.Get( GLOW_MATERIAL_COMPOSITE );
	// Native dev/glow_blur_x/y supply INTEGER $kernel=1 in their VMTs.
	// Native does not look it up, rewrite it, or raise a contract Engine Error.

	ITexture *pFullFrame = RequireNativeTarget( "_rt_FullFrameFB" );
	ITexture *pSmall0 = RequireNativeTarget( "_rt_SmallFB0" );
	ITexture *pSmall1 = RequireNativeTarget( "_rt_SmallFB1" );
	const int width = view.width / 4;
	const int height = view.height / 4;
	CMatRenderContextPtr pContext( materials );
	int viewportX, viewportY, viewportWidth, viewportHeight;
	pContext->GetViewport( viewportX, viewportY, viewportWidth, viewportHeight );
	pContext->PushRenderTargetAndViewport();

	PrepareQuarterTarget( pContext, pSmall0, width, height );
	IMaterialVar *pBloomExp = pDownsample->FindVar( "$bloomexp", NULL, true );
	if ( pBloomExp )
		pBloomExp->SetFloatValue( 2.5f );
	IMaterialVar *pBloomSaturation = pDownsample->FindVar( "$bloomsaturation", NULL, true );
	if ( pBloomSaturation )
		pBloomSaturation->SetFloatValue( 1.0f );
	pContext->DrawScreenSpaceRectangle( pDownsample, 0, 0, width, height,
		0.0f, 0.0f, view.width - 4.0f, view.height - 4.0f,
		pFullFrame->GetActualWidth(), pFullFrame->GetActualHeight() );

	PrepareQuarterTarget( pContext, pSmall1, width, height );
	pContext->DrawScreenSpaceRectangle( pBlurX, 0, 0, width, height,
		0.0f, 0.0f, width - 1.0f, height - 1.0f,
		pSmall0->GetActualWidth(), pSmall0->GetActualHeight() );

	SetTargetViewport( pContext, pSmall0, width, height );
	pBlurY->FindVar( "$bloomamount", NULL, true )->SetFloatValue( blurScale );
	pContext->DrawScreenSpaceRectangle( pBlurY, 0, 0, width, height,
		0.0f, 0.0f, width - 1.0f, height - 1.0f,
		pSmall1->GetActualWidth(), pSmall1->GetActualHeight() );
	pContext->PopRenderTargetAndViewport();
	pComposite->FindVar( "$C0_X", NULL, true )->SetFloatValue( compositeScale );
	pComposite->FindVar( "$C0_Y", NULL, true )->SetFloatValue( 0.8f );

	// Main-view stencil bits 1 and 3 suppress the interior. Bits0/2 are
	// deliberately excluded by the native 0xFA test mask. No stencil clear.
	pContext->SetStencilEnable( true );
	pContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilPassOperation( STENCILOPERATION_KEEP );
	pContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_EQUAL );
	pContext->SetStencilReferenceValue( 0 );
	pContext->SetStencilTestMask( 0xfa );
	pContext->SetStencilWriteMask( 0 );
	pContext->DrawScreenSpaceRectangle( pComposite, 0, 0, viewportWidth, viewportHeight,
		0.0f, -0.5f, width - 1.0f, height - 1.0f,
		pSmall1->GetActualWidth(), pSmall1->GetActualHeight() );
	TerrorGlow_EndStencilMask();
}

bool TerrorGlow_IsModelPass()
{
	return s_GlowDrawDepth != 0;
}

// C:276460 ->2755F0. This is the real view entry, not an SDK glow-manager
// proxy. The native default-false UI suppression has a separate real writer.
void TerrorGlow_Render( const CViewSetup &view, int slot )
{
	if ( cl_glow_blur_scale.GetFloat() > 0.0f && TerrorGlow_ShouldRender( slot ) )
	{
		const float scale = TerrorGlow_GetCompositeScale( slot ) *
			static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->GetTransitionFadeScale();
		if ( scale > 0.0000099999997f )
		{
			CViewSetup glowView = view;
			glowView.zFar = 9999999.0f;
			Frustum frustum;
			render->Push3DView( glowView, 0, NULL, frustum );
			g_pStudioRender->ForcedMaterialOverride( g_TerrorGlowMaterials.Get( GLOW_MATERIAL_COLOR ) );
			TerrorGlow_DrawStencil( glowView, slot );
			g_pStudioRender->ForcedMaterialOverride( NULL );
			if ( TerrorGlow_DrawColor( slot, false ) )
			{
				Vector color;
				render->GetColorModulation( color.Base() );
				const float blend = render->GetBlend();
				TerrorGlow_BeginColorBuffer( view );
				TerrorGlow_DrawColor( slot, true );
				g_pStudioRender->ForcedMaterialOverride( NULL );
				render->SetColorModulation( color.Base() );
				render->SetBlend( blend );
				TerrorGlow_EndColorBuffer();
				render->PopView( frustum );
				TerrorGlow_Composite( view, cl_glow_blur_scale.GetFloat(), scale );
			}
			else render->PopView( frustum );
		}
	}
}

#pragma float_control( pop )
