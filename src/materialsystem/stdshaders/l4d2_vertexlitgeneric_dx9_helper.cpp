//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "l4d2_vertexlitgeneric_dx9_helper.h"
#include "l4d2_phong_dx9_helper.h"
#include "renderparm.h"
#include "commandbuilder.h"
#include "l4d2_vertexlit_and_unlit_generic_vs20.inc"
#include "l4d2_vertexlit_and_unlit_generic_ps20b.inc"
#include "l4d2_vertexlit_and_unlit_generic_bump_vs20.inc"
#include "l4d2_vertexlit_and_unlit_generic_bump_ps20b.inc"
#include "tier0/memdbgon.h"

static bool WantsPhong( IMaterialVar **params, const L4D2VertexLitGeneric_DX9_Vars_t &info )
{
	// ShaderDLL::Connect supplies g_pCVar after this DLL's global initializers.
	static ConVarRef mat_phong( "mat_phong" );
	if ( !mat_phong.GetBool() || !GetIntParam( info.m_nPhong, params, 0 ) )
		return false;
	return params[info.m_nDiffuseWarpTexture]->IsTexture() ||
		GetIntParam( info.m_nBaseMapAlphaPhongMask, params, 0 ) == 1 || params[info.m_nBumpmap]->IsDefined();
}

void InitParamsL4D2VertexLitGeneric_DX9( CBaseVSShader *pShader, IMaterialVar **params, const char *pMaterialName,
	bool bVertexLitGeneric, L4D2VertexLitGeneric_DX9_Vars_t &info )
{
	InitIntParam( info.m_nPhong, params, 0 );
	InitIntParam( info.m_nBlendTintByBaseAlpha, params, 0 );
	InitFloatParam( info.m_nAlphaTestReference, params, 0 );
	InitIntParam( info.m_nAllowFenceRenderStateHack, params, 0 );
	InitIntParam( info.m_nFlashlightNoLambert, params, 0 );
	InitIntParam( info.m_nShaderSRGBRead360, params, 0 );
	InitIntParam( info.m_nAllowDiffuseModulation, params, 1 );
	InitIntParam( info.m_nEnvmapFrame, params, 0 );
	InitIntParam( info.m_nBumpFrame, params, 0 );
	InitIntParam( info.m_nDetailFrame, params, 0 );
	InitIntParam( info.m_nDetailTextureCombineMode, params, 0 );
	InitFloatParam( info.m_nDetailTextureBlendFactor, params, 1 );
	InitFloatParam( info.m_nDetailScale, params, 4 );
	if ( !params[info.m_nEnvmapTint]->IsDefined() )
		params[info.m_nEnvmapTint]->SetVecValue( 1, 1, 1 );
	if ( !params[info.m_nDetailTint]->IsDefined() )
		params[info.m_nDetailTint]->SetVecValue( 1, 1, 1 );
	if ( !params[info.m_nSelfIllumTint]->IsDefined() )
		params[info.m_nSelfIllumTint]->SetVecValue( 1, 1, 1 );
	if ( WantsPhong( params, info ) )
	{
		if ( g_pHardwareConfig->GetDXSupportLevel() >= 92 )
		{
			InitParamsL4D2Phong_DX9( pShader, params, pMaterialName, info );
			return;
		}
		params[info.m_nPhong]->SetIntValue( 0 );
	}
	params[info.m_nFlashlightTexture]->SetStringValue( "effects/flashlight001" );
	if ( params[info.m_nAlbedo]->IsDefined() && params[info.m_nBaseTexture]->IsDefined() &&
		g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() )
		params[info.m_nBaseTexture]->SetStringValue( params[info.m_nAlbedo]->GetStringValue() );
	SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
	if ( bVertexLitGeneric )
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
	InitIntParam( info.m_nEnvmapMaskFrame, params, 0 );
	InitFloatParam( info.m_nEnvmapContrast, params, 0 );
	InitFloatParam( info.m_nEnvmapSaturation, params, 1 );
	InitFloatParam( info.m_nEnvmapFresnel, params, 0 );
	InitFloatParam( info.m_nSeamlessScale, params, 0 );
	InitIntParam( info.m_nSeamlessBase, params, 0 );
	InitIntParam( info.m_nSeamlessDetail, params, 0 );
	InitIntParam( info.m_nSeparateDetailUVs, params, 0 );
	InitIntParam( info.m_nSelfIllumFresnel, params, 0 );
	if ( !params[info.m_nSelfIllumFresnelMinMaxExp]->IsDefined() )
		params[info.m_nSelfIllumFresnelMinMaxExp]->SetVecValue( 0, 1, 1 );
	if ( !params[info.m_nEnvmapFresnelMinMaxExp]->IsDefined() )
		params[info.m_nEnvmapFresnelMinMaxExp]->SetVecValue( 0, 1, 2 );
	if ( !params[info.m_nBaseAlphaEnvmapMaskMinMaxExp]->IsDefined() )
		params[info.m_nBaseAlphaEnvmapMaskMinMaxExp]->SetVecValue( 1, 0, 1 );
	InitFloatParam( info.m_nMaxFogDensityScalar, params, 1 );
	InitIntParam( info.m_nVertexFog, params, 0 );
	InitIntParam( info.m_nAmbientOnly, params, 0 );
	InitIntParam( info.m_nSelfIllumEnvMapMask_Alpha, params, 0 );
	InitIntParam( info.m_nTreeSway, params, 0 );
	InitFloatParam( info.m_nTreeSwayHeight, params, 1000 );
	InitFloatParam( info.m_nTreeSwayStartHeight, params, .1f );
	InitFloatParam( info.m_nTreeSwayRadius, params, 300 );
	InitFloatParam( info.m_nTreeSwayStartRadius, params, .2f );
	InitFloatParam( info.m_nTreeSwaySpeed, params, 1 );
	InitFloatParam( info.m_nTreeSwaySpeedHighWindMultiplier, params, 2 );
	InitFloatParam( info.m_nTreeSwayStrength, params, 10 );
	InitFloatParam( info.m_nTreeSwayScrumbleSpeed, params, 5 );
	InitFloatParam( info.m_nTreeSwayScrumbleStrength, params, 10 );
	InitFloatParam( info.m_nTreeSwayScrumbleFrequency, params, 12 );
	InitFloatParam( info.m_nTreeSwayFalloffExp, params, 1.5f );
	InitFloatParam( info.m_nTreeSwayScrumbleFalloffExp, params, 1 );
	InitFloatParam( info.m_nTreeSwaySpeedLerpStart, params, 3 );
	InitFloatParam( info.m_nTreeSwaySpeedLerpEnd, params, 6 );
	if ( params[info.m_nBumpmap]->IsDefined() || params[info.m_nDiffuseWarpTexture]->IsDefined() )
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
	if ( !params[info.m_nBaseTexture]->IsDefined() )
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	if ( IS_FLAG_SET( MATERIAL_VAR_DECAL ) )
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
}

