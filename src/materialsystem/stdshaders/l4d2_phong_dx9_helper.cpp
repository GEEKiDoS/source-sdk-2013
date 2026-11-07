//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "l4d2_phong_dx9_helper.h"
#include "cpp_shader_constant_register_map.h"
#include "renderparm.h"
#include "commandbuilder.h"
#include "l4d2_phong_vs20.inc"
#include "l4d2_phong_ps20b.inc"
#include "tier0/memdbgon.h"

void InitParamsL4D2Phong_DX9( CBaseVSShader *pShader, IMaterialVar **params, const char *pMaterialName, L4D2VertexLitGeneric_DX9_Vars_t &info )
{
	params[info.m_nFlashlightTexture]->SetStringValue( "effects/flashlight001" );
	if ( info.m_nAlbedo != -1 && g_pConfig->UseBumpmapping() &&
		params[info.m_nBumpmap]->IsDefined() && params[info.m_nAlbedo]->IsDefined() && params[info.m_nBaseTexture]->IsDefined() )
		params[info.m_nBaseTexture]->SetStringValue( params[info.m_nAlbedo]->GetStringValue() );
	SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
	SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
	SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
	if ( !params[info.m_nBaseTexture]->IsDefined() )
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	if ( IS_FLAG_SET( MATERIAL_VAR_DECAL ) )
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
	InitIntParam( info.m_nSelfIllumFresnel, params, 0 );
	if ( !params[info.m_nSelfIllumFresnelMinMaxExp]->IsDefined() )
		params[info.m_nSelfIllumFresnelMinMaxExp]->SetVecValue( 0, 1, 1 );
	InitIntParam( info.m_nBaseMapAlphaPhongMask, params, 0 );
	InitIntParam( info.m_nBaseMapLuminancePhongMask, params, 0 );
	InitIntParam( info.m_nShinyBlood, params, 0 );
	InitIntParam( info.m_nAllowDiffuseModulation, params, 1 );
	InitFloatParam( info.m_nEnvmapFresnel, params, 0 );
	InitFloatParam( info.m_nAmbientOcclusion, params, 0 );
	InitFloatParam( info.m_nDiffuseExponent, params, 1 );
	// The native initializer uses13, despite the declaration's default17.
	InitFloatParam( info.m_nShinyBloodExponent, params, 13 );
}

void InitL4D2Phong_DX9( CBaseVSShader *pShader, IMaterialVar **params, L4D2VertexLitGeneric_DX9_Vars_t &info )
{
	pShader->LoadTexture( info.m_nFlashlightTexture, TEXTUREFLAGS_SRGB );
	bool bBaseAlpha = false;
	if ( params[info.m_nBaseTexture]->IsDefined() )
	{
		pShader->LoadTexture( info.m_nBaseTexture, TEXTUREFLAGS_SRGB );
		bBaseAlpha = params[info.m_nBaseTexture]->GetTextureValue()->IsTranslucent();
		if ( params[info.m_nWrinkle]->IsDefined() )
			pShader->LoadTexture( info.m_nWrinkle, TEXTUREFLAGS_SRGB );
	}
	bool bSelfMask = IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) && params[info.m_nSelfIllumMask]->IsDefined();
	if ( !bBaseAlpha )
	{
		if ( !bSelfMask && !GetIntParam( info.m_nSelfIllumFresnel, params, 0 ) )
			CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	}
	if ( (IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) && !bSelfMask) ||
		IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
		CLEAR_FLAGS( MATERIAL_VAR_ALPHATEST );
	if ( params[info.m_nPhongExponentTexture]->IsDefined() )
		pShader->LoadTexture( info.m_nPhongExponentTexture );
	if ( params[info.m_nDiffuseWarpTexture]->IsDefined() )
		pShader->LoadTexture( info.m_nDiffuseWarpTexture );
	if ( params[info.m_nDetail]->IsDefined() )
	{
		int nMode = GetIntParam( info.m_nDetailTextureCombineMode, params, 0 );
		pShader->LoadTexture( info.m_nDetail, ( nMode == 2 || nMode == 3 || nMode == 4 ) ? TEXTUREFLAGS_SRGB : 0 );
	}
	if ( g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() )
	{
		pShader->LoadBumpMap( info.m_nBumpmap );
		SET_FLAGS2( MATERIAL_VAR2_DIFFUSE_BUMPMAPPED_MODEL );
	}
	if ( params[info.m_nEnvmap]->IsDefined() )
		pShader->LoadCubeMap( info.m_nEnvmap, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0 );
	if ( bSelfMask )
		pShader->LoadTexture( info.m_nSelfIllumMask );
}

