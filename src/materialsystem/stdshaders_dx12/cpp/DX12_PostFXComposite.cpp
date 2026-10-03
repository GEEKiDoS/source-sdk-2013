//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 post-processing composite (Froyok's bloom and lens flare mix). Lerps the scene towards bloom plus
//          lens flare and glare by the bloom intensity raised by the lens dirt, tonemaps with AgX and applies up to
//          four colour-correction lookups (Engine_Post semantics). $params0 = scene scale, bloom intensity, flare
//          intensity; $params1.x = dirt intensity, $output.x = HDR output flag.
//
//===========================================================================//
#include "BaseVSShaderDX12.h"
#include "screenspaceeffect_vs51.inc"
#include "postfx_composite_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
const int kMaxColorCorrectionLookups = 4;
const int kFlagDirt = 2;
const int kFlagColorCorrection = 4;
}

BEGIN_VS_SHADER( DX12_PostFXComposite, "DX12 post-processing composite" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SCENE, SHADER_PARAM_TYPE_TEXTURE, "", "Scene" )
		SHADER_PARAM( BLOOM, SHADER_PARAM_TYPE_TEXTURE, "", "Bloom chain (mip 1 is sampled)" )
		SHADER_PARAM( FLARE, SHADER_PARAM_TYPE_TEXTURE, "", "Lens flare ghosts and halo" )
		SHADER_PARAM( GLARE, SHADER_PARAM_TYPE_TEXTURE, "", "Lens flare glare" )
		SHADER_PARAM( DIRT, SHADER_PARAM_TYPE_TEXTURE, "", "Lens dirt" )
		SHADER_PARAM( DIRTENABLED, SHADER_PARAM_TYPE_INTEGER, "0", "Lens dirt enabled" )
		SHADER_PARAM( PARAMS0, SHADER_PARAM_TYPE_VEC4, "[1 0 0 0]", "Scene scale, bloom intensity and flare intensity" )
		SHADER_PARAM( PARAMS1, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Lens dirt intensity" )
		SHADER_PARAM( OUTPUT, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "x: 1 when writing HDR output" )
		SHADER_PARAM( OUTSCALE, SHADER_PARAM_TYPE_FLOAT, "1", "Output scale (world nits / UI nits in HDR)" )
		SHADER_PARAM( PEAK, SHADER_PARAM_TYPE_FLOAT, "1", "AgX peak relative to paper white" )
		SHADER_PARAM( DEBUGVIEW, SHADER_PARAM_TYPE_INTEGER, "0", "0 off, 1 bloom, 2 flare, 3 glare, 4 dirt" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( DIRT );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableSRGBWrite( false );
			for ( int nSampler = SHADER_SAMPLER0; nSampler <= SHADER_SAMPLER4 + kMaxColorCorrectionLookups; ++nSampler )
				pShaderShadow->EnableTexture( static_cast<Sampler_t>( nSampler ), true );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, nullptr, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_STATIC_PIXEL_SHADER( postfx_composite_ps51 );
			SET_STATIC_PIXEL_SHADER( postfx_composite_ps51 );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, SCENE, -1 );
			BindTexture( SHADER_SAMPLER1, BLOOM, -1 );
			BindTexture( SHADER_SAMPLER2, FLARE, -1 );
			BindTexture( SHADER_SAMPLER3, GLARE, -1 );
			BindTexture( SHADER_SAMPLER4, DIRT, -1 );

			ShaderColorCorrectionInfo_t info;
			pShaderAPI->GetCurrentColorCorrection( &info );
			const int nLookups = info.m_bIsEnabled ? MIN( info.m_nLookupCount, kMaxColorCorrectionLookups ) : 0;
			for ( int i = 0; i < nLookups; ++i )
				pShaderAPI->BindStandardTexture( static_cast<Sampler_t>( SHADER_SAMPLER5 + i ), static_cast<StandardTextureId_t>( TEXTURE_COLOR_CORRECTION_VOLUME_0 + i ) );

			int nFlags = static_cast<int>( params[OUTPUT]->GetVecValue()[0] );
			if ( params[DIRTENABLED]->GetIntValue() )
				nFlags |= kFlagDirt;
			if ( nLookups )
				nFlags |= kFlagColorCorrection;
			const float dirtPeak[4] = { params[PARAMS1]->GetVecValue()[0], params[PEAK]->GetFloatValue(), 0.0f, 0.0f };
			const float output[4] = { params[OUTSCALE]->GetFloatValue(), static_cast<float>( nFlags ), static_cast<float>( params[DEBUGVIEW]->GetIntValue() ), static_cast<float>( nLookups ) };
			const float lutWeights[4] = { info.m_flDefaultWeight, info.m_pLookupWeights[0], info.m_pLookupWeights[1], info.m_pLookupWeights[2] };
			const float lutWeight4[4] = { info.m_pLookupWeights[3], 0.0f, 0.0f, 0.0f };
			DX12SetPixelShaderConstant( 0, params[PARAMS0]->GetVecValue() );
			DX12SetPixelShaderConstant( 1, dirtPeak );
			DX12SetPixelShaderConstant( 2, output );
			DX12SetPixelShaderConstant( 3, lutWeights );
			DX12SetPixelShaderConstant( 4, lutWeight4 );

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_DYNAMIC_PIXEL_SHADER( postfx_composite_ps51 );
			SET_DYNAMIC_PIXEL_SHADER( postfx_composite_ps51 );
		}
		Draw();
	}
END_SHADER
