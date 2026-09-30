// Native SM5 port of materialsystem/stdshaders/Downsample.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShaderDX12.h"
#include "common_hlsl_cpp_consts.h"
#include "downsample_ps51.inc"



BEGIN_VS_SHADER_FLAGS( Downsample, "Help for Downsample", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
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
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );

			const bool bForceSRGBReadAndWrite = false;
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, bForceSRGBReadAndWrite );
			pShaderShadow->EnableSRGBWrite( bForceSRGBReadAndWrite );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "downsample_vs51", 0 );
			
			{
				DECLARE_STATIC_PIXEL_SHADER( downsample_ps51 );
				SET_STATIC_PIXEL_SHADER( downsample_ps51 );
			}
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );

			int width, height;
			pShaderAPI->GetBackBufferDimensions( width, height );

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

			// Setup luminance threshold (all values are scaled down by max luminance)
//			v[0] = 1.0f / MAX_HDR_OVERBRIGHT;
			v[0] = 0.0f;
			DX12SetPixelShaderConstant( 0, v, 1 );

			DX12SelectNativeBlockByName( "downsample_vs51", dx12native::kStageVertex );
			pShaderAPI->SetVertexShaderIndex( 0 );

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( downsample_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( downsample_ps51 );
			}
		}
		Draw();
	}
END_SHADER
