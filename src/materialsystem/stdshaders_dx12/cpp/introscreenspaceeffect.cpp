// Native SM5 port of materialsystem/stdshaders/introscreenspaceeffect.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BaseVSShaderDX12.h"

#include "screenspaceeffect_vs51.inc"
#include "introscreenspaceeffect_ps51.inc"

BEGIN_VS_SHADER_FLAGS( IntroScreenSpaceEffect, "Help for IntroScreenSpaceEffect", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( MODE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( ENABLESRGB, SHADER_PARAM_TYPE_BOOL, "0", "" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_FULL_FRAME_BUFFER_TEXTURE );

		if ( !params[ENABLESRGB]->IsDefined() )
		{
			params[ENABLESRGB]->SetIntValue( 0 );
		}
	}

	SHADER_INIT
	{
	}
	
	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );

			// On OpenGL OSX, we MUST do sRGB reads from the bloom and full framebuffer textures AND sRGB writes on the way out to the framebuffer.
			if ( params[ENABLESRGB]->GetIntValue() || IsOSX() )
			{
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
				pShaderShadow->EnableSRGBWrite( true );
			}

			const bool bNeedsSRGBAdapter = false;

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			
			{
				DECLARE_STATIC_PIXEL_SHADER( introscreenspaceeffect_ps51 );
				SET_STATIC_PIXEL_SHADER_COMBO( LINEAR_TO_SRGB, bNeedsSRGBAdapter );
				SET_STATIC_PIXEL_SHADER( introscreenspaceeffect_ps51 );
			}

			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_1 );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( introscreenspaceeffect_ps51 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( MODE, params[MODE]->GetIntValue() );
				SET_DYNAMIC_PIXEL_SHADER( introscreenspaceeffect_ps51 );
			}

			SetPixelShaderConstant( 0, ALPHA );
		}
		Draw();
	}
END_SHADER
