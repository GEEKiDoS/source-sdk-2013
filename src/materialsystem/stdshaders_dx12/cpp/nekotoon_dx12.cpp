// Draw the NekoToon model surface and its optional skinned outline. Both passes
// use the same frozen receiver generation as the native lit model materials.
#include "BaseVSShaderDX12.h"
#include "lightmappedgeneric_dx9_helper.h"
#include "nekotoon_model_vs51.inc"
#include "nekotoon_model_ps51.inc"
#include "tier0/memdbgon.h"
extern ConVar r_shadowmap_filter_quality;
namespace {
// PBRMaterialVS world-only blend transform is available to model hulls.
const int kBaseTransformRegister = 48, kBumpTransformRegister = 50, kHullRegister = 54;
const int kAmbientCubeRegister = 15, kLocalLightRegister = 21, kFogRegister = 27, kToonMaterialRegister = 0;
const float kMinimumCharacterLight = 0.45f;
}

BEGIN_VS_SHADER( NekoToon, "Stylized model with probe and shadow-map lighting" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( LIGHTINGSCALE, SHADER_PARAM_TYPE_FLOAT, ".6", "NekoToon lightingscale" )
		SHADER_PARAM( CAMERAAO, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon cameraao" )
		SHADER_PARAM( LIGHTINGSATURATION, SHADER_PARAM_TYPE_FLOAT, ".75", "NekoToon lightingsaturation" )
		SHADER_PARAM( ALPHA2, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon alpha2" )
		SHADER_PARAM( DARKNESSLIMIT, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon darknesslimit" )
		SHADER_PARAM( LIGHTWARPSCALE, SHADER_PARAM_TYPE_FLOAT, "2", "NekoToon lightwarpscale" )
		SHADER_PARAM( LIGHTWARPMAPOFFSET, SHADER_PARAM_TYPE_FLOAT, "0", "NekoToon lightwarpmapoffset" )
		SHADER_PARAM( LIGHTWARPEXPONENT, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon lightwarpexponent" )
		SHADER_PARAM( RIMLIGHTBOOST, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon rimlightboost" )
		SHADER_PARAM( RIMLIGHTEXPONENT, SHADER_PARAM_TYPE_FLOAT, "4", "NekoToon rimlightexponent" )
		SHADER_PARAM( RIMLIGHTALBEDOTINT, SHADER_PARAM_TYPE_FLOAT, ".8", "NekoToon rimlightalbedotint" )
		SHADER_PARAM( MATCAPLIGHTING, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon matcaplighting" )
		SHADER_PARAM( OUTLINETHICKNESS, SHADER_PARAM_TYPE_FLOAT, ".02", "NekoToon outlinethickness" )
		SHADER_PARAM( OUTLINEZOFFSET, SHADER_PARAM_TYPE_FLOAT, "0", "NekoToon outlinezoffset" )
		SHADER_PARAM( OUTLINELIGHTING, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon outlinelighting" )
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, ".5", "NekoToon alphatestreference" )
		SHADER_PARAM( EYEBROWALPHA, SHADER_PARAM_TYPE_FLOAT, ".5", "NekoToon eyebrowalpha" )
		SHADER_PARAM( EYEBROWZOFFSET, SHADER_PARAM_TYPE_FLOAT, "-.6", "NekoToon eyebrowzoffset" )
		SHADER_PARAM( BUMPSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon bumpstrength" )
		SHADER_PARAM( EMISSIONSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "1", "NekoToon emissionstrength" )
		SHADER_PARAM( PHONGBOOST, SHADER_PARAM_TYPE_FLOAT, "0", "Legacy VMT compatibility only; NekoToon uses matcap rather than Source Phong" )
		SHADER_PARAM( PHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "0", "Legacy VMT compatibility only; NekoToon uses matcap rather than Source Phong" )
		SHADER_PARAM( ENVMAPFRESNEL, SHADER_PARAM_TYPE_FLOAT, "0", "NekoToon envmapfresnel" )
		SHADER_PARAM( FLASHLIGHTLAMBERTRANGE, SHADER_PARAM_TYPE_FLOAT, ".5", "NekoToon flashlightlambertrange" )
		SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon bumpframe" )
		SHADER_PARAM( MATCAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon matcapframe" )
		SHADER_PARAM( MATCAP, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon matcap" )
		SHADER_PARAM( OUTLINE, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon outline" )
		SHADER_PARAM( RIMLIGHT, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon rimlight" )
		SHADER_PARAM( EYEBROW, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon eyebrow" )
		SHADER_PARAM( PHONG, SHADER_PARAM_TYPE_INTEGER, "0", "Legacy VMT compatibility only; NekoToon uses matcap rather than Source Phong" )
		SHADER_PARAM( PHONGALBEDOTINT, SHADER_PARAM_TYPE_INTEGER, "0", "Legacy VMT compatibility only; NekoToon uses matcap rather than Source Phong" )
		SHADER_PARAM( EMISSION, SHADER_PARAM_TYPE_INTEGER, "0", "NekoToon emission" )
		SHADER_PARAM( OUTLINEFIXEDTHICKNESS, SHADER_PARAM_TYPE_BOOL, "1", "NekoToon outlinefixedthickness" )
		SHADER_PARAM( LIGHTWARPHALF, SHADER_PARAM_TYPE_BOOL, "1", "NekoToon lightwarphalf" )
		SHADER_PARAM( OUTLINEBASETEXTUREBLEND, SHADER_PARAM_TYPE_BOOL, "1", "NekoToon outlinebasetextureblend" )
		SHADER_PARAM( ALLOWDIFFUSEMODULATION, SHADER_PARAM_TYPE_BOOL, "1", "NekoToon allowdiffusemodulation" )
		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon bumpmap" )
		SHADER_PARAM( LIGHTWARPTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon lightwarptexture" )
		SHADER_PARAM( LIGHTWARPSRGBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon lightwarpsrgbtexture" )
		SHADER_PARAM( MATCAPTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon matcaptexture" )
		SHADER_PARAM( MASK1, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon mask1" )
		SHADER_PARAM( MASK2, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon mask2" )
		SHADER_PARAM( OUTLINETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon outlinetexture" )
		SHADER_PARAM( EMISSIONTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "NekoToon emissiontexture" )
		SHADER_PARAM( PHONGEXPONENTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Legacy VMT compatibility only; NekoToon uses matcap rather than Source Phong" )
		SHADER_PARAM( OUTLINECOLOR, SHADER_PARAM_TYPE_COLOR, "[0 0 0]", "NekoToon outlinecolor" )
		SHADER_PARAM( EMISSIONTINT, SHADER_PARAM_TYPE_COLOR, "[.005 .005 .005]", "NekoToon emissiontint" )
		SHADER_PARAM( MATCAPTINT, SHADER_PARAM_TYPE_VEC4, "[1 1 1 1]", "NekoToon matcaptint" )
		SHADER_PARAM( PHONGFRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "[0 .5 1]", "Legacy VMT compatibility only; NekoToon uses matcap rather than Source Phong" )
		SHADER_PARAM( BUMPTRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "NekoToon bumptransform" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}
	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
		SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );
		SET_FLAGS( MATERIAL_VAR_SUPPRESS_DECALS );
		if ( !params[LIGHTINGSCALE]->IsDefined() )
			params[LIGHTINGSCALE]->SetFloatValue( .6f );
		if ( !params[CAMERAAO]->IsDefined() )
			params[CAMERAAO]->SetFloatValue( 1.0f );
		if ( !params[LIGHTINGSATURATION]->IsDefined() )
			params[LIGHTINGSATURATION]->SetFloatValue( .75f );
		if ( !params[ALPHA2]->IsDefined() )
			params[ALPHA2]->SetFloatValue( 1.0f );
		if ( !params[DARKNESSLIMIT]->IsDefined() )
			params[DARKNESSLIMIT]->SetFloatValue( 1.0f );
		if ( !params[LIGHTWARPSCALE]->IsDefined() )
			params[LIGHTWARPSCALE]->SetFloatValue( 2.0f );
		if ( !params[LIGHTWARPEXPONENT]->IsDefined() )
			params[LIGHTWARPEXPONENT]->SetFloatValue( 1.0f );
		if ( !params[RIMLIGHTBOOST]->IsDefined() )
			params[RIMLIGHTBOOST]->SetFloatValue( 1.0f );
		if ( !params[RIMLIGHTEXPONENT]->IsDefined() )
			params[RIMLIGHTEXPONENT]->SetFloatValue( 4.0f );
		if ( !params[RIMLIGHTALBEDOTINT]->IsDefined() )
			params[RIMLIGHTALBEDOTINT]->SetFloatValue( .8f );
		if ( !params[MATCAPLIGHTING]->IsDefined() )
			params[MATCAPLIGHTING]->SetFloatValue( 1.0f );
		if ( !params[OUTLINETHICKNESS]->IsDefined() )
			params[OUTLINETHICKNESS]->SetFloatValue( .02f );
		if ( !params[OUTLINELIGHTING]->IsDefined() )
			params[OUTLINELIGHTING]->SetFloatValue( 1.0f );
		if ( !params[ALPHATESTREFERENCE]->IsDefined() )
			params[ALPHATESTREFERENCE]->SetFloatValue( .5f );
		if ( !params[EYEBROWALPHA]->IsDefined() )
			params[EYEBROWALPHA]->SetFloatValue( .5f );
		if ( !params[EYEBROWZOFFSET]->IsDefined() )
			params[EYEBROWZOFFSET]->SetFloatValue( -.6f );
		if ( !params[BUMPSTRENGTH]->IsDefined() )
			params[BUMPSTRENGTH]->SetFloatValue( 1.0f );
		if ( !params[EMISSIONSTRENGTH]->IsDefined() )
			params[EMISSIONSTRENGTH]->SetFloatValue( 1.0f );
		if ( !params[FLASHLIGHTLAMBERTRANGE]->IsDefined() )
			params[FLASHLIGHTLAMBERTRANGE]->SetFloatValue( .5f );
		if ( !params[OUTLINEFIXEDTHICKNESS]->IsDefined() )
			params[OUTLINEFIXEDTHICKNESS]->SetFloatValue( 1.0f );
		if ( !params[LIGHTWARPHALF]->IsDefined() )
			params[LIGHTWARPHALF]->SetFloatValue( 1.0f );
		if ( !params[OUTLINEBASETEXTUREBLEND]->IsDefined() )
			params[OUTLINEBASETEXTUREBLEND]->SetFloatValue( 1.0f );
		if ( !params[ALLOWDIFFUSEMODULATION]->IsDefined() )
			params[ALLOWDIFFUSEMODULATION]->SetFloatValue( 1.0f );
		if ( !params[MATCAPTINT]->IsDefined() )
			params[MATCAPTINT]->SetVecValue( 1, 1, 1, 1 );
		if ( !params[OUTLINECOLOR]->IsDefined() )
			params[OUTLINECOLOR]->SetVecValue( 0, 0, 0 );
	}
	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() )
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[MATCAPTEXTURE]->IsDefined() )
			LoadTexture( MATCAPTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[EMISSIONTEXTURE]->IsDefined() )
			LoadTexture( EMISSIONTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[LIGHTWARPSRGBTEXTURE]->IsDefined() )
			LoadTexture( LIGHTWARPSRGBTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[LIGHTWARPTEXTURE]->IsDefined() )
			LoadTexture( LIGHTWARPTEXTURE );
		if ( params[MASK1]->IsDefined() )
			LoadTexture( MASK1 );
		if ( params[MASK2]->IsDefined() )
			LoadTexture( MASK2 );
		if ( params[OUTLINETEXTURE]->IsDefined() )
			LoadTexture( OUTLINETEXTURE );
		if ( params[BUMPMAP]->IsDefined() )
			LoadBumpMap( BUMPMAP );
	}
	bool HasTexture( IMaterialVar **params, int v )
	{
		return params[v]->IsTexture() && !params[v]->GetTextureValue()->IsError();
	}
	void BindMaterialTexture( IMaterialVar **params, IShaderDynamicAPI *api, Sampler_t slot, int materialVar, int frameVar )
	{
		if ( HasTexture( params, materialVar ) )
			BindTexture( slot, materialVar, frameVar );
		else
			api->BindStandardTexture( slot, TEXTURE_WHITE );
	}
	SHADER_DRAW
	{
		bool enhanced = false;
		if ( !DX12ShadowmapReceiverSnapshot( pContextDataPtr, pShaderShadow, GetName(), enhanced ) )
			return;
		const bool bump = HasTexture( params, BUMPMAP );
		const int warp = HasTexture( params, LIGHTWARPSRGBTEXTURE ) ? LIGHTWARPSRGBTEXTURE : LIGHTWARPTEXTURE;

		for ( int pass = 0; pass < 1 + ( params[OUTLINE]->GetIntValue() != 0 ); ++pass )
		{
			bool hull = pass != 0;
			SHADOW_STATE
			{
				SetInitialShadowState();
				pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED, 1, 0,
														 bump ? 4 : 0 );
				SetBlendingShadowState( EvaluateBlendRequirements( BASETEXTURE, true ) );
				if ( hull )
				{
					pShaderShadow->EnableCulling( false );
					pShaderShadow->EnableDepthWrites( false );
				}
				pShaderShadow->EnableAlphaTest( IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) );
				if ( IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) )
					pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, params[ALPHATESTREFERENCE]->GetFloatValue() );
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, warp == LIGHTWARPSRGBTEXTURE );
				pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER7, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
				pShaderShadow->EnableSRGBWrite( true );
				DefaultFog();
				DECLARE_STATIC_VERTEX_SHADER( nekotoon_model_vs51 );
				SET_STATIC_VERTEX_SHADER_COMBO( BUMP, bump );
				SET_STATIC_VERTEX_SHADER_COMBO( HULL, hull );
				SET_STATIC_VERTEX_SHADER( nekotoon_model_vs51 );
				DECLARE_STATIC_PIXEL_SHADER( nekotoon_model_ps51 );
				SET_STATIC_PIXEL_SHADER_COMBO( BUMP, bump );
				SET_STATIC_PIXEL_SHADER_COMBO( HULL, hull );
				SET_STATIC_PIXEL_SHADER_COMBO( ENHANCED, enhanced );
				SET_STATIC_PIXEL_SHADER( nekotoon_model_ps51 );
			}
			DYNAMIC_STATE
			{
				if ( UsingFlashlight( params ) )
				{
					Draw( false );
					continue;
				}
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER0, BASETEXTURE, FRAME );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER1, BUMPMAP, BUMPFRAME );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER2, warp, -1 );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER3, MATCAPTEXTURE, MATCAPFRAME );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER4, MASK1, -1 );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER5, MASK2, -1 );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER6, OUTLINETEXTURE, -1 );
				BindMaterialTexture( params, pShaderAPI, SHADER_SAMPLER7, EMISSIONTEXTURE, -1 );
				SetVertexShaderTextureTransform( kBaseTransformRegister, BASETEXTURETRANSFORM );
				SetVertexShaderTextureTransform( kBumpTransformRegister, BUMPTRANSFORM );
				SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11,
											 SHADER_VERTEXTEXTURE_SAMPLER0 );
				int w, h;
				pShaderAPI->GetBackBufferDimensions( w, h );
				float extrusion[] = { params[OUTLINETHICKNESS]->GetFloatValue(), params[OUTLINEFIXEDTHICKNESS]->GetFloatValue(),
									  params[OUTLINEZOFFSET]->GetFloatValue(), float( w ) / h };
				DX12SetVertexShaderConstant( kHullRegister, extrusion );
				LightState_t state = {};
				pShaderAPI->GetDX9LightState( &state );
				pShaderAPI->SetPixelShaderStateAmbientLightCube( kAmbientCubeRegister, !state.m_bAmbientLight );
				pShaderAPI->CommitPixelShaderLighting( kLocalLightRegister );
				pShaderAPI->SetPixelShaderFogParams( kFogRegister );
				dx12cb::NekoModelPS material = {};
				ComputeModulationColor( material.g_NekoColor );
				if ( !params[ALLOWDIFFUSEMODULATION]->GetIntValue() )
					material.g_NekoColor[0] = material.g_NekoColor[1] = material.g_NekoColor[2] = 1;

				pShaderAPI->GetWorldSpaceCameraPosition( material.g_NekoEye );
				material.g_NekoEye[3] = float( state.m_nNumLights );
				material.g_NekoLighting[0] = params[LIGHTINGSCALE]->GetFloatValue();
				material.g_NekoLighting[1] = params[LIGHTINGSATURATION]->GetFloatValue();
				material.g_NekoLighting[2] = params[DARKNESSLIMIT]->GetFloatValue() * kMinimumCharacterLight;
				material.g_NekoLighting[3] = params[FLASHLIGHTLAMBERTRANGE]->GetFloatValue();
				material.g_NekoWarp[0] = HasTexture( params, warp ) ? params[LIGHTWARPSCALE]->GetFloatValue() : 0;
				material.g_NekoWarp[1] = params[LIGHTWARPMAPOFFSET]->GetFloatValue();
				material.g_NekoWarp[2] = params[LIGHTWARPEXPONENT]->GetFloatValue();
				material.g_NekoWarp[3] = params[LIGHTWARPHALF]->GetFloatValue();
				material.g_NekoRim[0] = params[RIMLIGHT]->GetFloatValue() * params[RIMLIGHTBOOST]->GetFloatValue();
				material.g_NekoRim[1] = params[RIMLIGHTEXPONENT]->GetFloatValue();
				material.g_NekoRim[3] = params[CAMERAAO]->GetFloatValue();
				material.g_NekoRim[2] = params[RIMLIGHTALBEDOTINT]->GetFloatValue();
				material.g_NekoCap[0] = HasTexture( params, MATCAPTEXTURE ) ? params[MATCAP]->GetFloatValue() : 0;
				material.g_NekoCap[1] = params[MATCAPLIGHTING]->GetFloatValue();
				params[MATCAPTINT]->GetVecValue( material.g_NekoCapTint, 4 );
				params[OUTLINECOLOR]->GetVecValue( material.g_NekoOutline, 3 );
				material.g_NekoOutline[3] = params[OUTLINELIGHTING]->GetFloatValue();
				material.g_NekoSurface[0] = clamp( params[ALPHA2]->GetFloatValue(), 0.0f, 1.0f );
				material.g_NekoSurface[1] = HasTexture( params, OUTLINETEXTURE ) &&
											params[OUTLINETEXTURE]->GetTextureValue()->GetImageFormat() == IMAGE_FORMAT_ATI1N;
				material.g_NekoSurface[2] = params[BUMPSTRENGTH]->GetFloatValue();
				material.g_NekoSurface[3] = params[OUTLINEBASETEXTUREBLEND]->GetFloatValue();
				material.g_NekoEmit[3] = HasTexture( params, EMISSIONTEXTURE );
				if ( params[EMISSION]->GetFloatValue() != 0 )
				{
					params[EMISSIONTINT]->GetVecValue( material.g_NekoEmit, 3 );
					for ( int j = 0; j < 3; ++j )
						material.g_NekoEmit[j] *= params[EMISSIONSTRENGTH]->GetFloatValue();
				}
				float view[16];
				pShaderAPI->GetMatrix( MATERIAL_VIEW, view );
				for ( int j = 0; j < 3; ++j )
				{
					material.g_NekoRight[j] = view[j * 4];
					material.g_NekoUp[j] = view[j * 4 + 1];
				}
				const Vector display = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_DX12_DISPLAY_TRANSFORM );
				material.g_NekoDisplay[0] = display.x;
				material.g_NekoDisplay[1] = display.y;
				ShaderColorCorrectionInfo_t correction{};
				pShaderAPI->GetCurrentColorCorrection( &correction );
				if ( display.x > 0.0f && correction.m_bIsEnabled && correction.m_nLookupCount )
				{
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER8, TEXTURE_DX12_INVERSE_COLOR_CORRECTION );
					material.g_NekoDisplay[2] = 1.0f;
				}
				const Vector origin = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_DX12_MODEL_LIGHTING_ORIGIN );
				material.g_NekoProbeOrigin[0] = origin.x;
				material.g_NekoProbeOrigin[1] = origin.y;
				material.g_NekoProbeOrigin[2] = origin.z;
				static_assert( sizeof( material ) == 14 * sizeof( material.g_NekoColor ), "contiguous NekoModelPS legacy rows" );
				DX12SetPixelShaderConstant( kToonMaterialRegister, material.g_NekoColor,
											sizeof( material ) / sizeof( material.g_NekoColor ) );
				DECLARE_DYNAMIC_VERTEX_SHADER( nekotoon_model_vs51 );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, pShaderAPI->IsHWMorphingEnabled() );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
				SET_DYNAMIC_VERTEX_SHADER( nekotoon_model_vs51 );
				DECLARE_DYNAMIC_PIXEL_SHADER( nekotoon_model_ps51 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo1( true ) );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( SHADOWQUALITY, enhanced ? r_shadowmap_filter_quality.GetInt() : 0 );
				SET_DYNAMIC_PIXEL_SHADER( nekotoon_model_ps51 );
				g_DX12Constants.shadowmapPassAdmitted = enhanced;
			}
			Draw();
		}
	}
END_SHADER
