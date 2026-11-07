//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native SM5 port of materialsystem/stdshaders/BaseVSShader.h (DX9 helpers only; the DX8/DX7 passes
//          and their shaders do not exist below the DX12 material level).
//
// Material constants: the material code keeps the DX9 vocabulary (register numbers from the DX9 register maps,
// the same helper names and arguments). Every constant write lands in a legacy-register STAGING file owned by this
// DLL instead of the shader API. Constructing a generated <logical>_Dynamic_Index selects that logical shader's
// generated material block type (generated/inc, nativeshaderpack_dx12); Draw() then builds each selected block from
// the staging registers through the block's own legacy map (the exact inverse of the backend mirror) and sends it
// through the private space-1 pointer bridge. Engine-owned values (matrices, fog, ambient cube, lights, bones,
// tone mapping, raster emulation) still go through their IShaderDynamicAPI calls and the backend's engine blocks.
//
// Staging is sticky across draws exactly like the DX9 register file: a value a material does not rewrite keeps
// the last value any material in this DLL staged for that register.
//===========================================================================//

#ifndef BASEVSSHADERDX12_H
#define BASEVSSHADERDX12_H

#ifdef _WIN32
#pragma once
#endif

#include "shaderlib/cshader.h"
#include "shaderlib/BaseShader.h"
#include "convar.h"
#include <renderparm.h>
#include "native_cbuffer_dx12.h"
#include <cstring>
#include <type_traits>

//-----------------------------------------------------------------------------
// Legacy-register staging and native block selection
//-----------------------------------------------------------------------------
struct DX12NoNativeBlock {};

struct DX12ConstantStaging
{
	// vs float: c0..c1535 (vs30 morph targets read cFlexWeights at c1024..c1535); ps float: ps_3_0 c0..c223.
	enum { kVSFloat = 1536, kPSFloat = 224, kInt = 16, kBool = 16 };
	float vsFloat[kVSFloat][4];
	float psFloat[kPSFloat][4];
	int vsInt[kInt][4];
	int psInt[kInt][4];
	int vsBool[kBool];
	int psBool[kBool];
	// Block writers of the logical shaders selected since the last Draw (null: none / no material block).
	void ( *vsWriter )( IShaderDynamicAPI *api, const DX12ConstantStaging &staging );
	void ( *psWriter )( IShaderDynamicAPI *api, const DX12ConstantStaging &staging );
	// A shader selected by a name that has no native logical draws from the legacy shaders/fxc record: the
	// registers staged since the last replay are then written to the shader API's legacy register files.
	bool vsPassthrough, psPassthrough;
	uint32_t vsFloatDirty[kVSFloat / 32], psFloatDirty[kPSFloat / 32];
	uint32_t vsIntDirty, psIntDirty, vsBoolDirty, psBoolDirty;
	// Set only by an admitted receiver or an explicitly non-lighting auxiliary pass;
	// consumed/reset at Draw so an earlier pass cannot admit an unknown later one.
	bool shadowmapPassAdmitted;
};
extern DX12ConstantStaging g_DX12Constants;

// Same signatures and register semantics as the IShaderDynamicAPI setters they replace in material code.
void DX12SetVertexShaderConstant( int reg, const float *data, int count = 1, bool force = false );
void DX12SetPixelShaderConstant( int reg, const float *data, int count = 1, bool force = false );
void DX12SetBooleanVertexShaderConstant( int reg, const int *data, int count = 1, bool force = false );
void DX12SetIntegerVertexShaderConstant( int reg, const int *data, int count = 1, bool force = false );
void DX12SetBooleanPixelShaderConstant( int reg, const int *data, int count = 1, bool force = false );
void DX12SetIntegerPixelShaderConstant( int reg, const int *data, int count = 1, bool force = false );
// Material-register form of IShaderDynamicAPI::SetDepthFeatheringPixelShaderConstant (staged like the others).
void DX12SetDepthFeatheringPixelShaderConstant( int reg, float depthBlendScale );
// Native logical of a shader named by its DX9 file name: "<base>_(vs|ps)(11|14|20|20b|2x|30|xx)" -> "<base>_<stage>51"
// (the SM5.1 profile every native source is compiled for). Names without such a suffix are copied unchanged.
void DX12NativeShaderName( const char *name, const char *stage, char *out, int outSize );

