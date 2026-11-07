// Native SM5 port of materialsystem/stdshaders/downsample_nohdr.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//============================================================================//

#include "BaseVSShaderDX12.h"
#include "common_hlsl_cpp_consts.h"
#include "convar.h"
#include "downsample_nohdr_ps51.inc"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static ConVar r_bloomtintr( "r_bloomtintr", "0.3" );
static ConVar r_bloomtintg( "r_bloomtintg", "0.59" );
static ConVar r_bloomtintb( "r_bloomtintb", "0.11" );
static ConVar r_bloomtintexponent( "r_bloomtintexponent", "2.2" );

BEGIN_VS_SHADER_FLAGS( Downsample_nohdr, "Help for Downsample_nohdr", SHADER_NOT_EDITABLE )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BLOOMTINTENABLE, SHADER_PARAM_TYPE_INTEGER, "1", "" )
		SHADER_PARAM( CSTRIKE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[ BLOOMTINTENABLE ]->IsDefined() )
		{
			params[ BLOOMTINTENABLE ]->SetIntValue( 1 );
		}
	}

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

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "downsample_vs51", 0 );
			
			{
				DECLARE_STATIC_PIXEL_SHADER( downsample_nohdr_ps51 );
				SET_STATIC_PIXEL_SHADER_COMBO( CSTRIKE, params[CSTRIKE]->GetIntValue() ? 1 : 0 );
				SET_STATIC_PIXEL_SHADER_COMBO( SRGB_ADAPTER, bForceSRGBReadAndWrite );
				SET_STATIC_PIXEL_SHADER( downsample_nohdr_ps51 );
			}
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );

			int width, height;
			pShaderAPI->GetBackBufferDimensions( width, height );

			float v[4][4];
			float dX = 1.0f/width;
			float dY = 1.0f/height;

			v[0][0] = .5*dX;
			v[0][1] = .5*dY;
			v[1][0] = 2.5*dX;
			v[1][1] = .5*dY;
			v[2][0] = .5*dX;
			v[2][1] = 2.5*dY;
			v[3][0] = 2.5*dX;
			v[3][1] = 2.5*dY;
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, &v[0][0], 4 );

			DX12SelectNativeBlockByName( "downsample_vs51", dx12native::kStageVertex );
			pShaderAPI->SetVertexShaderIndex( 0 );

			float flPixelShaderParams[4] = { r_bloomtintr.GetFloat(),
											 r_bloomtintg.GetFloat(),
											 r_bloomtintb.GetFloat(),
											 r_bloomtintexponent.GetFloat() };
			if ( params[ BLOOMTINTENABLE ]->GetIntValue() == 0 )
			{
				flPixelShaderParams[0] = 0.333f;
				flPixelShaderParams[1] = 0.333f;
				flPixelShaderParams[2] = 0.333f;
				flPixelShaderParams[3] = 1.0f;
			}
			DX12SetPixelShaderConstant( 0, flPixelShaderParams, 1 );
						
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( downsample_nohdr_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( downsample_nohdr_ps51 );
			}
		}
		Draw();
	}
END_SHADER