void InitL4D2VertexLitGeneric_DX9( CBaseVSShader *pShader, IMaterialVar **params, bool bVertexLitGeneric,
	L4D2VertexLitGeneric_DX9_Vars_t &info )
{
	static ConVarRef mat_phong( "mat_phong" );
	// Native resource init tests the authored Phong switch, not draw eligibility.
	if ( GetIntParam( info.m_nPhong, params, 0 ) && mat_phong.GetBool() && g_pHardwareConfig->GetDXSupportLevel() >= 92 )
	{
		InitL4D2Phong_DX9( pShader, params, info );
		return;
	}
	pShader->LoadTexture( info.m_nFlashlightTexture, TEXTUREFLAGS_SRGB );
	bool bBaseAlpha = false;
	if ( params[info.m_nBaseTexture]->IsDefined() )
	{
		pShader->LoadTexture( info.m_nBaseTexture, TEXTUREFLAGS_SRGB );
		bBaseAlpha = params[info.m_nBaseTexture]->GetTextureValue()->IsTranslucent();
	}
	bool bSelfMask = IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) && params[info.m_nSelfIllumMask]->IsDefined();
	if ( !bBaseAlpha )
	{
		if ( !bSelfMask && !GetIntParam( info.m_nSelfIllumFresnel, params, 0 ) )
			CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	}
	if ( params[info.m_nDetail]->IsDefined() )
	{
		int mode = GetIntParam( info.m_nDetailTextureCombineMode, params, 0 );
		pShader->LoadTexture( info.m_nDetail, mode == 2 || mode == 3 || mode == 4 ? TEXTUREFLAGS_SRGB : 0 );
	}
	if ( g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() )
	{
		pShader->LoadBumpMap( info.m_nBumpmap );
		SET_FLAGS2( MATERIAL_VAR2_DIFFUSE_BUMPMAPPED_MODEL );
	}
	if ( params[info.m_nDiffuseWarpTexture]->IsDefined() )
		pShader->LoadTexture( info.m_nDiffuseWarpTexture );
	if ( (IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) && !bSelfMask) ||
		IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
		CLEAR_FLAGS( MATERIAL_VAR_ALPHATEST );
	if ( params[info.m_nEnvmap]->IsDefined() )
		pShader->LoadCubeMap( info.m_nEnvmap, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0 );
	if ( params[info.m_nEnvmapMask]->IsDefined() )
		pShader->LoadTexture( info.m_nEnvmapMask );
	if ( bSelfMask )
		pShader->LoadTexture( info.m_nSelfIllumMask );
}

