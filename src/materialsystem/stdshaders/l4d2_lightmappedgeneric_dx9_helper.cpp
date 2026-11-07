//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "l4d2_lightmappedgeneric_dx9_helper.h"
#include "commandbuilder.h"
#include "renderparm.h"
#include "l4d2_lightmappedgeneric_vs20.inc"
#include "l4d2_lightmappedgeneric_ps20b.inc"
#include "l4d2_lightmappedgeneric_flashlight_vs20.inc"
#include "l4d2_lightmappedgeneric_flashlight_ps20b.inc"
#include "tier0/memdbgon.h"

void InitParamsL4D2LightmappedGeneric_DX9( CBaseVSShader *pShader, IMaterialVar **params,
	const char *pMaterialName, L4D2LightmappedGeneric_DX9_Vars_t &info )
{
	params[info.m_nFlashlightTexture]->SetStringValue( g_pHardwareConfig->SupportsBorderColor() ?
		"effects/flashlight_border" : "effects/flashlight001" );
	InitIntParam( info.m_nNoDiffuseBumpLighting, params, 0 );
	if ( g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() &&
		params[info.m_nAlbedo]->IsDefined() && params[info.m_nBaseTexture]->IsDefined() &&
		!params[info.m_nNoDiffuseBumpLighting]->GetIntValue() )
	{
		params[info.m_nBaseTexture]->SetStringValue( params[info.m_nAlbedo]->GetStringValue() );
	}
	if ( pShader->IsUsingGraphics() && params[info.m_nEnvmap]->IsDefined() && !pShader->CanUseEditorMaterials() &&
		!Q_stricmp( params[info.m_nEnvmap]->GetStringValue(), "env_cubemap" ) )
	{
		Warning( "env_cubemap used on world geometry without rebuilding map. . ignoring: %s\n", pMaterialName );
		params[info.m_nEnvmap]->SetUndefined();
	}
	static ConVarRef mat_disable_lightwarp( "mat_disable_lightwarp" );
	static ConVarRef mat_disable_fancy_blending( "mat_disable_fancy_blending" );
	if ( mat_disable_lightwarp.GetBool() )
		params[info.m_nLightWarpTexture]->SetUndefined();
	if ( mat_disable_fancy_blending.GetBool() )
		params[info.m_nBlendModulateTexture]->SetUndefined();
	if ( !params[info.m_nEnvmapTint]->IsDefined() )
		params[info.m_nEnvmapTint]->SetVecValue( 1, 1, 1 );
	if ( !params[info.m_nSelfIllumTint]->IsDefined() )
		params[info.m_nSelfIllumTint]->SetVecValue( 1, 1, 1 );
	if ( !params[info.m_nDetailTint]->IsDefined() )
		params[info.m_nDetailTint]->SetVecValue( 1, 1, 1 );
	InitFloatParam( info.m_nDetailScale, params, 4 );
	InitFloatParam( info.m_nDetailTextureBlendFactor, params, 1 );
	InitIntParam( info.m_nDetailTextureCombineMode, params, 0 );
	InitFloatParam( info.m_nFresnelReflection, params, 1 );
	InitFloatParam( info.m_nEnvmapContrast, params, 0 );
	InitFloatParam( info.m_nEnvmapSaturation, params, 1 );
	InitFloatParam( info.m_nAlphaTestReference, params, 0 );
	InitIntParam( info.m_nBaseTextureFrame, params, 0 );
	InitIntParam( info.m_nBaseTexture2Frame, params, 0 );
	InitIntParam( info.m_nEnvmapFrame, params, 0 );
	InitIntParam( info.m_nEnvmapMaskFrame, params, 0 );
	InitIntParam( info.m_nBumpFrame, params, 0 );
	InitIntParam( info.m_nBumpFrame2, params, 0 );
	InitIntParam( info.m_nDetailFrame, params, 0 );
	InitIntParam( info.m_nBaseTextureNoEnvmap, params, 0 );
	InitIntParam( info.m_nBaseTexture2NoEnvmap, params, 0 );
	InitIntParam( info.m_nSelfShadowedBumpFlag, params, 0 );
	InitIntParam( info.m_nBlendTintByBaseAlpha, params, 0 );
	InitFloatParam( info.m_nSsBumpMathFix, params, 0 );
	InitIntParam( info.m_nVertexFog, params, 0 );
	if ( !params[info.m_nBaseTexture]->IsDefined() )
	{
		CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	}
	if ( params[info.m_nBumpmap]->IsDefined() )
		params[info.m_nEnvmapMask]->SetUndefined();
	if ( IS_FLAG_SET( MATERIAL_VAR_DECAL ) )
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
	SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
	if ( g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() &&
		!params[info.m_nNoDiffuseBumpLighting]->GetIntValue() )
	{
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
	}
	if ( !g_pConfig->UseSpecular() && params[info.m_nBaseTexture]->IsDefined() )
		params[info.m_nEnvmap]->SetUndefined();
}

