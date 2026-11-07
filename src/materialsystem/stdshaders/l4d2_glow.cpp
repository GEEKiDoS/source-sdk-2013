//========= Copyright Valve Corporation, All rights reserved. ============//
// Game-owned names only. Native stdshader_dx9 wrappers and constants are
// retained in evidence/native_outline_manifest.json and wrapper_*.txt.
#include "BaseVSShader.h"
#include "convar.h"
#include "l4d2_glow_blurfilter_vs20.inc"
#include "l4d2_glow_blurfilter_ps20.inc"
#include "l4d2_glow_blurfilter_ps20b.inc"
#include "l4d2_glow_downsample_vs20.inc"
#include "l4d2_glow_downsample_nohdr_ps20.inc"
#include "l4d2_glow_downsample_nohdr_ps20b.inc"
#include "l4d2_glow_haloadd_vs20.inc"
#include "l4d2_glow_haloadd_ps20.inc"
#include "l4d2_glow_haloadd_ps20b.inc"
#include "l4d2_black_vs20.inc"
#include "l4d2_black_ps20.inc"
#include "l4d2_black_ps20b.inc"

#include "tier0/memdbgon.h"

// Native X/Y share every state and tap except axis, RGB scale and clear color.
static void DrawNativeL4D2Blur( CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow *pShaderShadow, IShaderDynamicAPI *pShaderAPI,
	int nKernel, bool bVertical, bool bAdditive, float flScale,
	bool bClearColor, const float *pClearColor )
{
	const bool bPS20b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
	if ( pShaderShadow )
	{
		pShaderShadow->EnableDepthWrites( false );
		pShaderShadow->EnableAlphaWrites( true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
		pShaderShadow->EnableSRGBWrite( false );
		DECLARE_STATIC_VERTEX_SHADER( l4d2_glow_blurfilter_vs20 );
		SET_STATIC_VERTEX_SHADER_COMBO( KERNEL, nKernel ? 1 : 0 );
		SET_STATIC_VERTEX_SHADER( l4d2_glow_blurfilter_vs20 );
		if ( bPS20b )
		{
			DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( KERNEL, nKernel );
			SET_STATIC_PIXEL_SHADER_COMBO( CLEAR_COLOR, bClearColor );
			SET_STATIC_PIXEL_SHADER_COMBO( APPROX_SRGB_ADAPTER, 0 );
			SET_STATIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20b );
		}
		else
		{
			DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20 );
			SET_STATIC_PIXEL_SHADER_COMBO( KERNEL, nKernel );
			SET_STATIC_PIXEL_SHADER_COMBO( CLEAR_COLOR, bClearColor );
			SET_STATIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20 );
		}
		if ( bAdditive )
			pShader->EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
	}
	if ( pShaderAPI )
	{
		pShader->BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
		ITexture *pTexture = params[BASETEXTURE]->GetTextureValue();
		const float flStep = 1.0f / ( bVertical ? pTexture->GetActualHeight() : pTexture->GetActualWidth() );
		static const float flTapOffsets[6] = { 1.3366f, 3.4295f, 5.4264f, 7.4359f, 9.4436f, 11.4401f };
		float v[4] = { 0, 0, 0, 0 };
		for ( int i = 0; i < 6; ++i )
		{
			v[bVertical ? 1 : 0] = flTapOffsets[i] * flStep;
			if ( i < 3 )
				pShaderAPI->SetVertexShaderConstant( 48 + i, v );
			else
				pShaderAPI->SetPixelShaderConstant( i - 3, v );
		}
		v[0] = v[1] = v[2] = flScale;
		v[3] = bVertical ? 0.0f : 1.0f;
		pShaderAPI->SetPixelShaderConstant( 3, v );
		v[0] = bVertical ? 0.0f : flStep;
		v[1] = bVertical ? flStep : 0.0f;
		v[2] = v[3] = 0.0f;
		pShaderAPI->SetPixelShaderConstant( 4, v );
		if ( bVertical )
		{
			VectorCopy( pClearColor, v );
			pShaderAPI->SetPixelShaderConstant( 5, v );
		}
		DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_glow_blurfilter_vs20 );
		SET_DYNAMIC_VERTEX_SHADER( l4d2_glow_blurfilter_vs20 );
		if ( bPS20b )
		{
			DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20b );
			SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20b );
		}
		else
		{
			DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20 );
			SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_blurfilter_ps20 );
		}
	}
	pShader->Draw();
}

