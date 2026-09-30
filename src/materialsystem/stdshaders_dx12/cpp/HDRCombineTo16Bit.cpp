// Native SM5 port of materialsystem/stdshaders/HDRCombineTo16Bit.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "BaseVSShaderDX12.h"
#include "common_hlsl_cpp_consts.h"
#include "hdrcombineto16bit_vs51.inc"
#include "hdrcombineto16bit_ps51.inc"
#include "convar.h"

BEGIN_VS_SHADER_FLAGS( HDRCombineTo16Bit, "Help for HDRCombineTo16Bit", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SOURCEMRTRENDERTARGET, SHADER_PARAM_TYPE_TEXTURE, "", "" )
	END_SHADER_PARAMS

	SHADER_INIT
	{
		LoadTexture( SOURCEMRTRENDERTARGET );
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
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableDepthTest( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( hdrcombineto16bit_vs51 );
			SET_STATIC_VERTEX_SHADER( hdrcombineto16bit_vs51 );

			{
				DECLARE_STATIC_PIXEL_SHADER( hdrcombineto16bit_ps51 );
				SET_STATIC_PIXEL_SHADER( hdrcombineto16bit_ps51 );
			}
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, SOURCEMRTRENDERTARGET, -1 );
			DECLARE_DYNAMIC_VERTEX_SHADER( hdrcombineto16bit_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( hdrcombineto16bit_vs51 );

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( hdrcombineto16bit_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( hdrcombineto16bit_ps51 );
			}
		}
		Draw();
	}
END_SHADER