class CL4D2PhongContext : public CBasePerMaterialContextData
{
public:
	CCommandBufferBuilder< CFixedCommandStorageBuffer< 800 > > m_Commands;
	float m_Controls[4];
};

static void BuildPhongCommands( CBaseVSShader *pShader, IMaterialVar **params, L4D2VertexLitGeneric_DX9_Vars_t &info,
	CL4D2PhongContext *pContext, bool bSelfIllum, bool bSelfFresnel, bool bWrinkle, bool bDetail )
{
	pContext->m_Commands.Reset();
	if ( params[info.m_nBaseTexture]->IsTexture() )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER0, info.m_nBaseTexture, info.m_nBaseTextureFrame );
	else
		pContext->m_Commands.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
	if ( bWrinkle )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER9, info.m_nWrinkle, info.m_nBaseTextureFrame );
	if ( params[info.m_nPhongExponentTexture]->IsTexture() )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER7, info.m_nPhongExponentTexture, -1 );
	else
		pContext->m_Commands.BindStandardTexture( SHADER_SAMPLER7, TEXTURE_WHITE );
	if ( bDetail )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER13, info.m_nDetail, info.m_nDetailFrame );
	bool bSelfMask = bSelfIllum && params[info.m_nSelfIllumMask]->IsTexture();
	if ( bSelfMask )
		pContext->m_Commands.BindTexture( pShader, SHADER_SAMPLER14, info.m_nSelfIllumMask, -1 );
	else if ( bSelfIllum )
		pContext->m_Commands.BindStandardTexture( SHADER_SAMPLER14, TEXTURE_BLACK );
	pContext->m_Commands.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, info.m_nBaseTextureTransform );
	if ( bDetail )
		pContext->m_Commands.SetVertexShaderTextureScaledTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4,
			IS_PARAM_DEFINED( info.m_nDetailTextureTransform ) ? info.m_nDetailTextureTransform : info.m_nBaseTextureTransform, info.m_nDetailScale );
	float selfTint[4];
	params[info.m_nSelfIllumTint]->GetVecValue( selfTint, 3 );
	selfTint[3] = GetFloatParam( info.m_nDetailTextureBlendFactor, params, 1 );
	pContext->m_Commands.SetPixelShaderConstant( PSREG_SELFILLUMTINT, selfTint );
	if ( bSelfFresnel )
	{
		const float *pRanges = params[info.m_nSelfIllumFresnelMinMaxExp]->GetVecValue();
		float flBias = pRanges[1] != 0 ? pRanges[0] / pRanges[1] : 0;
		pContext->m_Commands.SetPixelShaderConstant4( PSREG_SELFILLUM_SCALE_BIAS_EXP,
			1 - flBias, flBias, pRanges[2], pRanges[1] );
	}
	float controls[4] = { GetFloatParam( info.m_nEnvmapFresnel, params, 0 ),
		(float)GetIntParam( info.m_nBaseMapLuminancePhongMask, params, 0 ),
		GetFloatParam( info.m_nPhongExponent, params, 0 ), bSelfMask ? 1.0f : 0.0f };
	pContext->m_Commands.SetPixelShaderConstant( PSREG_ENVMAP_FRESNEL__SELFILLUMMASK, controls );
	float fresnel[4] = { 0, .5f, 1, params[info.m_nPhongBoost]->IsDefined() ? params[info.m_nPhongBoost]->GetFloatValue() : 1 };
	if ( params[info.m_nPhongFresnelRanges]->IsDefined() )
		params[info.m_nPhongFresnelRanges]->GetVecValue( fresnel, 3 );
	pContext->m_Commands.SetPixelShaderConstant( PSREG_FRESNEL_SPEC_PARAMS, fresnel );
	float tint[4] = { 1, 1, 1, 4 };
	if ( params[info.m_nPhongTint]->IsDefined() )
		params[info.m_nPhongTint]->GetVecValue( tint, 3 );
	if ( tint[0] == 0 && tint[1] == 0 && tint[2] == 0 )
	{
		if ( params[info.m_nPhongExponentTexture]->IsTexture() && GetIntParam( info.m_nPhongAlbedoTint, params, 0 ) )
			tint[0] = -1;
		else
			tint[0] = tint[1] = tint[2] = 1;
	}
	pContext->m_Commands.SetPixelShaderConstant( PSREG_SPEC_RIM_PARAMS, tint );
	pContext->m_Controls[0] = GetIntParam( info.m_nShinyBlood, params, 0 ) ? GetFloatParam( info.m_nShinyBloodExponent, params, 13 ) :
		GetIntParam( info.m_nBaseMapAlphaPhongMask, params, 0 ) ? 1.0f : 0.0f;
	pContext->m_Controls[1] = 0;
	pContext->m_Controls[2] = GetFloatParam( info.m_nDiffuseExponent, params, 1 );
	pContext->m_Controls[3] = GetIntParam( info.m_nInvertPhongMask, params, 0 ) ? 1.0f : 0.0f;
	pContext->m_Commands.SetPixelShaderFogParams( PSREG_FOG_PARAMS );
	pContext->m_Commands.End();
	pContext->m_bMaterialVarsChanged = false;
}