BEGIN_VS_SHADER_FLAGS( L4D2_BlurFilterX, "Native L4D2 horizontal outline blur", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( KERNEL, SHADER_PARAM_TYPE_INTEGER, "0", "Kernel type" )
	END_SHADER_PARAMS
	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() ) LoadTexture( BASETEXTURE );
		if ( !params[KERNEL]->IsDefined() ) params[KERNEL]->SetIntValue( 0 );
	}
	SHADER_FALLBACK { return NULL; }
	SHADER_DRAW
	{
		// The original X shader does not declare $bloomamount and always scales by 1.
		DrawNativeL4D2Blur( this, params, pShaderShadow, pShaderAPI,
			params[KERNEL]->GetIntValue(), false, IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ),
			1.0f, false, NULL );
	}
END_SHADER

BEGIN_VS_SHADER_FLAGS( L4D2_BlurFilterY, "Native L4D2 vertical outline blur", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BLOOMAMOUNT, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( FRAMETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_SmallHDR0", "" )
		SHADER_PARAM( KERNEL, SHADER_PARAM_TYPE_INTEGER, "0", "Kernel type" )
		SHADER_PARAM( ENABLECLEARCOLOR, SHADER_PARAM_TYPE_BOOL, "0", "Clear RGB channels to a solid color" )
		SHADER_PARAM( CLEARCOLOR, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "Clear color" )
	END_SHADER_PARAMS
	SHADER_INIT_PARAMS()
	{
		INIT_FLOAT_PARM( BLOOMAMOUNT, 1.0f );
		if ( !params[KERNEL]->IsDefined() ) params[KERNEL]->SetIntValue( 0 );
		if ( !params[ENABLECLEARCOLOR]->IsDefined() ) params[ENABLECLEARCOLOR]->SetIntValue( 0 );
		if ( !params[CLEARCOLOR]->IsDefined() ) params[CLEARCOLOR]->SetVecValue( 0.0f, 0.0f, 0.0f );
	}
	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() ) LoadTexture( BASETEXTURE );
	}
	SHADER_FALLBACK { return NULL; }
	SHADER_DRAW
	{
		float vClearColor[3];
		params[CLEARCOLOR]->GetVecValue( vClearColor, 3 );
		DrawNativeL4D2Blur( this, params, pShaderShadow, pShaderAPI,
			params[KERNEL]->GetIntValue(), true, IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ),
			params[BLOOMAMOUNT]->GetFloatValue(), params[ENABLECLEARCOLOR]->GetIntValue() != 0, vClearColor );
	}
END_SHADER

