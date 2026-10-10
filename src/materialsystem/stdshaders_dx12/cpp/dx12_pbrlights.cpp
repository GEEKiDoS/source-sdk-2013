//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12_PBRLights hands the cookie and shadow-depth textures of the view's projected lights (flashlight,
//          env_projectedtexture) to the backend; the packet carrying their matrices follows through
//          IShaderAPIDX12Lighting::BeginProjectedLights. DX12_PBRGBufferDebug is the compute pass that renders the PBR
//          G-buffer (normals / F0 / roughness) into $output.
//
//===========================================================================//
#include "BaseVSShaderDX12.h"
#include "dx12_compute_material_common.h"
#include "lightmappedgeneric_dx9_helper.h"
#include "pbr_gbuffer_debug_cs51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// DX12_PBRLights: $cookie0..7 / $cookieframe0..7 / $depth0..7 are declared consecutively so slot i is name0 + i.
//-----------------------------------------------------------------------------
BEGIN_VS_SHADER( DX12_PBRLights, "Projected-light cookie and shadow textures for the PBR shaders" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( COOKIE0, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 0" )
		SHADER_PARAM( COOKIE1, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 1" )
		SHADER_PARAM( COOKIE2, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 2" )
		SHADER_PARAM( COOKIE3, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 3" )
		SHADER_PARAM( COOKIE4, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 4" )
		SHADER_PARAM( COOKIE5, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 5" )
		SHADER_PARAM( COOKIE6, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 6" )
		SHADER_PARAM( COOKIE7, SHADER_PARAM_TYPE_TEXTURE, "", "Cookie of projected light 7" )
		SHADER_PARAM( COOKIEFRAME0, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 0" )
		SHADER_PARAM( COOKIEFRAME1, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 1" )
		SHADER_PARAM( COOKIEFRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 2" )
		SHADER_PARAM( COOKIEFRAME3, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 3" )
		SHADER_PARAM( COOKIEFRAME4, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 4" )
		SHADER_PARAM( COOKIEFRAME5, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 5" )
		SHADER_PARAM( COOKIEFRAME6, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 6" )
		SHADER_PARAM( COOKIEFRAME7, SHADER_PARAM_TYPE_INTEGER, "0", "Cookie frame of projected light 7" )
		SHADER_PARAM( DEPTH0, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 0 (shadowed lights only)" )
		SHADER_PARAM( DEPTH1, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 1 (shadowed lights only)" )
		SHADER_PARAM( DEPTH2, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 2 (shadowed lights only)" )
		SHADER_PARAM( DEPTH3, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 3 (shadowed lights only)" )
		SHADER_PARAM( DEPTH4, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 4 (shadowed lights only)" )
		SHADER_PARAM( DEPTH5, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 5 (shadowed lights only)" )
		SHADER_PARAM( DEPTH6, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 6 (shadowed lights only)" )
		SHADER_PARAM( DEPTH7, SHADER_PARAM_TYPE_TEXTURE, "", "Shadow depth of projected light 7 (shadowed lights only)" )
		SHADER_PARAM( COUNT, SHADER_PARAM_TYPE_INTEGER, "0", "Number of projected lights (0..8)" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
	}

	SHADER_DRAW
	{
		DYNAMIC_STATE
		{
			const int nCount = clamp( params[COUNT]->GetIntValue(), 0, DX12_PBR_MAX_PROJECTED_LIGHTS );
			ShaderAPITextureHandle_t hCookies[DX12_PBR_MAX_PROJECTED_LIGHTS];
			ShaderAPITextureHandle_t hDepths[DX12_PBR_MAX_PROJECTED_LIGHTS];
			for ( int i = 0; i < nCount; ++i )
			{
				hCookies[i] = GetShaderAPITextureBindHandle( COOKIE0 + i, COOKIEFRAME0 + i );
				// $depthN is set only for shadowed lights; asking an undefined var for its texture warns and yields the error texture.
				hDepths[i] = params[DEPTH0 + i]->IsTexture() ? GetShaderAPITextureBindHandle( DEPTH0 + i, -1 ) : INVALID_SHADERAPI_TEXTURE_HANDLE;
			}
			DX12ShadowmapLighting()->SetProjectedLightTextures( hCookies, hDepths, nCount );
		}
		Draw( false );
	}
END_SHADER

//-----------------------------------------------------------------------------
// DX12_PBRGBufferDebug: $mode 0 normals, 1 F0, 2 roughness, written to $output (RGBA8).
//-----------------------------------------------------------------------------
BEGIN_VS_SHADER( DX12_PBRGBufferDebug, "PBR G-buffer debug view (compute)" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( OUTPUT, SHADER_PARAM_TYPE_TEXTURE, "", "RGBA8 render target the view is written to" )
		SHADER_PARAM( MODE, SHADER_PARAM_TYPE_INTEGER, "0", "0 normals, 1 F0, 2 roughness" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
	}

	SHADER_DRAW
	{
		DYNAMIC_STATE
		{
			float flWidth = 0.0f, flHeight = 0.0f;
			GetTextureDimensions( &flWidth, &flHeight, OUTPUT );

			// Mirrors cbuffer PBRGBufferDebug : register( b0, space1 ) in pbr_gbuffer_debug_cs51.fxc.
			const uint32 constants[4] = { uint32( params[MODE]->GetIntValue() ), uint32( flWidth ), uint32( flHeight ), 0 };

			IShaderAPIDX12Compute *pCompute = DX12ComputeAPI();
			pbr_gbuffer_debug_cs51_Static_Index staticIndex;
			staticIndex.SetMSAA( pCompute->SceneSampleCount() > 1 );
			pbr_gbuffer_debug_cs51_Dynamic_Index dynamicIndex;

			ShaderAPIDX12ComputeDispatch_t dispatch = {};
			dispatch.m_pShaderName = "pbr_gbuffer_debug_cs51";
			dispatch.m_nStaticIndex = staticIndex.GetIndex();
			dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
			dispatch.m_Srv[0] = DX12MaterialPbrNormals();
			dispatch.m_Srv[1] = DX12MaterialPbrSpecular();
			dispatch.m_Srv[2] = DX12MaterialSceneDepth();
			dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( OUTPUT, -1 ) );
			dispatch.m_pConstants = constants;
			dispatch.m_nConstantBytes = sizeof( constants );
			dispatch.m_nGroupsX = ( constants[1] + 7 ) / 8;
			dispatch.m_nGroupsY = ( constants[2] + 7 ) / 8;
			dispatch.m_nGroupsZ = 1;
			pCompute->Dispatch( dispatch );
		}
		Draw( false );
	}
END_SHADER
