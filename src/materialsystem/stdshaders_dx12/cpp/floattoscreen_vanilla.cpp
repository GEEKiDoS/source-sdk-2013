// Native SM5 port of materialsystem/stdshaders/floattoscreen_vanilla.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BaseVSShaderDX12.h"

#include "screenspaceeffect_vs51.inc"
#include "floattoscreen_ps51.inc"

BEGIN_VS_SHADER_FLAGS( floattoscreen_vanilla, "Help for floattoscreen_vanilla", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "" )
	END_SHADER_PARAMS

	SHADER_INIT
	{
		if( params[FBTEXTURE]->IsDefined() )
		{
			LoadTexture( FBTEXTURE );
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

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			// convert from linear to gamma on write.
			pShaderShadow->EnableSRGBWrite( true );

			// Pre-cache shaders
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );

			{
				DECLARE_STATIC_PIXEL_SHADER( floattoscreen_ps51 );
				SET_STATIC_PIXEL_SHADER( floattoscreen_ps51 );
			}
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( floattoscreen_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( floattoscreen_ps51 );
			}
		}
		Draw();
	}
END_SHADER
