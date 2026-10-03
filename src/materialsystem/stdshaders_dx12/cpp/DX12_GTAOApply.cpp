//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 GTAO apply. Multiplies the scene by the AO term ($debug 1 writes the term instead); below
//          full resolution the term is upsampled with a view-depth-aware bilateral filter.
//
//===========================================================================//
#include "BaseVSShaderDX12.h"
#include "screenspaceeffect_vs51.inc"
#include "gtao_apply_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( DX12_GTAOApply, "DX12 GTAO apply" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( AOIN, SHADER_PARAM_TYPE_TEXTURE, "", "AO term" )
		SHADER_PARAM( VIEWDEPTH, SHADER_PARAM_TYPE_TEXTURE, "", "Full-resolution view depth" )
		SHADER_PARAM( DEPTHMIPS, SHADER_PARAM_TYPE_TEXTURE, "", "AO-resolution view depth (mip 0)" )
		SHADER_PARAM( SCENEORIGIN, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Viewport origin" )
		SHADER_PARAM( FULLSIZE, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Viewport size" )
		SHADER_PARAM( AOSIZE, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "AO working size" )
		SHADER_PARAM( DEPTHTOLERANCE, SHADER_PARAM_TYPE_FLOAT, "0.05", "Relative bilateral depth tolerance" )
		SHADER_PARAM( FULLRES, SHADER_PARAM_TYPE_INTEGER, "1", "AO is at full resolution" )
		SHADER_PARAM( RESTORESCALE, SHADER_PARAM_TYPE_INTEGER, "0", "Restore the 1.5 packed AO scale (no final denoise ran)" )
		SHADER_PARAM( DEBUGVIEW, SHADER_PARAM_TYPE_INTEGER, "0", "Write the AO term instead of multiplying" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			// Drawn mid-scene with the scene depth bound: the fullscreen triangle must not be depth-tested.
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			if ( params[DEBUGVIEW]->GetIntValue() == 0 )
			{
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_ZERO, SHADER_BLEND_SRC_COLOR );
			}
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, nullptr, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_STATIC_PIXEL_SHADER( gtao_apply_ps51 );
			SET_STATIC_PIXEL_SHADER( gtao_apply_ps51 );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, AOIN, -1 );
			BindTexture( SHADER_SAMPLER1, VIEWDEPTH, -1 );
			BindTexture( SHADER_SAMPLER2, DEPTHMIPS, -1 );
			const float *pOrigin = params[SCENEORIGIN]->GetVecValue();
			const float *pFull = params[FULLSIZE]->GetVecValue();
			const float *pAo = params[AOSIZE]->GetVecValue();
			const float originFull[4] = { pOrigin[0], pOrigin[1], pFull[0], pFull[1] };
			const float aoTolerance[4] = { pAo[0], pAo[1], params[DEPTHTOLERANCE]->GetFloatValue(), 0.0f };
			const float options[4] = { static_cast<float>( params[FULLRES]->GetIntValue() ), static_cast<float>( params[RESTORESCALE]->GetIntValue() ), 0.0f, 0.0f };
			DX12SetPixelShaderConstant( 0, originFull );
			DX12SetPixelShaderConstant( 1, aoTolerance );
			DX12SetPixelShaderConstant( 2, options );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_DYNAMIC_PIXEL_SHADER( gtao_apply_ps51 );
			SET_DYNAMIC_PIXEL_SHADER( gtao_apply_ps51 );
		}
		Draw();
	}
END_SHADER