class CL4D2GenericContext : public CBasePerMaterialContextData
{
public:
	CCommandBufferBuilder< CFixedCommandStorageBuffer< 800 > > m_Commands;
};

static void BuildGenericCommands( CBaseVSShader *pShader, IMaterialVar **params, L4D2VertexLitGeneric_DX9_Vars_t &info,
	CL4D2GenericContext *pContext, bool bDetail, bool bEnvmap, bool bSelfIllum, bool bEnvmapMask, bool bTree )
{
	pContext->m_Commands.Reset();
	if ( params[info.m_nBaseTexture]->IsTexture() )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER0, info.m_nBaseTexture, info.m_nBaseTextureFrame );
	else
		pContext->m_Commands.BindStandardTexture( SHADER_SAMPLER0, bEnvmap ? TEXTURE_BLACK : TEXTURE_WHITE );
	if ( bDetail )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER2, info.m_nDetail, info.m_nDetailFrame );
	if ( bEnvmapMask )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER4, info.m_nEnvmapMask, info.m_nEnvmapMaskFrame );
	bool bSelfMask = bSelfIllum && params[info.m_nSelfIllumMask]->IsTexture();
	if ( bSelfMask )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER11, info.m_nSelfIllumMask, -1 );
	else if ( bSelfIllum )
		pContext->m_Commands.BindStandardTexture( SHADER_SAMPLER11, TEXTURE_BLACK );
	pContext->m_Commands.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, info.m_nBaseTextureTransform );
	if ( bDetail )
	{
		pContext->m_Commands.SetVertexShaderTextureScaledTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4,
			IS_PARAM_DEFINED( info.m_nDetailTextureTransform ) ? info.m_nDetailTextureTransform : info.m_nBaseTextureTransform, info.m_nDetailScale );
		pContext->m_Commands.SetPixelShaderConstantGammaToLinear( 10, info.m_nDetailTint );
	}
	float saturation[4];
	params[info.m_nEnvmapSaturation]->GetVecValue( saturation, 3 );
	saturation[3] = bSelfMask ? 1.0f : 0.0f;
	pContext->m_Commands.SetPixelShaderConstant( 3, saturation );
	float self[4];
	params[info.m_nSelfIllumTint]->GetVecValue( self, 3 );
	self[3] = GetFloatParam( info.m_nDetailTextureBlendFactor, params, 1 );
	pContext->m_Commands.SetPixelShaderConstant( 4, self );
	const float *pBaseMask = params[info.m_nBaseAlphaEnvmapMaskMinMaxExp]->GetVecValue();
	pContext->m_Commands.SetPixelShaderConstant4( 7, 0, 0, pBaseMask[0], pBaseMask[1] - pBaseMask[0] );
	const float *pFresnel = params[info.m_nEnvmapFresnelMinMaxExp]->GetVecValue();
	pContext->m_Commands.SetPixelShaderConstant4( 13, pFresnel[0], pFresnel[1] - pFresnel[0], pFresnel[2], pBaseMask[2] );
	if ( GetIntParam( info.m_nSeamlessBase, params, 0 ) || GetIntParam( info.m_nSeamlessDetail, params, 0 ) )
		pContext->m_Commands.SetVertexShaderConstant4( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3,
			GetFloatParam( info.m_nSeamlessScale, params, 0 ), 0, 0, 0 );
	if ( bTree )
	{
		pContext->m_Commands.SetVertexShaderConstant4( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3,
			GetFloatParam( info.m_nTreeSwaySpeedHighWindMultiplier, params ), GetFloatParam( info.m_nTreeSwayScrumbleFalloffExp, params ),
			GetFloatParam( info.m_nTreeSwayFalloffExp, params ), GetFloatParam( info.m_nTreeSwayScrumbleSpeed, params ) );
		pContext->m_Commands.SetVertexShaderConstant4( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4,
			GetFloatParam( info.m_nTreeSwaySpeedLerpStart, params ), GetFloatParam( info.m_nTreeSwaySpeedLerpEnd, params ), 0, 0 );
		pContext->m_Commands.SetVertexShaderConstant4( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10,
			GetFloatParam( info.m_nTreeSwayHeight, params ), GetFloatParam( info.m_nTreeSwayStartHeight, params ),
			GetFloatParam( info.m_nTreeSwayRadius, params ), GetFloatParam( info.m_nTreeSwayStartRadius, params ) );
		pContext->m_Commands.SetVertexShaderConstant4( VERTEX_SHADER_SHADER_SPECIFIC_CONST_11,
			GetFloatParam( info.m_nTreeSwaySpeed, params ), GetFloatParam( info.m_nTreeSwayStrength, params ),
			GetFloatParam( info.m_nTreeSwayScrumbleFrequency, params ), GetFloatParam( info.m_nTreeSwayScrumbleStrength, params ) );
	}
	pContext->m_Commands.SetPixelShaderFogParams( 21 );
	pContext->m_Commands.End();
	pContext->m_bMaterialVarsChanged = false;
}