static bool IsWorldDetailSRGB( int mode )
{
	return mode == 2 || mode == 3 || mode == 4;
}

void InitL4D2LightmappedGeneric_DX9( CBaseVSShader *pShader, IMaterialVar **params,
	L4D2LightmappedGeneric_DX9_Vars_t &info )
{
	if ( g_pConfig->UseBumpmapping() )
	{
		if ( params[info.m_nBumpmap]->IsDefined() )
			pShader->LoadBumpMap( info.m_nBumpmap );
		if ( params[info.m_nBumpmap2]->IsDefined() )
			pShader->LoadBumpMap( info.m_nBumpmap2 );
		if ( params[info.m_nBumpMask]->IsDefined() )
			pShader->LoadBumpMap( info.m_nBumpMask );
	}
	bool baseAlpha = false;
	if ( params[info.m_nBaseTexture]->IsDefined() )
	{
		pShader->LoadTexture( info.m_nBaseTexture, TEXTUREFLAGS_SRGB );
		baseAlpha = params[info.m_nBaseTexture]->GetTextureValue()->IsTranslucent();
	}
	if ( !baseAlpha )
	{
		CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	}
	if ( params[info.m_nBaseTexture2]->IsDefined() )
		pShader->LoadTexture( info.m_nBaseTexture2, TEXTUREFLAGS_SRGB );
	if ( params[info.m_nLightWarpTexture]->IsDefined() )
		pShader->LoadTexture( info.m_nLightWarpTexture );
	if ( params[info.m_nBlendModulateTexture]->IsDefined() )
		pShader->LoadTexture( info.m_nBlendModulateTexture );
	if ( params[info.m_nDetail]->IsDefined() )
		pShader->LoadTexture( info.m_nDetail, IsWorldDetailSRGB( GetIntParam( info.m_nDetailTextureCombineMode, params, 0 ) ) ? TEXTUREFLAGS_SRGB : 0 );
	pShader->LoadTexture( info.m_nFlashlightTexture, TEXTUREFLAGS_SRGB );
	if ( IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) || IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
		CLEAR_FLAGS( MATERIAL_VAR_ALPHATEST );
	if ( params[info.m_nEnvmap]->IsDefined() )
	{
		pShader->LoadCubeMap( info.m_nEnvmap, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0 );
		if ( params[info.m_nEnvmapMask]->IsDefined() )
			pShader->LoadTexture( info.m_nEnvmapMask );
	}
	else
	{
		params[info.m_nEnvmapMask]->SetUndefined();
	}
	SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
}

static int WorldDetailMode( IMaterialVar **params, L4D2LightmappedGeneric_DX9_Vars_t &info, bool bump )
{
	if ( !params[info.m_nDetail]->IsTexture() )
		return 12;
	if ( params[info.m_nDetail]->GetTextureValue()->GetFlags() & TEXTUREFLAGS_SSBUMP )
		return bump ? 10 : 11;
	return GetIntParam( info.m_nDetailTextureCombineMode, params, 0 );
}

class CL4D2WorldContext : public CBasePerMaterialContextData
{
public:
	CCommandBufferBuilder< CFixedCommandStorageBuffer< 1000 > > m_Commands;
	bool m_bVertexFastPath;
	bool m_bPixelFastPath;
	bool m_bFastEnvmapContrast;
};