// Sends one generated block through the private bridge (magic/version/stage/space/register/size/hash/map).
template< class CB > void DX12WriteNativeCBuffer( IShaderDynamicAPI *api, const CB &block )
{
	static_assert( std::is_trivially_copyable< CB >::value && std::is_standard_layout< CB >::value,
		"A native block must be byte-identical to its reflected HLSL layout" );
	static_assert( CB::kSize == sizeof( CB ) && CB::kSize != 0 && ( CB::kSize % 16 ) == 0 && CB::kSize <= 65536,
		"Invalid native cbuffer size" );
	static_assert( ( CB::kStage == dx12native::kStageVertex && CB::kRegister >= 2 && CB::kRegister <= 7 ) ||
		( CB::kStage == dx12native::kStagePixel && CB::kRegister >= 1 && CB::kRegister <= 7 ),
		"Material cbuffer register is reserved or out of range" );
	const dx12native::NativeCBufferWriteDX12 write = {
		dx12native::kDX12NativeCBufferWriteMagic, dx12native::kDX12NativeCBufferVersion,
		CB::kStage, 1, CB::kRegister, CB::kSize, CB::kLayoutHash, &block, &CB::LegacyMap()
	};
	if ( CB::kStage == dx12native::kStageVertex )
		api->SetVertexShaderConstant( dx12native::kDX12NativeCBufferPointerVar, reinterpret_cast< const float * >( &write ), 1, true );
	else
		api->SetPixelShaderConstant( dx12native::kDX12NativeCBufferPointerVar, reinterpret_cast< const float * >( &write ), 1, true );
}

// Inverse of the backend mirror: every legacy-map entry gathers its registers from the staging file.
void DX12GatherLegacyRegisters( const dx12native::NativeCBufferLegacyMapDX12 &map, uint32_t stage,
	const DX12ConstantStaging &staging, void *block );

template< class CB > void DX12WriteBlockFromStaging( IShaderDynamicAPI *api, const DX12ConstantStaging &staging )
{
	CB block;
	DX12GatherLegacyRegisters( CB::LegacyMap(), CB::kStage, staging, &block );
	DX12WriteNativeCBuffer( api, block );
}

typedef void ( *DX12NativeBlockWriter )( IShaderDynamicAPI *api, const DX12ConstantStaging &staging );
inline void DX12SelectNativeBlockWriter( uint32_t stage, DX12NativeBlockWriter writer )
{
	if ( stage == dx12native::kStageVertex ) { g_DX12Constants.vsWriter = writer; g_DX12Constants.vsPassthrough = false; }
	else { g_DX12Constants.psWriter = writer; g_DX12Constants.psPassthrough = false; }
}
template< class CB > inline void DX12SelectNativeBlock( uint32_t stage )
{
	DX12SelectNativeBlockWriter( stage, &DX12WriteBlockFromStaging< CB > );
}
template<> inline void DX12SelectNativeBlock< DX12NoNativeBlock >( uint32_t stage )
{
	DX12SelectNativeBlockWriter( stage, nullptr );
}

// Name registry (generated/inc/native_block_registry.inc) for shaders selected by name
// (IShaderShadow::Set*Shader( name ) + Set*ShaderIndex). Unknown names select legacy passthrough.
struct DX12NativeBlockRegistryEntry { const char *logical; uint32_t stage; DX12NativeBlockWriter writer; };
// Logical shaders that only exist as legacy shaders/fxc records (manifest "# legacy" lines): their generated index
// classes select passthrough, so the staged registers reach the legacy register files.
inline void DX12SelectLegacyPassthrough( uint32_t stage )
{
	DX12SelectNativeBlockWriter( stage, nullptr );
	( stage == dx12native::kStageVertex ? g_DX12Constants.vsPassthrough : g_DX12Constants.psPassthrough ) = true;
}
void DX12SelectNativeBlockByName( const char *name, uint32_t stage );

// Writes and clears the selected blocks (called by CBaseVSShaderDX12::Draw before a real dynamic draw).
void DX12FlushNativeBlocks( IShaderDynamicAPI *api );

// Executes a CCommandBufferBuilder stream like IShaderDynamicAPI::ExecuteCommandBuffer, except that every material
// constant it sets (float constants, eye position, depth-feathering scale) is staged instead of written to the
// legacy register files; engine commands (fog/lighting/ambient registers, textures, shader indices) go to the API.
void DX12ExecuteCommandBuffer( IShaderDynamicAPI *api, uint8 *buffer );

