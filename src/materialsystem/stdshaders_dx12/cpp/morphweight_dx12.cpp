// Native SM5 port of materialsystem/stdshaders/morphweight_dx9.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShaderDX12.h"


#include "morphweight_vs51.inc"
#include "morphweight_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//#define V1 1

DEFINE_FALLBACK_SHADER( MorphWeight, MorphWeight_DX9 )

BEGIN_VS_SHADER_FLAGS( MorphWeight_DX9, "Help for morphweight", SHADER_NOT_EDITABLE )

	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

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
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableCulling( false );
			pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED );

 			DECLARE_STATIC_VERTEX_SHADER( morphweight_vs51 );
 			SET_STATIC_VERTEX_SHADER( morphweight_vs51 );
 
 			DECLARE_STATIC_PIXEL_SHADER( morphweight_ps51 );
 			SET_STATIC_PIXEL_SHADER( morphweight_ps51 );
 
			// Texcoord0 is the texcoord to write the weights into
 			// Texcoord1 contains the morph weights
			int pTexCoord[2] = { 2, 4 };

 			pShaderShadow->VertexShaderVertexFormat( VERTEX_FORMAT_USE_EXACT_FORMAT, 2, pTexCoord, 0 );
		}
		DYNAMIC_STATE
		{
 			DECLARE_DYNAMIC_VERTEX_SHADER( morphweight_vs51 );
 			SET_DYNAMIC_VERTEX_SHADER( morphweight_vs51 );
 
 			DECLARE_DYNAMIC_PIXEL_SHADER( morphweight_ps51 );
 			SET_DYNAMIC_PIXEL_SHADER( morphweight_ps51 );
		}
		Draw();
	}
END_SHADER