// Native compares preserve unordered values and test the rounded SSBump product.
#pragma float_control( precise, on, push )
static void BuildWorldCommands( CBaseVSShader *pShader, IMaterialVar **params,
	L4D2LightmappedGeneric_DX9_Vars_t &info, CL4D2WorldContext *context,
	bool bump, bool bump2, bool bumpMask, bool base2, bool envmap, bool envmask,
	bool normalMask, bool detail, bool self, bool fancy, bool warp, bool seamless )
{
	context->m_Commands.Reset();
	if ( params[info.m_nBaseTexture]->IsTexture() )
		context->m_Commands.BindTexture( pShader, SHADER_SAMPLER0, info.m_nBaseTexture, info.m_nBaseTextureFrame );
	else
		context->m_Commands.BindStandardTexture( SHADER_SAMPLER0, envmap ? TEXTURE_BLACK : TEXTURE_WHITE );
	context->m_Commands.BindStandardTexture( SHADER_SAMPLER1, TEXTURE_LIGHTMAP );
	if ( base2 )
		context->m_Commands.BindTexture( pShader, SHADER_SAMPLER7, info.m_nBaseTexture2, info.m_nBaseTexture2Frame );
	if ( bump || normalMask )
	{
		if ( g_pConfig->m_bFastNoBump )
			context->m_Commands.BindStandardTexture( SHADER_SAMPLER4, TEXTURE_NORMALMAP_FLAT );
		else
			context->m_Commands.BindTexture( pShader, SHADER_SAMPLER4, info.m_nBumpmap, info.m_nBumpFrame );
	}
	if ( bump2 )
	{
		if ( g_pConfig->m_bFastNoBump )
			context->m_Commands.BindStandardTexture( SHADER_SAMPLER5, TEXTURE_NORMALMAP_FLAT );
		else
			context->m_Commands.BindTexture( pShader, SHADER_SAMPLER5, info.m_nBumpmap2, info.m_nBumpFrame2 );
	}
	else if ( envmask )
		context->m_Commands.BindTexture( pShader, SHADER_SAMPLER5, info.m_nEnvmapMask, info.m_nEnvmapMaskFrame );
	if ( bumpMask )
	{
		if ( g_pConfig->m_bFastNoBump )
			context->m_Commands.BindStandardTexture( SHADER_SAMPLER8, TEXTURE_NORMALMAP_FLAT );
		else
			context->m_Commands.BindTexture( pShader, SHADER_SAMPLER8, info.m_nBumpMask, info.m_nBumpFrame );
	}
	if ( detail )
		context->m_Commands.BindTexture( pShader, SHADER_SAMPLER12, info.m_nDetail, info.m_nDetailFrame );
	if ( fancy )
		context->m_Commands.BindTexture( pShader, SHADER_SAMPLER3, info.m_nBlendModulateTexture, -1 );
	if ( warp )
		context->m_Commands.BindTexture( pShader, SHADER_SAMPLER6, info.m_nLightWarpTexture, -1 );
	context->m_bVertexFastPath = !detail && params[info.m_nBaseTextureTransform]->MatrixIsIdentity() &&
		params[info.m_nBumpTransform]->MatrixIsIdentity() && params[info.m_nBumpTransform2]->MatrixIsIdentity() &&
		params[info.m_nEnvmapMaskTransform]->MatrixIsIdentity();
	if ( seamless )
		context->m_Commands.SetVertexShaderConstant4( 48, GetFloatParam( info.m_nSeamlessMappingScale, params, 0 ), 0, 0, 0 );
	else
		context->m_Commands.SetVertexShaderTextureTransform( 48, info.m_nBaseTextureTransform );
	if ( detail )
		context->m_Commands.SetVertexShaderTextureScaledTransform( 50, info.m_nBaseTextureTransform, info.m_nDetailScale );
	else
		context->m_Commands.SetVertexShaderTextureTransform( 50, info.m_nBumpTransform );
	context->m_Commands.SetVertexShaderTextureTransform( 52, envmask ? info.m_nEnvmapMaskTransform : info.m_nBumpTransform2 );
	context->m_Commands.SetVertexShaderTextureTransform( 14, fancy ? info.m_nBlendMaskTransform : info.m_nBumpTransform );
	float ssBumpScale = params[info.m_nSsBumpMathFix]->IsDefined() ?
		params[info.m_nSsBumpMathFix]->GetFloatValue() : 0.0f;
	ssBumpScale *= 0.57735026f;
	if ( ssBumpScale == 0 )
		ssBumpScale = 1;
	context->m_Commands.SetPixelShaderConstant4( 1, ssBumpScale, 0, 0, 0 );
	float detailTint[4];
	params[info.m_nDetailTint]->GetVecValue( detailTint, 3 );
	detailTint[3] = GetFloatParam( info.m_nDetailTextureBlendFactor, params, 1 );
	context->m_Commands.SetPixelShaderConstant( 8, detailTint );
	const float *selfTint = params[info.m_nSelfIllumTint]->GetVecValue();
	float selfTintRed = selfTint[0];
	float selfTintGreen = selfTint[1];
	float selfTintBlue = selfTint[2];
	float contrast = GetFloatParam( info.m_nEnvmapContrast, params, 0 );
	float saturation = GetFloatParam( info.m_nEnvmapSaturation, params, 1 );
	float fresnel = GetFloatParam( info.m_nFresnelReflection, params, 1 );
	context->m_bPixelFastPath = !(envmap && ((contrast != 0 && contrast != 1) || saturation != 1 || fresnel != 1)) &&
		!(self && (selfTintRed != 1 || selfTintGreen != 1 || selfTintBlue != 1)) && g_pConfig->bShowSpecular;
	context->m_bFastEnvmapContrast = contrast == 1;
	context->m_Commands.SetPixelShaderConstant( 2, params[info.m_nEnvmapContrast]->GetVecValue() );
	context->m_Commands.SetPixelShaderConstant( 3, params[info.m_nEnvmapSaturation]->GetVecValue() );
	context->m_Commands.SetPixelShaderConstant4( 4, 0, 0, 1 - fresnel, fresnel );
	context->m_Commands.SetPixelShaderConstant( 7, selfTint );
	context->m_Commands.SetPixelShaderFogParams( 11 );
	context->m_Commands.End();
	context->m_bMaterialVarsChanged = false;
}
#pragma float_control( pop )