//-----------------------------------------------------------------------------
// Helper macros for vertex shaders
//-----------------------------------------------------------------------------
#define BEGIN_VS_SHADER_FLAGS(_name, _help, _flags)	__BEGIN_SHADER_INTERNAL( CBaseVSShaderDX12, _name, _help, _flags )
#define BEGIN_VS_SHADER(_name,_help)	__BEGIN_SHADER_INTERNAL( CBaseVSShaderDX12, _name, _help, 0 )

// useful parameter initialization macro
#define INIT_FLOAT_PARM( parm, value )					\
		if ( !params[(parm)]->IsDefined() )				\
		{												\
			params[(parm)]->SetFloatValue( (value) );	\
		}

// The DX12 DLL has one native pixel shader per base: these select <base>_ps51 (compiled from the ps20b source).
#define SET_STATIC_PS2X_PIXEL_SHADER_NO_COMBOS( basename )		\
		{														\
			DECLARE_STATIC_PIXEL_SHADER( basename##_ps51 );	\
			SET_STATIC_PIXEL_SHADER( basename##_ps51 );		\
		}

#define SET_DYNAMIC_PS2X_PIXEL_SHADER_NO_COMBOS( basename )		\
		{														\
			DECLARE_DYNAMIC_PIXEL_SHADER( basename##_ps51 );	\
			SET_DYNAMIC_PIXEL_SHADER( basename##_ps51 );		\
		}


//-----------------------------------------------------------------------------
// Base class for shaders, contains helper methods.
//-----------------------------------------------------------------------------
class CBaseVSShaderDX12 : public CBaseShader
{
public:
	// Hides CBaseShader::Draw: a real dynamic draw first writes the selected native blocks.
	void Draw( bool bMakeActualDrawCall = true );
	// Receiver admission is scoped to one DrawElements: a lit material's secondary passes
	// (DrawEqualDepthToDestAlpha, ...) inherit the primary pass's admission.
	void DrawElements( IMaterialVar **params, int nModulationFlags, IShaderShadow *pShaderShadow, IShaderDynamicAPI *pShaderAPI,
		VertexCompressionType_t vertexCompression, CBasePerMaterialContextData **pContext ) override;

	// Loads bump lightmap coordinates into the pixel shader
	void LoadBumpLightmapCoordinateAxes_PixelShader( int pixelReg );

	// Loads bump lightmap coordinates into the vertex shader
	void LoadBumpLightmapCoordinateAxes_VertexShader( int vertexReg );

	// Pixel and vertex shader constants....
	void SetPixelShaderConstant( int pixelReg, int constantVar );

	// Pixel and vertex shader constants....
	void SetPixelShaderConstantGammaToLinear( int pixelReg, int constantVar );

	// This version will put constantVar into x,y,z, and constantVar2 into the w
	void SetPixelShaderConstant( int pixelReg, int constantVar, int constantVar2 );
	void SetPixelShaderConstantGammaToLinear( int pixelReg, int constantVar, int constantVar2 );

	// Helpers for setting constants that need to be converted to linear space (from gamma space).
	void SetVertexShaderConstantGammaToLinear( int var, float const* pVec, int numConst = 1, bool bForce = false );
	void SetPixelShaderConstantGammaToLinear( int var, float const* pVec, int numConst = 1, bool bForce = false );

	void SetVertexShaderConstant( int vertexReg, int constantVar );

	// set rgb components of constant from a color parm and give an explicit w value
	void SetPixelShaderConstant_W( int pixelReg, int constantVar, float fWValue );

	// GR - fix for const/lerp issues
	void SetPixelShaderConstantFudge( int pixelReg, int constantVar );

	// Sets light direction for pixel shaders.
	void SetPixelShaderLightColors( int pixelReg );

	// Sets vertex shader texture transforms
	void SetVertexShaderTextureTranslation( int vertexReg, int translationVar );
	void SetVertexShaderTextureScale( int vertexReg, int scaleVar );
 	void SetVertexShaderTextureTransform( int vertexReg, int transformVar );
	void SetVertexShaderTextureScaledTransform( int vertexReg,
											int transformVar, int scaleVar );

	// Set pixel shader texture transforms
	void SetPixelShaderTextureTranslation( int pixelReg, int translationVar );
	void SetPixelShaderTextureScale( int pixelReg, int scaleVar );
 	void SetPixelShaderTextureTransform( int pixelReg, int transformVar );
	void SetPixelShaderTextureScaledTransform( int pixelReg,
											int transformVar, int scaleVar );

	// Moves a matrix into vertex shader constants
	void SetVertexShaderMatrix2x4( int vertexReg, int matrixVar );
	void SetVertexShaderMatrix3x4( int vertexReg, int matrixVar );
	void SetVertexShaderMatrix4x4( int vertexReg, int matrixVar );

	// Loads the view matrix into vertex shader constants
	void LoadViewMatrixIntoVertexShaderConstant( int vertexReg );

	// Loads the projection matrix into vertex shader constants
	void LoadProjectionMatrixIntoVertexShaderConstant( int vertexReg );

	// Loads the model->view matrix into vertex shader constants
	void LoadModelViewMatrixIntoVertexShaderConstant( int vertexReg );

	// Loads a scale/offset version of the viewport transform into the specified constant.
	void LoadViewportTransformScaledIntoVertexShaderConstant( int vertexReg );

	// Sets up ambient light cube...
	void SetAmbientCubeDynamicStateVertexShader( );
	float GetAmbientLightCubeLuminance( );

	// Helpers for dealing with envmaptint
	void SetEnvMapTintPixelShaderDynamicState( int pixelReg, int tintVar, int alphaVar, bool bConvertFromGammaToLinear = false );

	// Helper methods for pixel shader overbrighting
	void EnablePixelShaderOverbright( int reg, bool bEnable, bool bDivideByTwo );

	// Helper for dealing with modulation
	void SetModulationVertexShaderDynamicState();
	void SetModulationPixelShaderDynamicState( int modulationVar );
	void SetModulationPixelShaderDynamicState_LinearColorSpace( int modulationVar );
	void SetModulationPixelShaderDynamicState_LinearColorSpace_LinearScale( int modulationVar, float flScale );

	// Sets a color + alpha into shader constants
	void SetColorVertexShaderConstant( int nVertexReg, int colorVar, int alphaVar );
	void SetColorPixelShaderConstant( int nPixelReg, int colorVar, int alphaVar );

	// Sets up hw morphing state for the vertex shader
	void SetHWMorphVertexShaderState( int nDimConst, int nSubrectConst, VertexTextureSampler_t morphSampler );

	// Helper for setting up flashlight constants
	void SetFlashlightVertexShaderConstants( bool bBump, int bumpTransformVar, bool bDetail, int detailScaleVar, bool bSetTextureTransforms );

	struct DrawFlashlight_dx90_Vars_t
	{
		DrawFlashlight_dx90_Vars_t()
		{
			// set all ints to -1
			memset( this, 0xFF, sizeof(DrawFlashlight_dx90_Vars_t) );
			// set all bools to a default value.
			m_bBump = false;
			m_bLightmappedGeneric = false;
			m_bWorldVertexTransition = false;
			m_bTeeth = false;
			m_bSSBump = false;
			m_fSeamlessScale = 0.0;
		}
		bool m_bBump;
		bool m_bLightmappedGeneric;
		bool m_bWorldVertexTransition;
		bool m_bTeeth;
		int m_nBumpmapVar;
		int m_nBumpmapFrame;
		int m_nBumpTransform;
		int m_nFlashlightTextureVar;
		int m_nFlashlightTextureFrameVar;
		int m_nBaseTexture2Var;
		int m_nBaseTexture2FrameVar;
		int m_nBumpmap2Var;
		int m_nBumpmap2Frame;
		int m_nBump2Transform;
		int m_nDetailVar;
		int m_nDetailScale;
		int m_nDetailTextureCombineMode;
		int m_nDetailTextureBlendFactor;
		int m_nDetailTint;
		int m_nTeethForwardVar;
		int m_nTeethIllumFactorVar;
		int m_nAlphaTestReference;
		bool m_bSSBump;
		float m_fSeamlessScale;								// 0.0 = not seamless
	};
	// Every stdshader caller draws the lightmapped variant; the non-lightmapped DX9 path used the DX8
	// vertexlitgeneric_flashlight_vs11 assembly shader and is rejected.
	void DrawFlashlight_dx90( IMaterialVar** params,
		IShaderDynamicAPI *pShaderAPI, IShaderShadow* pShaderShadow, DrawFlashlight_dx90_Vars_t &vars );

	BlendType_t EvaluateBlendRequirements( int textureVar, bool isBaseTexture, int detailTextureVar = -1 );

	void HashShadow2DJitter( const float fJitterSeed, float *fU, float* fV );

	//Alpha tested materials can end up leaving garbage in the dest alpha buffer if they write depth.
	//This pass fills in the areas that passed the alpha test with depth in dest alpha
	//by writing only equal depth pixels and only if we should be writing depth to dest alpha
	void DrawEqualDepthToDestAlpha( void );

private:
	// Converts a color + alpha into a vector4
	void ColorVarsToVector( int colorVar, int alphaVar, Vector4D &color );

};

FORCEINLINE void SetFlashLightColorFromState( FlashlightState_t const &state, IShaderDynamicAPI *pShaderAPI, int nPSRegister=28, bool bFlashlightNoLambert=false )
{
	// Force flashlight to 25% bright always
	float flFlashlightScale = 0.25f;

	if ( !g_pHardwareConfig->GetHDREnabled() )
	{
		// Non-HDR path requires 2.0 flashlight
		flFlashlightScale = 2.0f;
	}

	// DX10 requires some hackery due to sRGB/blend ordering change from DX9
	if ( g_pHardwareConfig->UsesSRGBCorrectBlending() )
	{
		flFlashlightScale *= 2.5f; // Magic number that works well on the NVIDIA 8800
	}

	// Generate pixel shader constant
	float const *pFlashlightColor = state.m_Color;
	float vPsConst[4] = { flFlashlightScale * pFlashlightColor[0], flFlashlightScale * pFlashlightColor[1], flFlashlightScale * pFlashlightColor[2], pFlashlightColor[3] };
	vPsConst[3] = bFlashlightNoLambert ? 2.0f : 0.0f; // This will be added to N.L before saturate to force a 1.0 N.L term

	DX12SetPixelShaderConstant( nPSRegister, ( float * )vPsConst );
}

FORCEINLINE float ShadowAttenFromState( FlashlightState_t const &state )
{
	// DX10 requires some hackery due to sRGB/blend ordering change from DX9, which makes the shadows too light
	if ( g_pHardwareConfig->UsesSRGBCorrectBlending() )
		return state.m_flShadowAtten * 0.1f; // magic number

	return state.m_flShadowAtten;
}

FORCEINLINE float ShadowFilterFromState( FlashlightState_t const &state )
{
	// We developed shadow maps at 1024, so we expect the penumbra size to have been tuned relative to that
	return state.m_flShadowFilterSize / 1024.0f;
}


// convenient material variable access functions for helpers to use.
FORCEINLINE bool IsTextureSet( int nVar, IMaterialVar **params )
{
	return ( nVar != -1 ) && ( params[nVar]->IsTexture() );
}

FORCEINLINE bool IsBoolSet( int nVar, IMaterialVar **params )
{
	return ( nVar != -1 ) && ( params[nVar]->GetIntValue() );
}

FORCEINLINE int GetIntParam( int nVar, IMaterialVar **params, int nDefaultValue = 0 )
{
	return ( nVar != -1 ) ? ( params[nVar]->GetIntValue() ) : nDefaultValue;
}

FORCEINLINE float GetFloatParam( int nVar, IMaterialVar **params, float flDefaultValue = 0.0 )
{
	return ( nVar != -1 ) ? ( params[nVar]->GetFloatValue() ) : flDefaultValue;
}

FORCEINLINE void InitFloatParam( int nIndex, IMaterialVar **params, float flValue )
{
	if ( (nIndex != -1) && !params[nIndex]->IsDefined() )
	{
		params[nIndex]->SetFloatValue( flValue );
	}
}

FORCEINLINE void InitIntParam( int nIndex, IMaterialVar **params, int nValue )
{
	if ( (nIndex != -1) && !params[nIndex]->IsDefined() )
	{
		params[nIndex]->SetIntValue( nValue );
	}
}


class ConVar;

#ifdef _DEBUG
extern ConVar mat_envmaptintoverride;
extern ConVar mat_envmaptintscale;
#endif


#endif // BASEVSSHADERDX12_H