BEGIN_VS_SHADER_FLAGS( L4D2_Downsample_nohdr, "Native L4D2 outline downsample", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BLOOMTYPE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( BLOOMEXP, SHADER_PARAM_TYPE_FLOAT, "2.5", "" )
		SHADER_PARAM( BLOOMSATURATION, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( BLOOMTINTENABLE, SHADER_PARAM_TYPE_INTEGER, "1", "" )
	END_SHADER_PARAMS
	SHADER_INIT_PARAMS()
	{
		INIT_FLOAT_PARM( BLOOMEXP, 2.5f );
		INIT_FLOAT_PARM( BLOOMSATURATION, 1.0f );
		if ( !params[BLOOMTINTENABLE]->IsDefined() ) params[BLOOMTINTENABLE]->SetIntValue( 1 );
		if ( !params[BLOOMTYPE]->IsDefined() ) params[BLOOMTYPE]->SetIntValue( 0 );
	}
	SHADER_INIT { LoadTexture( BASETEXTURE ); }
	SHADER_FALLBACK { return NULL; }
	SHADER_DRAW
	{
		const bool bPS20b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( l4d2_glow_downsample_vs20 );
			SET_STATIC_VERTEX_SHADER( l4d2_glow_downsample_vs20 );
			if ( bPS20b )
			{
				DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20b );
				SET_STATIC_PIXEL_SHADER_COMBO( BLOOMTYPE, params[BLOOMTYPE]->GetIntValue() );
				SET_STATIC_PIXEL_SHADER_COMBO( SRGB_ADAPTER, 0 );
				SET_STATIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20 );
				SET_STATIC_PIXEL_SHADER_COMBO( BLOOMTYPE, params[BLOOMTYPE]->GetIntValue() );
				SET_STATIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20 );
			}
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			int nWidth, nHeight;
			pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
			const float flDX = 1.0f / nWidth;
			const float flDY = 1.0f / nHeight;
			const float vOffsets[4][4] = {
				{ 0.5f * flDX, 0.5f * flDY, 0, 0 }, { 2.5f * flDX, 0.5f * flDY, 0, 0 },
				{ 0.5f * flDX, 2.5f * flDY, 0, 0 }, { 2.5f * flDX, 2.5f * flDY, 0, 0 } };
			pShaderAPI->SetVertexShaderConstant( 48, &vOffsets[0][0], 4 );
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_glow_downsample_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_glow_downsample_vs20 );
			float vTint[4] = { 1, 1, 1, 1 };
			if ( params[BLOOMTINTENABLE]->GetIntValue() != 0 )
			{
				// Resolve the stock-owned CVars only after shaderlib connects.
				static ConVarRef s_BloomTintR( "r_bloomtintr" );
				static ConVarRef s_BloomTintG( "r_bloomtintg" );
				static ConVarRef s_BloomTintB( "r_bloomtintb" );
				static ConVarRef s_BloomTintExponent( "r_bloomtintexponent" );
				vTint[0] = s_BloomTintR.GetFloat();
				vTint[1] = s_BloomTintG.GetFloat();
				vTint[2] = s_BloomTintB.GetFloat();
				vTint[3] = s_BloomTintExponent.GetFloat();
			}
			pShaderAPI->SetPixelShaderConstant( 0, vTint );
			const float vParams[4] = { params[BLOOMEXP]->GetFloatValue(), params[BLOOMSATURATION]->GetFloatValue(), 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 1, vParams );
			if ( bPS20b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20b );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( FLOAT_BACK_BUFFER, g_pHardwareConfig->GetHDRType() == HDR_TYPE_FLOAT );
				SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20 );
				SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_downsample_nohdr_ps20 );
			}
		}
		Draw();
	}
END_SHADER