static void DrawWorldFlashlight( CBaseVSShader *pShader, IMaterialVar **params, IShaderDynamicAPI *pShaderAPI,
	IShaderShadow *pShaderShadow, L4D2LightmappedGeneric_DX9_Vars_t &info,
	int bump, bool bump2, bool base2, int detailMode, bool seamless, bool blendTint, bool pixelFog )
{
	bool detail = detailMode != 12;
	int filter = g_pHardwareConfig->GetShadowFilterMode();
	if ( pShaderShadow )
	{
		bool alphaTest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST );
		pShader->SetAdditiveBlendingShadowState( blendTint ? -1 : info.m_nBaseTexture, true );
		pShaderShadow->EnableDepthWrites( false );
		pShaderShadow->EnableAlphaWrites( false );
		if ( alphaTest )
		{
			pShaderShadow->EnableAlphaTest( false );
			pShaderShadow->DepthFunc( SHADER_DEPTHFUNC_EQUAL );
		}
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER2, bump != 2 );
		pShaderShadow->EnableTexture( SHADER_SAMPLER3, bump != 0 );
		pShaderShadow->EnableTexture( SHADER_SAMPLER4, base2 );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER6, bump2 );
		pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
		pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER7 );
		pShaderShadow->EnableTexture( SHADER_SAMPLER8, detail );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, IsWorldDetailSRGB( detailMode ) );
		pShaderShadow->EnableSRGBWrite( true );
		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL;
		if ( bump && !seamless )
			flags |= VERTEX_TANGENT_S | VERTEX_TANGENT_T;
		if ( base2 )
			flags |= VERTEX_COLOR;
		pShaderShadow->VertexShaderVertexFormat( flags, 1, NULL, 0 );
		DECLARE_STATIC_VERTEX_SHADER( l4d2_lightmappedgeneric_flashlight_vs20 );
		SET_STATIC_VERTEX_SHADER_COMBO( NORMALMAP, bump != 0 );
		SET_STATIC_VERTEX_SHADER_COMBO( WORLDVERTEXTRANSITION, base2 );
		SET_STATIC_VERTEX_SHADER_COMBO( SEAMLESS, seamless );
		SET_STATIC_VERTEX_SHADER_COMBO( DETAIL, detail );
		SET_STATIC_VERTEX_SHADER_COMBO( PIXELFOG, pixelFog );
		SET_STATIC_VERTEX_SHADER( l4d2_lightmappedgeneric_flashlight_vs20 );
		DECLARE_STATIC_PIXEL_SHADER( l4d2_lightmappedgeneric_flashlight_ps20b );
		SET_STATIC_PIXEL_SHADER_COMBO( NORMALMAP, bump );
		SET_STATIC_PIXEL_SHADER_COMBO( NORMALMAP2, bump2 && base2 );
		SET_STATIC_PIXEL_SHADER_COMBO( WORLDVERTEXTRANSITION, base2 );
		SET_STATIC_PIXEL_SHADER_COMBO( SEAMLESS, seamless );
		SET_STATIC_PIXEL_SHADER_COMBO( DETAIL, detail );
		SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, detail ? detailMode : 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, filter );
		SET_STATIC_PIXEL_SHADER_COMBO( BLENDTINTBYBASEALPHA, blendTint );
		SET_STATIC_PIXEL_SHADER_COMBO( PIXELFOG, pixelFog );
		SET_STATIC_PIXEL_SHADER( l4d2_lightmappedgeneric_flashlight_ps20b );
		pShader->FogToBlack();
	}
	else
	{
		VMatrix worldToTexture;
		ITexture *depthTexture;
		const FlashlightState_t &state = pShaderAPI->GetFlashlightStateEx( worldToTexture, &depthTexture );
		bool shadows = state.m_bEnableShadows && depthTexture && g_pConfig->ShadowDepthTexture();
		pShader->BindTexture( SHADER_SAMPLER0, state.m_pSpotlightTexture, state.m_nSpotlightTextureFrame );
		if ( params[info.m_nBaseTexture]->IsTexture() )
			pShader->BindTexture( SHADER_SAMPLER1, info.m_nBaseTexture, info.m_nBaseTextureFrame );
		else
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_WHITE );
		pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_NORMALIZATION_CUBEMAP );
		if ( bump )
		{
			if ( g_pConfig->m_bFastNoBump )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_NORMALMAP_FLAT );
			else
				pShader->BindTexture( SHADER_SAMPLER3, info.m_nBumpmap, info.m_nBumpFrame );
		}
		if ( base2 )
			pShader->BindTexture( SHADER_SAMPLER4, info.m_nBaseTexture2, info.m_nBaseTexture2Frame );
		if ( bump2 && base2 )
			pShader->BindTexture( SHADER_SAMPLER6, info.m_nBumpmap2, info.m_nBumpFrame2 );
		if ( detail )
			pShader->BindTexture( SHADER_SAMPLER8, info.m_nDetail, info.m_nDetailFrame );
		if ( shadows )
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_SHADOW_NOISE_2D );
			pShader->BindTexture( SHADER_SAMPLER7, depthTexture, 0 );
		}
		float origin[4] = { state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z, 0 };
		float attenuation[4] = { state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, state.m_FarZ };
		pShaderAPI->SetVertexShaderConstant( 48, origin );
		pShaderAPI->SetVertexShaderConstant( 49, worldToTexture.Base(), 4 );
		pShaderAPI->SetPixelShaderConstant( 13, attenuation );
		pShaderAPI->SetPixelShaderConstant( 14, origin );
		if ( seamless )
		{
			float scale[4] = { GetFloatParam( info.m_nSeamlessMappingScale, params, 0 ), 0, 0, 0 };
			pShaderAPI->SetVertexShaderConstant( 54, scale );
		}
		else
			pShader->SetVertexShaderTextureTransform( 54, info.m_nBaseTextureTransform );
		if ( detail )
			pShader->SetVertexShaderTextureScaledTransform( 56, info.m_nBaseTextureTransform, info.m_nDetailScale );
		else
			pShader->SetVertexShaderTextureTransform( 56, info.m_nBumpTransform );
		float modulation[4];
		pShader->ComputeModulationColor( modulation );
		pShaderAPI->SetPixelShaderConstant( 1, modulation );
		float tint[4];
		params[info.m_nDetailTint]->GetVecValue( tint, 3 );
		tint[3] = GetFloatParam( info.m_nDetailTextureBlendFactor, params, 1 );
		pShaderAPI->SetPixelShaderConstant( 0, tint );
		SetFlashLightColorFromState( state, pShaderAPI, 28 );
		float tweaks[4] = { ShadowFilterFromState( state ), ShadowAttenFromState( state ), 0, 0 };
		pShader->HashShadow2DJitter( state.m_flShadowJitterSeed, &tweaks[2], &tweaks[3] );
		pShaderAPI->SetPixelShaderConstant( 2, tweaks );
		int width, height;
		pShaderAPI->GetBackBufferDimensions( width, height );
		float screen[4] = { width / 32.0f, height / 32.0f, 0, 0 };
		pShaderAPI->SetPixelShaderConstant( 31, screen );
		float eye[4];
		pShaderAPI->GetWorldSpaceCameraPosition( eye );
		eye[3] = 0;
		pShaderAPI->SetPixelShaderConstant( 11, eye );
		pShaderAPI->SetPixelShaderFogParams( 12 );
		DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_lightmappedgeneric_flashlight_vs20 );
		SET_DYNAMIC_VERTEX_SHADER( l4d2_lightmappedgeneric_flashlight_vs20 );
		DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_lightmappedgeneric_flashlight_ps20b );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, shadows );
		SET_DYNAMIC_PIXEL_SHADER( l4d2_lightmappedgeneric_flashlight_ps20b );
	}
	pShader->Draw();
}

