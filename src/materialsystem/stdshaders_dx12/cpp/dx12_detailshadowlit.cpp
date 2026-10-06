//========= Copyright Valve Corporation, All rights reserved. ============//
// Feature-only prelit detail cards. Separate immutable lit/depth materials
// preserve authored cutout state without mutating queued material snapshots.
#include "BaseVSShaderDX12.h"
#include "dx12_detailshadowlit_vs51.inc"
#include "dx12_detailshadowlit_ps51.inc"
#include "dx12_detailshadowlit_depth_ps51.inc"
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( DX12_DetailShadowLit, "Runtime shadow-map detail lighting" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "Authored sprite cutout reference" )
		SHADER_PARAM( SHADOWDEPTH, SHADER_PARAM_TYPE_BOOL, "0", "Shadow-only cutout pass" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_VERTEXCOLOR );
		SET_FLAGS( MATERIAL_VAR_VERTEXALPHA );
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
	}
	SHADER_FALLBACK
	{
		return 0;
	}
	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
	}
	SHADER_DRAW
	{
		const bool depth = params[SHADOWDEPTH]->GetIntValue() != 0;
		SHADOW_STATE
		{
			int texcoordSizes[3] = { 2, 4, 4 };
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_COLOR, 3, texcoordSizes, 0 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			// Legacy detail RGB and texture inputs are gamma encoded. Decode
			// explicitly in the feature PS; retain the existing gamma output path.
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->EnableCulling( false );
			pShaderShadow->EnableAlphaTest( IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) );
			if ( IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) )
				pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, params[ALPHATESTREFERENCE]->GetFloatValue() );
			pShaderShadow->EnableDepthWrites( depth || !IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) );
			pShaderShadow->EnableColorWrites( !depth );
			pShaderShadow->EnableAlphaWrites( !depth );
			if ( !depth && ( IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) || IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) ) )
			{
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA,
					IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) ? SHADER_BLEND_ONE : SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			}
			FogToFogColor();
			DECLARE_STATIC_VERTEX_SHADER( dx12_detailshadowlit_vs51 );
			SET_STATIC_VERTEX_SHADER( dx12_detailshadowlit_vs51 );
			if ( depth )
			{
				DECLARE_STATIC_PIXEL_SHADER( dx12_detailshadowlit_depth_ps51 );
				SET_STATIC_PIXEL_SHADER( dx12_detailshadowlit_depth_ps51 );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( dx12_detailshadowlit_ps51 );
				SET_STATIC_PIXEL_SHADER_COMBO( HDRTYPE, g_pHardwareConfig->GetHDRType() );
				SET_STATIC_PIXEL_SHADER( dx12_detailshadowlit_ps51 );
			}
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			SetPixelShaderConstant( 0, COLOR, ALPHA );
			DECLARE_DYNAMIC_VERTEX_SHADER( dx12_detailshadowlit_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, !depth && pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			SET_DYNAMIC_VERTEX_SHADER( dx12_detailshadowlit_vs51 );
			if ( depth )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( dx12_detailshadowlit_depth_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( dx12_detailshadowlit_depth_ps51 );
			}
			else
			{
				pShaderAPI->SetPixelShaderFogParams( 21 );
				DECLARE_DYNAMIC_PIXEL_SHADER( dx12_detailshadowlit_ps51 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
				SET_DYNAMIC_PIXEL_SHADER( dx12_detailshadowlit_ps51 );
			}
			g_DX12Constants.shadowmapPassAdmitted = true;
		}
		Draw();
	}
END_SHADER
