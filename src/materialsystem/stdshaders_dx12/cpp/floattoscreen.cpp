// Native SM5 port of materialsystem/stdshaders/floattoscreen.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BaseVSShaderDX12.h"

#include "screenspaceeffect_vs51.inc"
#include "convar.h"

BEGIN_VS_SHADER_FLAGS( floattoscreen, "Help for floattoscreen", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( PIXSHADER, SHADER_PARAM_TYPE_STRING, "floattoscreen_ps20", "Name of the pixel shader to use" )
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
		// $pixshader names a shader by its DX9 file name (e.g. "sample4x4_ps20"); the DX12 logical is the same base
		// compiled for SM5.1 ("sample4x4_ps51").
		char szPixelShader[MAX_PATH];
		DX12NativeShaderName( params[PIXSHADER]->GetStringValue(), "ps", szPixelShader, sizeof( szPixelShader ) );

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

			pShaderShadow->SetPixelShader( szPixelShader, 0 );
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );

			DX12SelectNativeBlockByName( szPixelShader, dx12native::kStagePixel );
			pShaderAPI->SetPixelShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
