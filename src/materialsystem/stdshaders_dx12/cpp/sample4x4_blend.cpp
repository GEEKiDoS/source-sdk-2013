// Native SM5 port of materialsystem/stdshaders/sample4x4_blend.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BaseVSShaderDX12.h"
#include "common_hlsl_cpp_consts.h"


BEGIN_VS_SHADER( Sample4x4_Blend, "Help for Sample4x4_Blend" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( PIXSHADER, SHADER_PARAM_TYPE_STRING, "sample4x4_ps20", "Name of the pixel shader to use" )
	END_SHADER_PARAMS

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
	}
	
	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		// $pixshader names a shader by its DX9 file name (e.g. "sample4x4_ps20"); the DX12 logical is the same base
		// compiled for SM5.1 ("sample4x4_ps51").
		char szPixelShader[MAX_PATH];
		DX12NativeShaderName( params[PIXSHADER]->GetStringValue(), "ps", szPixelShader, sizeof( szPixelShader ) );

		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );
			
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );
			
			pShaderShadow->SetVertexShader( "downsample_vs51", 0 );
			
			pShaderShadow->SetPixelShader( szPixelShader, 0 );

			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA,
									  SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			ITexture *src_texture=params[BASETEXTURE]->GetTextureValue();

			int width=src_texture->GetActualWidth();
			int height=src_texture->GetActualHeight();

			float v[4];
			float dX = 1.0f / width;
			float dY = 1.0f / height;

			v[0] = -dX;
			v[1] = -dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, v, 1 );
			v[0] = -dX;
			v[1] = dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, v, 1 );
			v[0] = dX;
			v[1] = -dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, v, 1 );
			v[0] = dX;
			v[1] = dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, v, 1 );

			DX12SelectNativeBlockByName( "downsample_vs51", dx12native::kStageVertex );
			pShaderAPI->SetVertexShaderIndex( 0 );
			DX12SelectNativeBlockByName( szPixelShader, dx12native::kStagePixel );
			pShaderAPI->SetPixelShaderIndex( 0 );

			// store the ALPHA material var into c0
			v[0] = ALPHA;
			DX12SetPixelShaderConstant( 0, v, 1 );
			
		}
		Draw();
	}
END_SHADER
