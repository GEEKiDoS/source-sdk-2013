// Native SM5 port of materialsystem/stdshaders/worldvertextransition_dx8_helper.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "worldvertextransition_dx8_helper.h"
#include "BaseVSShaderDX12.h"
#include "cpp_shader_constant_register_map.h"
#include "WorldVertexTransition.inc"
#include "lightmappedgeneric_dx9_helper.h"
#include "worldvertextransition_editor_highres_vs51.inc"
#include "worldvertextransition_editor_highres_ps51.inc"



// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


void InitParamsWorldVertexTransitionEditor_DX8( IMaterialVar** params, WorldVertexTransitionEditor_DX8_Vars_t &info )
{
	SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
}

void InitWorldVertexTransitionEditor_DX8( CBaseVSShaderDX12 *pShader, IMaterialVar** params, WorldVertexTransitionEditor_DX8_Vars_t &info )
{
	if ( params[info.m_nBaseTextureVar]->IsDefined() )
	{
		pShader->LoadTexture( info.m_nBaseTextureVar );
	}

	if ( params[info.m_nBaseTexture2Var]->IsDefined() )
	{
		pShader->LoadTexture( info.m_nBaseTexture2Var );
	}
}

void DrawWorldVertexTransitionEditor_DX8( CBaseVSShaderDX12 *pShader, IMaterialVar** params, IShaderDynamicAPI *pShaderAPI, IShaderShadow* pShaderShadow, WorldVertexTransitionEditor_DX8_Vars_t &info )
{
	const bool highres = DX12HighresMap();
	SHADOW_STATE
	{
		// This is the dx8 worldcraft version (non-bumped always.. too bad)
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
		pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );

		int fmt = VERTEX_POSITION | VERTEX_COLOR;
		if ( highres ) fmt |= VERTEX_NORMAL;
		pShaderShadow->VertexShaderVertexFormat( fmt, 2, 0, 0 );

		if ( highres )
		{
			DECLARE_STATIC_VERTEX_SHADER( worldvertextransition_editor_highres_vs51 );
			SET_STATIC_VERTEX_SHADER( worldvertextransition_editor_highres_vs51 );
			DECLARE_STATIC_PIXEL_SHADER( worldvertextransition_editor_highres_ps51 );
			SET_STATIC_PIXEL_SHADER( worldvertextransition_editor_highres_ps51 );
		}
		else
		{
			worldvertextransition_Static_Index vshIndex;
			pShaderShadow->SetVertexShader( "WorldVertexTransition", vshIndex.GetIndex() );
			pShaderShadow->SetPixelShader( "WorldVertexTransition_Editor" );
		}
	
		pShader->FogToFogColor();
	}
	DYNAMIC_STATE
	{
		DX12SelectNativeBlockByName( highres ? "worldvertextransition_editor_highres_ps51" : "WorldVertexTransition_Editor", dx12native::kStagePixel );
		pShader->BindTexture( SHADER_SAMPLER0, info.m_nBaseTextureVar, info.m_nBaseTextureFrameVar );
		pShader->BindTexture( SHADER_SAMPLER1, info.m_nBaseTexture2Var, info.m_nBaseTexture2FrameVar );

		// Texture 3 = lightmap
		pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_LIGHTMAP );
		
		pShader->EnablePixelShaderOverbright( 0, true, true );
		
		// JasonM - Gnarly hack since we're calling this legacy shader from DX9
		int nTextureTransformConst  = VERTEX_SHADER_SHADER_SPECIFIC_CONST_0;
		int nTextureTransformConst2 = VERTEX_SHADER_SHADER_SPECIFIC_CONST_2;
		{
			nTextureTransformConst  -= 10;
			nTextureTransformConst2 -= 10;
		}

		pShader->SetVertexShaderTextureTransform( nTextureTransformConst,  info.m_nBaseTextureTransformVar  );
		pShader->SetVertexShaderTextureTransform( nTextureTransformConst2, info.m_nBaseTexture2TransformVar );

		if ( highres )
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( worldvertextransition_editor_highres_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			SET_DYNAMIC_VERTEX_SHADER( worldvertextransition_editor_highres_vs51 );
			DECLARE_DYNAMIC_PIXEL_SHADER( worldvertextransition_editor_highres_ps51 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER( worldvertextransition_editor_highres_ps51 );
			pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );
		}
		else
		{
			worldvertextransition_Dynamic_Index vshIndex;
			vshIndex.SetDOWATERFOG( pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			pShaderAPI->SetVertexShaderIndex( vshIndex.GetIndex() );
		}
	}
	pShader->Draw();
}
