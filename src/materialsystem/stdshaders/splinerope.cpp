//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"

#include "tier0/memdbgon.h"

// Game-owned bytecode compiled from the recovered retail equations.
// One VS combo, and PS static 0=lit / 1=shadow; both have dynamic index zero.
static ConVar rope_min_pixel_diameter( "rope_min_pixel_diameter", "2.0", FCVAR_CHEAT );

BEGIN_VS_SHADER( SplineRope, "Help for SplineRope" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SHADERSRGBREAD360, SHADER_PARAM_TYPE_BOOL, "0", "Simulate srgb read in shader code" )
		SHADER_PARAM( SHADOWDEPTH, SHADER_PARAM_TYPE_INTEGER, "0", "writing to a shadow depth buffer" )
		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "cable/cablenormalmap", "normal map" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[SHADERSRGBREAD360]->IsDefined() )
			params[SHADERSRGBREAD360]->SetIntValue( 0 );
		if ( !params[SHADOWDEPTH]->IsDefined() )
			params[SHADOWDEPTH]->SetIntValue( 0 );
		if ( !params[BUMPMAP]->IsDefined() )
			params[BUMPMAP]->SetStringValue( "cable/cablenormalmap" );
		SET_FLAGS2( MATERIAL_VAR2_IS_SPRITECARD );
	}

	SHADER_FALLBACK
	{
		return NULL;
	}

	SHADER_INIT
	{
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
		LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
		LoadBumpMap( BUMPMAP );
	}

	SHADER_DRAW
	{
		const bool bShadowDepth = params[SHADOWDEPTH]->GetIntValue() != 0;
		const bool bPS20b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		SHADOW_STATE
		{
			pShaderShadow->EnableCulling( false );
			if ( bShadowDepth )
			{
				pShaderShadow->EnableColorWrites( false );
				pShaderShadow->EnableAlphaWrites( false );
				pShaderShadow->EnablePolyOffset( SHADER_POLYOFFSET_SHADOW_BIAS );
			}
			else
			{
				pShaderShadow->EnableAlphaWrites( true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->EnableSRGBWrite( true );
				FogToFogColor();
			}
			int nTexCoordDimensions[] = { 4, 4, 4, 4 };
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_COLOR,
				4, nTexCoordDimensions, 0 );
			pShaderShadow->SetVertexShader( "l4d2_splinerope_vs20", 0 );
			pShaderShadow->SetPixelShader( bPS20b ? "l4d2_splinerope_ps20b" : "l4d2_splinerope_ps20", bShadowDepth ? 1 : 0 );
		}
		DYNAMIC_STATE
		{
			LoadModelViewMatrixIntoVertexShaderConstant( 48 );
			LoadProjectionMatrixIntoVertexShaderConstant( 51 );
			ShaderViewport_t viewport;
			pShaderAPI->GetViewports( &viewport, 1 );
			const float flMinPixelSize = g_pHardwareConfig->IsAAEnabled() ? 0.0f :
				rope_min_pixel_diameter.GetFloat() / (float)viewport.m_nWidth;
			float vMinPixelSize[4] = { flMinPixelSize, flMinPixelSize, flMinPixelSize, flMinPixelSize };
			pShaderAPI->SetVertexShaderConstant( 55, vMinPixelSize );
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			BindTexture( SHADER_SAMPLER1, BUMPMAP );
			if ( !bShadowDepth )
			{
				pShaderAPI->SetPixelShaderFogParams( 0 );
				float vEyePos[4];
				pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
				vEyePos[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 1, vEyePos );
			}
			pShaderAPI->SetVertexShaderIndex( 0 );
			pShaderAPI->SetPixelShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