// Keep native unordered seamless selection and ordered alpha-reference testing.
#pragma float_control( precise, on, push )
void DrawL4D2LightmappedGeneric_DX9( CBaseVSShader *pShader, IMaterialVar **params, IShaderDynamicAPI *pShaderAPI,
	IShaderShadow *pShaderShadow, L4D2LightmappedGeneric_DX9_Vars_t &info, CBasePerMaterialContextData **pContextDataPtr )
{
	bool base = params[info.m_nBaseTexture]->IsTexture();
	bool base2 = base && params[info.m_nBaseTexture2]->IsTexture();
	bool bump = params[info.m_nBumpmap]->IsTexture() && g_pConfig->UseBumpmapping();
	bool bump2 = bump && params[info.m_nBumpmap2]->IsTexture();
	bool diffuseBump = bump && !GetIntParam( info.m_nNoDiffuseBumpLighting, params, 0 );
	int bumpVariant = bump ? (GetIntParam( info.m_nSelfShadowedBumpFlag, params, 0 ) ? 2 : 1) : 0;
	bool envmap = params[info.m_nEnvmap]->IsTexture();
	bool envmask = envmap && params[info.m_nEnvmapMask]->IsTexture();
	bool baseMask = IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	bool normalMask = g_pConfig->UseSpecular() && IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK );
	bool self = IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
	bool detail = params[info.m_nDetail]->IsTexture();
	bool seamless = GetFloatParam( info.m_nSeamlessMappingScale, params, 0 ) != 0;
	bool fancy = params[info.m_nBlendModulateTexture]->IsTexture();
	bool masked = GetIntParam( info.m_nMaskedBlending, params, 0 );
	bool warp = params[info.m_nLightWarpTexture]->IsTexture();
	bool blendTint = GetIntParam( info.m_nBlendTintByBaseAlpha, params, 0 );
	bool pixelFog = !GetIntParam( info.m_nVertexFog, params, 0 );
	bool baseNoEnv = GetIntParam( info.m_nBaseTextureNoEnvmap, params, 0 );
	bool base2NoEnv = GetIntParam( info.m_nBaseTexture2NoEnvmap, params, 0 );
	bool bumpMask = bump2 && params[info.m_nBumpMask]->IsTexture() && !self && !detail && !base2 &&
		!GetIntParam( info.m_nBaseTextureNoEnvmap, params, 0 );
	int detailMode = WorldDetailMode( params, info, bump );
	if ( pShader->UsingFlashlight( params ) )
	{
		DrawWorldFlashlight( pShader, params, pShaderAPI, pShaderShadow, info, bumpVariant, bump2,
			base2, detailMode, seamless, blendTint, pixelFog );
		return;
	}
	int alphaTexture = blendTint ? -1 : (base ? info.m_nBaseTexture : info.m_nEnvmapMask);
	BlendType_t blend = pShader->EvaluateBlendRequirements( alphaTexture, base );
	bool alphaTest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST );
	bool opaque = blend != BT_BLEND && blend != BT_BLENDADD && !alphaTest;
	if ( pShaderShadow )
	{
		pShaderShadow->EnableAlphaTest( alphaTest );
		float reference = GetFloatParam( info.m_nAlphaTestReference, params, 0 );
		if ( reference > 0 )
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, reference );
		pShader->SetDefaultBlendingShadowState( alphaTexture, base );
		pShaderShadow->EnableAlphaWrites( opaque );
		pShaderShadow->EnableSRGBWrite( true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
		pShaderShadow->EnableTexture( SHADER_SAMPLER2, envmap );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
		pShaderShadow->EnableTexture( SHADER_SAMPLER3, fancy );
		pShaderShadow->EnableTexture( SHADER_SAMPLER4, bump || normalMask );
		pShaderShadow->EnableTexture( SHADER_SAMPLER5, bump2 || envmask );
		pShaderShadow->EnableTexture( SHADER_SAMPLER6, warp );
		pShaderShadow->EnableTexture( SHADER_SAMPLER7, base2 );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER7, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER8, bumpMask );
		pShaderShadow->EnableTexture( SHADER_SAMPLER12, detail );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER12, IsWorldDetailSRGB( detailMode ) );
		// The same snapshot also serves the dynamic normal/depth preview.
		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T;
		if ( IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) || base2 || bump2 )
			flags |= VERTEX_COLOR;
		pShaderShadow->VertexShaderVertexFormat( flags, bump ? 3 : 2, NULL, 0 );
		DECLARE_STATIC_VERTEX_SHADER( l4d2_lightmappedgeneric_vs20 );
		SET_STATIC_VERTEX_SHADER_COMBO( ENVMAP_MASK, envmask );
		SET_STATIC_VERTEX_SHADER_COMBO( TANGENTSPACE, envmap );
		SET_STATIC_VERTEX_SHADER_COMBO( BUMPMAP, bump );
		SET_STATIC_VERTEX_SHADER_COMBO( DETAILTEXTURE, detail );
		SET_STATIC_VERTEX_SHADER_COMBO( DIFFUSEBUMPMAP, diffuseBump );
		SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) );
		SET_STATIC_VERTEX_SHADER_COMBO( VERTEXALPHATEXBLENDFACTOR, base2 || bump2 );
		SET_STATIC_VERTEX_SHADER_COMBO( SEAMLESS, seamless );
		SET_STATIC_VERTEX_SHADER_COMBO( BUMPMASK, bumpMask );
		SET_STATIC_VERTEX_SHADER_COMBO( FANCY_BLENDING, fancy );
		SET_STATIC_VERTEX_SHADER_COMBO( SELFILLUM, self );
		SET_STATIC_VERTEX_SHADER_COMBO( PIXELFOG, pixelFog );
		SET_STATIC_VERTEX_SHADER( l4d2_lightmappedgeneric_vs20 );
		DECLARE_STATIC_PIXEL_SHADER( l4d2_lightmappedgeneric_ps20b );
		SET_STATIC_PIXEL_SHADER_COMBO( MASKEDBLENDING, masked );
		SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2, base2 );
		SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP, bumpVariant );
		SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP2, bump2 );
		SET_STATIC_PIXEL_SHADER_COMBO( CUBEMAP, envmap );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPMASK, envmask );
		SET_STATIC_PIXEL_SHADER_COMBO( BASEALPHAENVMAPMASK, baseMask );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM, self );
		SET_STATIC_PIXEL_SHADER_COMBO( NORMALMAPALPHAENVMAPMASK, normalMask );
		SET_STATIC_PIXEL_SHADER_COMBO( DIFFUSEBUMPMAP, diffuseBump );
		SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURENOENVMAP, baseNoEnv );
		SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2NOENVMAP, base2NoEnv );
		SET_STATIC_PIXEL_SHADER_COMBO( WARPLIGHTING, warp );
		SET_STATIC_PIXEL_SHADER_COMBO( FANCY_BLENDING, fancy );
		SET_STATIC_PIXEL_SHADER_COMBO( SEAMLESS, seamless );
		SET_STATIC_PIXEL_SHADER_COMBO( BUMPMASK, bumpMask );
		SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, detailMode );
		SET_STATIC_PIXEL_SHADER_COMBO( BLENDTINTBYBASEALPHA, blendTint );
		SET_STATIC_PIXEL_SHADER_COMBO( PIXELFOG, pixelFog );
		SET_STATIC_PIXEL_SHADER( l4d2_lightmappedgeneric_ps20b );
		pShader->DefaultFog();
	}
	else
	{
		CL4D2WorldContext *context = static_cast<CL4D2WorldContext *>( *pContextDataPtr );
		if ( !context )
		{
			context = new CL4D2WorldContext;
			*pContextDataPtr = context;
		}
		if ( context->m_bMaterialVarsChanged )
			BuildWorldCommands( pShader, params, info, context, bump, bump2, bumpMask, base2, envmap,
				envmask, normalMask, detail, self, fancy, warp, seamless );
		pShaderAPI->ExecuteCommandBuffer( context->m_Commands.Base() );
		static ConVarRef mat_fullbright( "mat_fullbright" );
		if ( envmap )
		{
			pShader->BindTexture( SHADER_SAMPLER2, info.m_nEnvmap, info.m_nEnvmapFrame );
			float tint[4] = { 0, 0, 0, 0 };
			if ( g_pConfig->bShowSpecular && mat_fullbright.GetInt() != 2 )
				params[info.m_nEnvmapTint]->GetVecValue( tint, 3 );
			pShaderAPI->SetPixelShaderConstant( 0, tint );
		}
		float modulation[4];
		pShader->ComputeModulationColor( modulation );
		pShaderAPI->SetVertexShaderConstant( 47, modulation );
		float scale = pShaderAPI->GetLightMapScaleFactor();
		float lightmapTint[4] = { modulation[0] * scale, modulation[1] * scale, modulation[2] * scale, scale };
		if ( mat_fullbright.GetInt() == 2 )
		{
			lightmapTint[0] = lightmapTint[1] = lightmapTint[2] = lightmapTint[3] = 1;
		}
		pShaderAPI->SetPixelShaderConstant( 12, lightmapTint );
		if ( mat_fullbright.GetInt() == 2 && !IS_FLAG_SET( MATERIAL_VAR_NO_DEBUG_OVERRIDE ) )
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, self ? TEXTURE_GREY_ALPHA_ZERO : TEXTURE_GREY );
			float black[4] = { 0, 0, 0, 0 };
			pShaderAPI->SetVertexShaderConstant( 47, black );
		}
		float eye[4];
		pShaderAPI->GetWorldSpaceCameraPosition( eye );
		eye[3] = 0;
		pShaderAPI->SetPixelShaderConstant( 10, eye );
		bool waterAlpha = opaque && pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_L4D2_WRITE_WATER_FOG_TO_DESTALPHA );
		bool depthAlpha = opaque && !waterAlpha && pShaderAPI->ShouldWriteDepthToDestAlpha();
		if ( waterAlpha )
		{
			Vector range = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_L4D2_WATER_FOG_RANGE );
			float reciprocal[4] = { 1.0f / (range.y - range.x), 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 22, reciprocal );
		}
		int preview = pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_ENABLE_FIXED_LIGHTING );
		if ( preview == ENABLE_FIXED_LIGHTING_OUTPUTNORMAL_AND_DEPTH )
		{
			float view[16], projection[16];
			pShaderAPI->GetMatrix( MATERIAL_VIEW, view );
			pShaderAPI->GetMatrix( MATERIAL_PROJECTION, projection );
			// Native API029's camera vector is the negative view Z axis.
			// Its far-plane getter preserves this projection arithmetic.
			float nearZ = projection[14] / projection[10];
			float farZ = (nearZ * projection[14]) / (nearZ + projection[14]);
			float reciprocalFar = 1.0f / farZ;
			float eyeVector[4] = { -view[2] * reciprocalFar, -view[6] * reciprocalFar,
				-view[10] * reciprocalFar, 1 };
			pShaderAPI->SetVertexShaderConstant( 12, eyeVector );
		}
		DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_lightmappedgeneric_vs20 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( FASTPATH, !preview && context->m_bVertexFastPath );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( LIGHTING_PREVIEW, preview );
		SET_DYNAMIC_VERTEX_SHADER( l4d2_lightmappedgeneric_vs20 );
		bool pixelFast = !preview && context->m_bPixelFastPath;
		DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_lightmappedgeneric_ps20b );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( FASTPATH, pixelFast );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( FASTPATHENVMAPCONTRAST, pixelFast && context->m_bFastEnvmapContrast );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( LIGHTING_PREVIEW, preview );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITEWATERFOGTODESTALPHA, waterAlpha );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, depthAlpha );
		SET_DYNAMIC_PIXEL_SHADER( l4d2_lightmappedgeneric_ps20b );
	}
	pShader->Draw();
}
#pragma float_control( pop )