void DrawL4D2Phong_DX9( CBaseVSShader *pShader, IMaterialVar **params, IShaderDynamicAPI *pShaderAPI,
	IShaderShadow *pShaderShadow, L4D2VertexLitGeneric_DX9_Vars_t &info, VertexCompressionType_t vertexCompression,
	CBasePerMaterialContextData **pContextDataPtr )
{
	bool bFlashlight = pShader->UsingFlashlight( params );
	bool bSelfIllum = !bFlashlight && IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
	bool bSelfFresnel = bSelfIllum && GetIntParam( info.m_nSelfIllumFresnel, params, 0 );
	bool bWrinkle = params[info.m_nBaseTexture]->IsTexture() && params[info.m_nWrinkle]->IsTexture();
	bool bDetail = params[info.m_nDetail]->IsTexture();
	bool bEnvmap = !bFlashlight && params[info.m_nEnvmap]->IsTexture();
	bool bWarp = !bFlashlight && params[info.m_nDiffuseWarpTexture]->IsTexture();
	bool bBlendTint = GetIntParam( info.m_nBlendTintByBaseAlpha, params, 0 ) && !IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
	int nDetailMode = bDetail ? GetIntParam( info.m_nDetailTextureCombineMode, params, 0 ) : 0;
	bool bAlphaTest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST );
	BlendType_t blend = pShader->EvaluateBlendRequirements( bBlendTint ? -1 : info.m_nBaseTexture, true );
	bool bOpaque = blend != BT_BLENDADD && blend != BT_BLEND && !bAlphaTest && !bFlashlight;
	// Target DirectX level cap: HardwareConfig012 never reaches native DX100.
	// Therefore the native HasFastVertexTextures branch selects VS20/PS20b,
	// even when the target SDK's differently-defined HasFastVertexTextures is true.
	if ( pShaderShadow )
	{
		pShaderShadow->EnableAlphaTest( bAlphaTest );
		float flAlphaReference = GetFloatParam( info.m_nAlphaTestReference, params, 0 );
		if ( flAlphaReference > 0 )
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, flAlphaReference );
		if ( bFlashlight )
		{
			pShader->SetAdditiveBlendingShadowState( info.m_nBaseTexture, true );
			if ( bAlphaTest )
			{
				pShaderShadow->EnableAlphaTest( false );
				pShaderShadow->DepthFunc( SHADER_DEPTHFUNC_EQUAL );
			}
			pShaderShadow->EnableBlending( true );
			pShaderShadow->EnableDepthWrites( false );
		}
		else
			pShader->SetDefaultBlendingShadowState( info.m_nBaseTexture, true );
		pShaderShadow->EnableAlphaWrites( bOpaque );
		pShaderShadow->EnableSRGBWrite( true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
		if ( bWrinkle )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER9, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER9, true );
		}
		if ( bWarp )
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
		if ( bDetail )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER13, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER13, nDetailMode == 2 || nDetailMode == 3 || nDetailMode == 4 );
		}
		if ( bEnvmap )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
		}
		if ( bSelfIllum && !bFlashlight )
			pShaderShadow->EnableTexture( SHADER_SAMPLER14, true );
		if ( bFlashlight )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
			pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER4 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER6, true );
		}
		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
		if ( IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) || IS_FLAG_SET( MATERIAL_VAR_VERTEXALPHA ) )
			flags |= VERTEX_COLOR;
		int texcoordSize = 2;
		pShaderShadow->VertexShaderVertexFormat( flags, 1, &texcoordSize, 4 );
		DECLARE_STATIC_VERTEX_SHADER( l4d2_phong_vs20 );
		SET_STATIC_VERTEX_SHADER_COMBO( USE_STATIC_CONTROL_FLOW, g_pHardwareConfig->SupportsStaticControlFlow() );
		SET_STATIC_VERTEX_SHADER( l4d2_phong_vs20 );
		DECLARE_STATIC_PIXEL_SHADER( l4d2_phong_ps20b );
		SET_STATIC_PIXEL_SHADER_COMBO( HALFLAMBERT, IS_FLAG_SET( MATERIAL_VAR_HALFLAMBERT ) );
		SET_STATIC_PIXEL_SHADER_COMBO( CUBEMAP, bEnvmap );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM, bSelfIllum && !bFlashlight );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUMFRESNEL, bSelfFresnel && !bFlashlight );
		SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHT, bFlashlight );
		SET_STATIC_PIXEL_SHADER_COMBO( LIGHTWARPTEXTURE, bWarp );
		SET_STATIC_PIXEL_SHADER_COMBO( PHONGWARPTEXTURE, 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( WRINKLEMAP, bWrinkle );
		SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, nDetailMode );
		SET_STATIC_PIXEL_SHADER_COMBO( DETAILTEXTURE, bDetail );
		SET_STATIC_PIXEL_SHADER_COMBO( WOUNDED, 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( RIMLIGHT, 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, bFlashlight ? g_pHardwareConfig->GetShadowFilterMode() : 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( SHADER_SRGB_READ, 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( BLENDTINTBYBASEALPHA, bBlendTint );
		SET_STATIC_PIXEL_SHADER_COMBO( SHINYBLOOD, GetIntParam( info.m_nShinyBlood, params, 0 ) );
		SET_STATIC_PIXEL_SHADER_COMBO( WORLD_NORMAL, 0 );
		SET_STATIC_PIXEL_SHADER( l4d2_phong_ps20b );
		if ( bFlashlight )
			pShader->FogToBlack();
		else
			pShader->DefaultFog();
	}
	else
	{
		CL4D2PhongContext *pContext = static_cast<CL4D2PhongContext *>( *pContextDataPtr );
		if ( !pContext )
		{
			pContext = new CL4D2PhongContext;
			*pContextDataPtr = pContext;
		}
		if ( pContext->m_bMaterialVarsChanged )
			BuildPhongCommands( pShader, params, info, pContext, bSelfIllum, bSelfFresnel, bWrinkle, bDetail );
		pShaderAPI->ExecuteCommandBuffer( pContext->m_Commands.Base() );
		if ( !g_pConfig->m_bFastNoBump && params[info.m_nBumpmap]->IsTexture() )
			pShader->BindTexture( SHADER_SAMPLER3, info.m_nBumpmap, info.m_nBumpFrame );
		else
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_NORMALMAP_FLAT );
		if ( bWarp )
		{
			static ConVarRef r_lightwarpidentity( "r_lightwarpidentity" );
			if ( r_lightwarpidentity.GetBool() )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_IDENTITY_LIGHTWARP );
			else
				pShader->BindTexture( SHADER_SAMPLER2, info.m_nDiffuseWarpTexture );
		}
		if ( bEnvmap )
			pShader->BindTexture( SHADER_SAMPLER8, info.m_nEnvmap, info.m_nEnvmapFrame );
		LightState_t lights = { 0, false, false };
		bool bShadows = false;
		if ( bFlashlight )
		{
			VMatrix worldToTexture;
			ITexture *pDepthTexture;
			const FlashlightState_t &state = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pDepthTexture );
			bShadows = state.m_bEnableShadows && pDepthTexture && g_pConfig->ShadowDepthTexture();
			if ( bShadows )
			{
				pShader->BindTexture( SHADER_SAMPLER4, pDepthTexture, 0 );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_SHADOW_NOISE_2D );
			}
			SetFlashLightColorFromState( state, pShaderAPI, PSREG_FLASHLIGHT_COLOR );
			pShader->BindTexture( SHADER_SAMPLER6, state.m_pSpotlightTexture, state.m_nSpotlightTextureFrame );
			float atten[4] = { state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, state.m_FarZ };
			float origin[4] = { state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z, 0 };
			pShaderAPI->SetPixelShaderConstant( PSREG_FLASHLIGHT_ATTENUATION, atten );
			pShaderAPI->SetPixelShaderConstant( PSREG_FLASHLIGHT_POSITION_RIM_BOOST, origin );
			pShaderAPI->SetPixelShaderConstant( PSREG_FLASHLIGHT_TO_WORLD_TEXTURE, worldToTexture.Base(), 4 );
			float tweaks[4] = { ShadowFilterFromState( state ), ShadowAttenFromState( state ), 0, 0 };
			pShader->HashShadow2DJitter( state.m_flShadowJitterSeed, &tweaks[2], &tweaks[3] );
			pShaderAPI->SetPixelShaderConstant( PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tweaks );
			int width, height;
			pShaderAPI->GetBackBufferDimensions( width, height );
			float screen[4] = { width / 32.0f, height / 32.0f, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( PSREG_FLASHLIGHT_SCREEN_SCALE, screen );
		}
		else
		{
			pShaderAPI->GetDX9LightState( &lights );
			float tint[4] = { 1, 1, 1, IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) ? 1.0f : 0.0f };
			params[info.m_nEnvmapTint]->GetVecValue( tint, 3 );
			pShaderAPI->SetPixelShaderConstant( PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tint );
		}
		static ConVarRef mat_fullbright( "mat_fullbright" );
		bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
		if ( bLightingOnly )
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, bSelfIllum ? TEXTURE_GREY_ALPHA_ZERO : TEXTURE_GREY );
			if ( bDetail )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER13, TEXTURE_GREY );
			float black[4] = { 0, 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( PSREG_SPEC_RIM_PARAMS, black );
			if ( !bFlashlight )
				pShaderAPI->SetPixelShaderConstant( PSREG_ENVMAP_TINT__SHADOW_TWEAKS, black );
		}
		float modulation[4];
		pShader->ComputeModulationColor( modulation );
		if ( GetIntParam( info.m_nAllowDiffuseModulation, params, 1 ) )
		{
			for ( int i = 0; i < 3; ++i )
				modulation[i] = modulation[i] > 1 ? modulation[i] : GammaToLinear( modulation[i] );
		}
		else
			modulation[0] = modulation[1] = modulation[2] = 1;
		pShaderAPI->SetPixelShaderConstant( PSREG_DIFFUSE_MODULATION, modulation );
		float eye[4];
		pShaderAPI->GetWorldSpaceCameraPosition( eye );
		eye[3] = 0; // Native AO is absent in the PS20b program.
		pShaderAPI->SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, eye );
		pShader->SetAmbientCubeDynamicStateVertexShader();
		pShaderAPI->SetPixelShaderStateAmbientLightCube( PSREG_AMBIENT_CUBE, !lights.m_bAmbientLight );
		pShaderAPI->CommitPixelShaderLighting( PSREG_LIGHT_INFO_ARRAY );
		bool bWaterAlpha = bOpaque && pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_L4D2_WRITE_WATER_FOG_TO_DESTALPHA );
		if ( bWaterAlpha )
		{
			Vector range = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_L4D2_WATER_FOG_RANGE );
			pContext->m_Controls[1] = 1.0f / (range.y - range.x);
		}
		pShaderAPI->SetPixelShaderConstant( PSREG_CONSTANT_27, pContext->m_Controls );
		DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_phong_vs20 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( NUM_LIGHTS, g_pHardwareConfig->SupportsStaticControlFlow() ? 0 : lights.m_nNumLights );
		SET_DYNAMIC_VERTEX_SHADER( l4d2_phong_vs20 );
		DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_phong_ps20b );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITEWATERFOGTODESTALPHA, bWaterAlpha );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( NUM_LIGHTS, lights.m_nNumLights );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, bShadows );
		SET_DYNAMIC_PIXEL_SHADER( l4d2_phong_ps20b );
	}
	pShader->Draw();
}