BEGIN_VS_SHADER_FLAGS( L4D2_HaloAdd, "Native L4D2 halo overlay", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( C0_X, SHADER_PARAM_TYPE_FLOAT, "0", "Dim value" )
		SHADER_PARAM( C0_Y, SHADER_PARAM_TYPE_FLOAT, "0", "Native unused constant" )
		SHADER_PARAM( C0_Z, SHADER_PARAM_TYPE_FLOAT, "0", "Native unused constant" )
		SHADER_PARAM( C0_W, SHADER_PARAM_TYPE_FLOAT, "0", "Native unused constant" )
		SHADER_PARAM( ALPHA_BLEND_COLOR_OVERLAY, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( LINEARREAD_BASETEXTURE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( LINEARWRITE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
	END_SHADER_PARAMS
	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() ) LoadTexture( BASETEXTURE );
	}
	SHADER_FALLBACK { return NULL; }
	SHADER_DRAW
	{
		const bool bPS20b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		SHADOW_STATE
		{
			// screenspace_general defaults: WRITEDEPTH/WRITEALPHA/DEPTHTEST/CULL = 0.
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableCulling( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, params[LINEARREAD_BASETEXTURE]->GetIntValue() == 0 );
			pShaderShadow->EnableSRGBWrite( params[LINEARWRITE]->GetFloatValue() == 0.0f );
			int nTexCoordSize = 2;
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, &nTexCoordSize, 0 );
			pShaderShadow->EnableBlending( false );
			if ( IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) ) EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			pShaderShadow->EnableAlphaTest( true );
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GREATER, 0.0f );
			if ( params[ALPHA_BLEND_COLOR_OVERLAY]->GetIntValue() )
				EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			DECLARE_STATIC_VERTEX_SHADER( l4d2_glow_haloadd_vs20 );
			SET_STATIC_VERTEX_SHADER( l4d2_glow_haloadd_vs20 );
			if ( bPS20b )
			{
				DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20b );
				SET_STATIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20 );
				SET_STATIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20 );
			}
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			const float vConstants[4] = { params[C0_X]->GetFloatValue(), params[C0_Y]->GetFloatValue(), params[C0_Z]->GetFloatValue(), params[C0_W]->GetFloatValue() };
			pShaderAPI->SetPixelShaderConstant( 0, vConstants );
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_glow_haloadd_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_glow_haloadd_vs20 );
			if ( bPS20b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20b );
				SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20 );
				SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_haloadd_ps20 );
			}
		}
		Draw();
	}
END_SHADER

// Original stdshader_dx9 10004DC0/10004E40. The native dev/black material
// is a skinned black silhouette, with XY range fog blended in linear space.
BEGIN_VS_SHADER( Black, "Native L4D2 black silhouette" )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS
	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
	}
	SHADER_INIT {}
	SHADER_FALLBACK { return NULL; }
	SHADER_DRAW
	{
		const bool bPS20b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		SHADOW_STATE
		{
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_FORMAT_COMPRESSED, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( l4d2_black_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( HARDWAREFOGBLEND, !bPS20b );
			SET_STATIC_VERTEX_SHADER( l4d2_black_vs20 );
			if ( bPS20b )
			{
				DECLARE_STATIC_PIXEL_SHADER( l4d2_black_ps20b );
				SET_STATIC_PIXEL_SHADER( l4d2_black_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( l4d2_black_ps20 );
				SET_STATIC_PIXEL_SHADER( l4d2_black_ps20 );
			}
			pShaderShadow->EnableSRGBWrite( true );
			// Native FogMode carries a separate vertex-fog selector. The SDK
			// has no such flag/API argument: ps20b already blends our vertex
			// fog color, while ps20 needs the fixed-function blend.
			if ( bPS20b )
				DisableFog();
			else
				FogToFogColor();
		}
		DYNAMIC_STATE
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_black_vs20 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SCENEFOG,
				!IS_FLAG_SET( MATERIAL_VAR_NOFOG ) && pShaderAPI->GetSceneFogMode() != MATERIAL_FOG_NONE );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_black_vs20 );
			if ( bPS20b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_black_ps20b );
				SET_DYNAMIC_PIXEL_SHADER( l4d2_black_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_black_ps20 );
				SET_DYNAMIC_PIXEL_SHADER( l4d2_black_ps20 );
			}
			unsigned char color[3];
			pShaderAPI->GetSceneFogColor( color );
			const float scale = pShaderAPI->GetToneMappingScaleLinear().x;
			const float fogColor[4] =
			{
				scale * SrgbGammaToLinear( color[0] * ( 1.0f / 255.0f ) ),
				scale * SrgbGammaToLinear( color[1] * ( 1.0f / 255.0f ) ),
				scale * SrgbGammaToLinear( color[2] * ( 1.0f / 255.0f ) ),
				1.0f
			};
			pShaderAPI->SetVertexShaderConstant( 48, fogColor );
		}
		Draw();
	}
END_SHADER
