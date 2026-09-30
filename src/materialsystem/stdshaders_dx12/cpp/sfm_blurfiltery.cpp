// Native SM5 port of materialsystem/stdshaders/sfm_blurfiltery.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShaderDX12.h"
#include "blurfilter_vs51.inc"
#include "blurfilter_ps51.inc"


BEGIN_VS_SHADER_FLAGS( sfm_blurfiltery_shader, "Help for BlurFilterY", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS

	SHADER_INIT
	{
		if( params[BASETEXTURE]->IsDefined() )
		{
			LoadTexture( BASETEXTURE );
		}
	}
	
	SHADER_FALLBACK
	{
		// Requires DX9 + above
		
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			// Pre-cache shaders
			blurfilter_vs51_Static_Index vshIndex;
			pShaderShadow->SetVertexShader( "blurfilter_vs51", vshIndex.GetIndex() );

			{
				DECLARE_STATIC_PIXEL_SHADER( blurfilter_ps51 );
				SET_STATIC_PIXEL_SHADER_COMBO( APPROX_SRGB_ADAPTER, 0 );
				SET_STATIC_PIXEL_SHADER( blurfilter_ps51 );
			}

			if ( IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) )
				EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );

			ITexture *src_texture=params[BASETEXTURE]->GetTextureValue();
			int height  = src_texture->GetActualHeight();

			float v[4];

			// The temp buffer is 1/4 back buffer size
			float dY = 1.0f / height;

			// Tap offsets
			v[0] = 0.0f;
			v[1] = 1.3366f * dY;
			v[2] = 0;
			v[3] = 0;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, v, 1 );
			v[0] = 0.0f;
			v[1] = 3.4295f * dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, v, 1 );
			v[0] = 0.0f;
			v[1] = 5.4264f * dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, v, 1 );

			v[0] = 0.0f;
			v[1] = 7.4359f * dY;
			DX12SetPixelShaderConstant( 0, v, 1 );
			v[0] = 0.0f;
			v[1] = 9.4436f * dY;
			DX12SetPixelShaderConstant( 1, v, 1 );
			v[0] = 0.0f;
			v[1] = 11.4401f * dY;
			DX12SetPixelShaderConstant( 2, v, 1 );

			v[0] = v[1] = v[2] = v [3]=1.0;
			DX12SetPixelShaderConstant( 3, v, 1 );

			DX12SelectNativeBlockByName( "blurfilter_vs51", dx12native::kStageVertex );
			pShaderAPI->SetVertexShaderIndex( 0 );
			
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( blurfilter_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( blurfilter_ps51 );
			}
		}
		Draw();
	}
END_SHADER