void DrawL4D2VertexLitGeneric_DX9( CBaseVSShader *pShader, IMaterialVar **params, IShaderDynamicAPI *pShaderAPI,
	IShaderShadow *pShaderShadow, bool bVertexLitGeneric, L4D2VertexLitGeneric_DX9_Vars_t &info,
	VertexCompressionType_t vertexCompression, CBasePerMaterialContextData **pContextDataPtr )
{
	if ( WantsPhong( params, info ) && g_pHardwareConfig->GetDXSupportLevel() >= 92 )
	{
		DrawL4D2Phong_DX9( pShader, params, pShaderAPI, pShaderShadow, info, vertexCompression, pContextDataPtr );
		return;
	}
	bool flashlight = pShader->UsingFlashlight( params );
	int tree = GetIntParam( info.m_nTreeSway, params, 0 );
	bool detail = !tree && params[info.m_nDetail]->IsTexture();
	bool self = !flashlight && IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
	bool selfFresnel = self && !detail && GetIntParam( info.m_nSelfIllumFresnel, params, 0 );
	bool warp = !flashlight && params[info.m_nDiffuseWarpTexture]->IsTexture();
	bool bump = (g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsTexture()) || warp;
	bool envmap = !flashlight && params[info.m_nEnvmap]->IsTexture();
	bool envmask = envmap && params[info.m_nEnvmapMask]->IsTexture();
	bool baseMask = envmap && IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	bool normalMask = IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK );
	bool blendTint = !self && GetIntParam( info.m_nBlendTintByBaseAlpha, params, 0 );
	bool alphaTest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST );
	bool halfLambert = IS_FLAG_SET( MATERIAL_VAR_HALFLAMBERT );
	bool vertexColor = !bVertexLitGeneric && IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR );
	bool vertexAlpha = IS_FLAG_SET( MATERIAL_VAR_VERTEXALPHA );
	bool pixelFog = !GetIntParam( info.m_nVertexFog, params, 0 );
	bool seamlessBase = !tree && GetIntParam( info.m_nSeamlessBase, params, 0 );
	bool seamlessDetail = !tree && GetIntParam( info.m_nSeamlessDetail, params, 0 );
	int detailMode = detail ? GetIntParam( info.m_nDetailTextureCombineMode, params, 0 ) : 0;
	int filter = flashlight ? g_pHardwareConfig->GetShadowFilterMode() : 0;
	if ( bump )
	{
		// Native's radix7 does not clamp an authored detail mode7: it
		// carries into the filter dimension. Retained23443 aliases21651.
		filter = (filter + detailMode / 7) % 3;
		detailMode %= 7;
		if ( !flashlight )
			filter = 0; // Native cache aliases away non-flashlight filter bits.
	}
	BlendType_t blend = pShader->EvaluateBlendRequirements( blendTint ? -1 : info.m_nBaseTexture, true );
	bool opaque = blend != BT_BLEND && blend != BT_BLENDADD && !alphaTest && !flashlight;
	if ( pShaderShadow )
	{
		pShaderShadow->EnableAlphaTest( alphaTest );
		float reference = GetFloatParam( info.m_nAlphaTestReference, params, 0 );
		if ( reference > 0 )
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, reference );
		if ( flashlight )
		{
			pShader->SetAdditiveBlendingShadowState( info.m_nBaseTexture, true );
			pShaderShadow->EnableDepthWrites( false );
			// Native 1003AC10 retains the cutout alpha test with depth equality.
			if ( alphaTest )
				pShaderShadow->DepthFunc( SHADER_DEPTHFUNC_EQUAL );
		}
		else
		{
			pShader->SetDefaultBlendingShadowState( info.m_nBaseTexture, true );
			if ( GetIntParam( info.m_nAllowFenceRenderStateHack, params, 0 ) )
			{
				pShader->EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
				pShaderShadow->EnableDepthWrites( true );
			}
		}
		pShaderShadow->EnableAlphaWrites( opaque );
		pShaderShadow->EnableSRGBWrite( GetIntParam( info.m_nLinearWrite, params, 0 ) != 1 );
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
		if ( detail )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, detailMode == 2 || detailMode == 3 || detailMode == 4 );
		}
		if ( envmap )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
		}
		if ( envmask )
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
		if ( bump )
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
		if ( warp )
			pShaderShadow->EnableTexture( SHADER_SAMPLER9, true );
		if ( self )
			pShaderShadow->EnableTexture( SHADER_SAMPLER11, true );
		if ( flashlight )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER7, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
			pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER8 );
		}
		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
		if ( vertexColor || vertexAlpha )
			flags |= VERTEX_COLOR;
		int coordinates[2] = { 2, 2 };
		pShaderShadow->VertexShaderVertexFormat( flags, GetIntParam( info.m_nSeparateDetailUVs, params, 0 ) ? 2 : 1, coordinates, bump ? 4 : 0 );
		if ( bump )
		{
			DECLARE_STATIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_bump_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( HALFLAMBERT, halfLambert );
			SET_STATIC_VERTEX_SHADER_COMBO( NORMALIZEEYEVEC, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( USE_STATIC_CONTROL_FLOW, g_pHardwareConfig->SupportsStaticControlFlow() );
			SET_STATIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_bump_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_bump_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( CUBEMAP, envmap );
			SET_STATIC_PIXEL_SHADER_COMBO( DIFFUSELIGHTING, bVertexLitGeneric );
			SET_STATIC_PIXEL_SHADER_COMBO( LIGHTWARPTEXTURE, warp );
			SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM, self );
			SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUMFRESNEL, 0 );
			SET_STATIC_PIXEL_SHADER_COMBO( NORMALMAPALPHAENVMAPMASK, normalMask || selfFresnel );
			SET_STATIC_PIXEL_SHADER_COMBO( HALFLAMBERT, halfLambert );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHT, flashlight );
			SET_STATIC_PIXEL_SHADER_COMBO( DETAILTEXTURE, detail );
			SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, detailMode );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, filter );
			SET_STATIC_PIXEL_SHADER_COMBO( BLENDTINTBYBASEALPHA, blendTint );
			SET_STATIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_bump_ps20b );
		}
		else
		{
			DECLARE_STATIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, vertexColor || vertexAlpha );
			SET_STATIC_VERTEX_SHADER_COMBO( CUBEMAP, envmap );
			SET_STATIC_VERTEX_SHADER_COMBO( HALFLAMBERT, halfLambert );
			SET_STATIC_VERTEX_SHADER_COMBO( FLASHLIGHT, flashlight );
			SET_STATIC_VERTEX_SHADER_COMBO( SEAMLESS_BASE, seamlessBase );
			SET_STATIC_VERTEX_SHADER_COMBO( SEAMLESS_DETAIL, seamlessDetail );
			SET_STATIC_VERTEX_SHADER_COMBO( SEPARATE_DETAIL_UVS, GetIntParam( info.m_nSeparateDetailUVs, params, 0 ) );
			SET_STATIC_VERTEX_SHADER_COMBO( USE_STATIC_CONTROL_FLOW, g_pHardwareConfig->SupportsStaticControlFlow() );
			SET_STATIC_VERTEX_SHADER_COMBO( TREESWAY, tree );
			SET_STATIC_VERTEX_SHADER_COMBO( PIXELFOG, pixelFog );
			SET_STATIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( DETAILTEXTURE, detail );
			SET_STATIC_PIXEL_SHADER_COMBO( CUBEMAP, envmap );
			SET_STATIC_PIXEL_SHADER_COMBO( DIFFUSELIGHTING, bVertexLitGeneric );
			SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPMASK, envmask );
			SET_STATIC_PIXEL_SHADER_COMBO( BASEALPHAENVMAPMASK, baseMask );
			SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM, self );
			SET_STATIC_PIXEL_SHADER_COMBO( VERTEXCOLOR, vertexColor );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHT, flashlight );
			SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM_ENVMAPMASK_ALPHA, envmask && GetIntParam( info.m_nSelfIllumEnvMapMask_Alpha, params, 0 ) );
			SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, detailMode );
			SET_STATIC_PIXEL_SHADER_COMBO( SEAMLESS_BASE, seamlessBase );
			SET_STATIC_PIXEL_SHADER_COMBO( SEAMLESS_DETAIL, seamlessDetail );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, filter );
			SET_STATIC_PIXEL_SHADER_COMBO( BLENDTINTBYBASEALPHA, blendTint );
			SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPFRESNEL, envmap && GetFloatParam( info.m_nEnvmapFresnel, params, 0 ) != 0 );
			SET_STATIC_PIXEL_SHADER_COMBO( PIXELFOG, pixelFog );
			SET_STATIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_ps20b );
		}
		if ( flashlight )
			pShader->FogToBlack();
		else
			pShader->DefaultFog();
	}
	else
	{
		CL4D2GenericContext *pContext = static_cast<CL4D2GenericContext *>( *pContextDataPtr );
		if ( !pContext )
		{
			pContext = new CL4D2GenericContext;
			*pContextDataPtr = pContext;
		}
		if ( pContext->m_bMaterialVarsChanged )
			BuildGenericCommands( pShader, params, info, pContext, detail, envmap, self, envmask, tree != 0 );
		pShaderAPI->ExecuteCommandBuffer( pContext->m_Commands.Base() );
		if ( bump )
		{
			if ( !g_pConfig->m_bFastNoBump && params[info.m_nBumpmap]->IsTexture() )
				pShader->BindTexture( SHADER_SAMPLER3, info.m_nBumpmap, info.m_nBumpFrame );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_NORMALMAP_FLAT );
		}
		if ( warp )
		{
			static ConVarRef r_lightwarpidentity( "r_lightwarpidentity" );
			if ( r_lightwarpidentity.GetBool() )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER9, TEXTURE_IDENTITY_LIGHTWARP );
			else
				pShader->BindTexture( SHADER_SAMPLER9, info.m_nDiffuseWarpTexture );
		}
		if ( envmap )
			pShader->BindTexture( SHADER_SAMPLER1, info.m_nEnvmap, info.m_nEnvmapFrame );
		LightState_t lights = { 0, false, false, false };
		if ( !flashlight )
			pShaderAPI->GetDX9LightState( &lights );
		if ( GetIntParam( info.m_nAmbientOnly, params, 0 ) )
			lights.m_nNumLights = 0;
		bool shadows = false;
		float tint[4] = { 0, 0, 0, 0 };
		if ( envmap )
			params[info.m_nEnvmapTint]->GetLinearVecValue( tint, 3 );
		pShaderAPI->SetPixelShaderConstant( 0, tint );
		if ( !flashlight )
			pShader->SetPixelShaderConstant( 2, info.m_nEnvmapContrast );
		if ( flashlight )
		{
			VMatrix worldToTexture;
			ITexture *pDepth;
			const FlashlightState_t &state = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pDepth );
			shadows = state.m_bEnableShadows && pDepth && g_pConfig->ShadowDepthTexture();
			if ( shadows )
			{
				pShader->BindTexture( SHADER_SAMPLER8, pDepth, 0 );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_SHADOW_NOISE_2D );
			}
			SetFlashLightColorFromState( state, pShaderAPI, 28, GetIntParam( info.m_nFlashlightNoLambert, params, 0 ) != 0 );
			pShader->BindTexture( SHADER_SAMPLER7, state.m_pSpotlightTexture, state.m_nSpotlightTextureFrame );
			float atten[4] = { state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, state.m_FarZ };
			float origin[4] = { state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z, 0 };
			pShaderAPI->SetPixelShaderConstant( 22, atten );
			pShaderAPI->SetPixelShaderConstant( 23, origin );
			if ( bump )
				pShaderAPI->SetPixelShaderConstant( 24, worldToTexture.Base(), 4 );
			else
				pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_6, worldToTexture.Base(), 4 );
			float tweaks[4] = { ShadowFilterFromState( state ), ShadowAttenFromState( state ), 0, 0 };
			pShader->HashShadow2DJitter( state.m_flShadowJitterSeed, &tweaks[2], &tweaks[3] );
			pShaderAPI->SetPixelShaderConstant( 2, tweaks );
			int width, height;
			pShaderAPI->GetBackBufferDimensions( width, height );
			float screen[4] = { width / 32.0f, height / 32.0f, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 31, screen );
		}
		pShader->SetAmbientCubeDynamicStateVertexShader();
		if ( bump )
		{
			pShaderAPI->SetPixelShaderStateAmbientLightCube( 5, !lights.m_bAmbientLight );
			pShaderAPI->CommitPixelShaderLighting( 13 );
		}
		float eye[4];
		pShaderAPI->GetWorldSpaceCameraPosition( eye );
		eye[3] = params[info.m_nBaseTexture]->IsTexture() && params[info.m_nBaseTexture]->GetTextureValue()->IsTranslucent() ? 1.0f : 0.0f;
		pShaderAPI->SetPixelShaderConstant( 20, eye );
		float fog[4] = { GetFloatParam( info.m_nMaxFogDensityScalar, params, 1 ), 0, 0, 0 };
		bool waterAlpha = opaque && pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_L4D2_WRITE_WATER_FOG_TO_DESTALPHA );
		if ( waterAlpha )
		{
			Vector range = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_L4D2_WATER_FOG_RANGE );
			fog[1] = 1.0f / (range.y - range.x);
		}
		pShaderAPI->SetPixelShaderConstant( 19, fog );
		float controls[4] = { 0, 0, 0, vertexAlpha ? 1.0f : 0.0f };
		pShaderAPI->SetPixelShaderConstant( 12, controls );
		float modulation[4];
		pShader->ComputeModulationColor( modulation );
		if ( GetIntParam( info.m_nAllowDiffuseModulation, params, 1 ) )
		{
			// Native 1003AC10 leaves modulation raw for $linearwrite 1.
			if ( GetIntParam( info.m_nLinearWrite, params, 0 ) != 1 )
			{
				for ( int i = 0; i < 3; ++i )
					modulation[i] = modulation[i] > 1 ? modulation[i] : GammaToLinear( modulation[i] );
			}
		}
		else
			modulation[0] = modulation[1] = modulation[2] = 1;
		pShaderAPI->SetPixelShaderConstant( 1, modulation );
		if ( tree || flashlight )
		{
			Vector wind = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_WIND_DIRECTION );
			float values[4] = { 0, pShaderAPI->GetFloatRenderingParameter( FLOAT_RENDERPARM_L4D2_CURRENT_TIME ), wind.x, wind.y };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, values );
		}
		if ( bump )
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_bump_vs20 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( NUM_LIGHTS, g_pHardwareConfig->SupportsStaticControlFlow() ? 0 : lights.m_nNumLights );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_bump_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_bump_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITEWATERFOGTODESTALPHA, waterAlpha );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( NUM_LIGHTS, lights.m_nNumLights );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( AMBIENT_LIGHT, lights.m_bAmbientLight );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, shadows );
			SET_DYNAMIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_bump_ps20b );
		}
		else
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_vs20 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DYNAMIC_LIGHT, lights.m_bAmbientLight || lights.m_nNumLights > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( STATIC_LIGHT_VERTEX, lights.m_bStaticLightVertex );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( NUM_LIGHTS, g_pHardwareConfig->SupportsStaticControlFlow() ? 0 : lights.m_nNumLights );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_vertexlit_and_unlit_generic_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITEWATERFOGTODESTALPHA, waterAlpha );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, shadows );
			SET_DYNAMIC_PIXEL_SHADER( l4d2_vertexlit_and_unlit_generic_ps20b );
		}
	}
	pShader->Draw();
}
