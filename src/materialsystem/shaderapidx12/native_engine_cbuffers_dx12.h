//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: C++ mirrors and reflection layouts of the engine-owned native DX12
//			constant buffers (DX12VSEngine, DX12VSBones, DX12PSEngine, DX12MotionVS)
//
//=============================================================================//

#ifndef NATIVE_ENGINE_CBUFFERS_DX12_H
#define NATIVE_ENGINE_CBUFFERS_DX12_H

#include "native_cbuffer_dx12.h"
#include "shaderapi/ishaderapidx12lighting.h"
#include <cstddef>
#include <cstdint>

namespace dx12native
{
struct DX12LightInfo
{
	float color[4], dir[4], pos[4], spotParams[4], atten[4];
};

struct alignas( 16 ) DX12VSEngine
{
	float cConstants0[4];
	float cConstants1[4];
	float cEyePosWaterZ[4];
	float cFlexScale[4];
	float cModelViewProj[16];
	float cViewProj[16];
	float cModelViewProjZ[4];
	float cViewProjZ[4];
	float cFogParams[4];
	float cViewModel[16];
	float cAmbientCube[6][4];
	DX12LightInfo cLightInfo[4];
	int32_t cLightCount[4];
	uint32_t cLightEnabled[4];
	float cViewportScale[4];
	float cClipPlanes[6][4];
	uint32_t cClipMask[4];
};

struct alignas( 16 ) DX12VSBones
{
	float cModel[53][12];
};

struct alignas( 16 ) DX12PSEngine
{
	float cPixelFogParams[4];
	float cLinearFogColor[4];
	float cLightScale[4];
	float cAmbientCube[6][4]; // HLSL `float3 cAmbientCube[6]` (legacy type; .w of each row unused)
	float cLightInfo[6][4];   // HLSL `PixelShaderLightInfo cLightInfo[3]` { float4 color, pos; }
	float cAlphaTest[4];
	float cRasterFogColor[4];
	float cRasterFogParams[4];
};

struct alignas( 16 ) DX12ComboFold
{
	uint32_t cComboFold[16][4];
};

struct EngineCBufferMemberDX12
{
	const char *name;
	uint32_t offset, size;
};

struct EngineCBufferLayoutDX12
{
	const char *name;
	uint32_t stage, shaderRegister, byteSize, memberCount;
	const EngineCBufferMemberDX12 *members;
};

#define DX12_ENGINE_MEMBER( type, member, expected )                                                                  \
	( []() constexpr {                                                                                                \
		static_assert( offsetof( type, member ) == expected, #type "::" #member " offset" );                          \
		return EngineCBufferMemberDX12{ #member, uint32_t( expected ), uint32_t( sizeof( ( (type *)0 )->member ) ) }; \
	}() )
// HLSL arrays whose last element is narrower than a row report a shorter Size than the C++ rows.
#define DX12_ENGINE_MEMBER_SIZED( type, member, expected, hlslSize )                              \
	( []() constexpr {                                                                            \
		static_assert( offsetof( type, member ) == expected, #type "::" #member " offset" );      \
		static_assert( hlslSize <= sizeof( ( (type *)0 )->member ), #type "::" #member " size" ); \
		return EngineCBufferMemberDX12{ #member, uint32_t( expected ), uint32_t( hlslSize ) };    \
	}() )
static_assert( sizeof( DX12LightInfo ) == 80, "DX12LightInfo size" );
static_assert( sizeof( DX12VSEngine ) == 880, "DX12VSEngine size" );
static_assert( sizeof( DX12VSBones ) == 2544, "DX12VSBones size" );
static_assert( sizeof( DX12PSEngine ) == 288, "DX12PSEngine size" );
static constexpr EngineCBufferMemberDX12 kDX12VSEngineMembers[] = {
    DX12_ENGINE_MEMBER( DX12VSEngine, cConstants0, 0 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cConstants1, 16 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cEyePosWaterZ, 32 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cFlexScale, 48 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cModelViewProj, 64 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cViewProj, 128 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cModelViewProjZ, 192 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cViewProjZ, 208 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cFogParams, 224 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cViewModel, 240 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cAmbientCube, 304 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cLightInfo, 400 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cLightCount, 720 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cLightEnabled, 736 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cViewportScale, 752 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cClipPlanes, 768 ),
    DX12_ENGINE_MEMBER( DX12VSEngine, cClipMask, 864 ),
};
static constexpr EngineCBufferMemberDX12 kDX12VSBonesMembers[] = {
    DX12_ENGINE_MEMBER( DX12VSBones, cModel, 0 ),
};
static constexpr EngineCBufferMemberDX12 kDX12PSEngineMembers[] = {
    DX12_ENGINE_MEMBER( DX12PSEngine, cPixelFogParams, 0 ),
    DX12_ENGINE_MEMBER( DX12PSEngine, cLinearFogColor, 16 ),
    DX12_ENGINE_MEMBER( DX12PSEngine, cLightScale, 32 ),
    DX12_ENGINE_MEMBER_SIZED( DX12PSEngine, cAmbientCube, 48, 92 ),
    DX12_ENGINE_MEMBER( DX12PSEngine, cLightInfo, 144 ),
    DX12_ENGINE_MEMBER( DX12PSEngine, cAlphaTest, 240 ),
    DX12_ENGINE_MEMBER( DX12PSEngine, cRasterFogColor, 256 ),
    DX12_ENGINE_MEMBER( DX12PSEngine, cRasterFogParams, 272 ),
};
static_assert( sizeof( DX12ComboFold ) == 256, "DX12ComboFold size" );
static constexpr EngineCBufferMemberDX12 kDX12ComboFoldMembers[] = {
	DX12_ENGINE_MEMBER( DX12ComboFold, cComboFold, 0 ),
};

struct alignas( 16 ) DX12MotionVS
{
	float cPrevViewProj[16];
	float cMotionParams[4];
	float cBaseTexTransform[2][4];
	float cPrevModel[53][12];
};

static_assert( sizeof( DX12MotionVS ) == 2656, "DX12MotionVS size" );
static constexpr EngineCBufferMemberDX12 kDX12MotionVSMembers[] = {
    DX12_ENGINE_MEMBER( DX12MotionVS, cPrevViewProj, 0 ),
    DX12_ENGINE_MEMBER( DX12MotionVS, cMotionParams, 64 ),
    DX12_ENGINE_MEMBER( DX12MotionVS, cBaseTexTransform, 80 ),
    DX12_ENGINE_MEMBER( DX12MotionVS, cPrevModel, 112 ),
};
// Lighting V1 constant-buffer layouts (space 2, feature variants only). Reflected block names and member offsets are
// validated by the packer and the runtime against these layouts; the CPU structs live in
// public/shaderapi/ishaderapidx12lighting.h and the HLSL twins in hlsl/common/dx12_engine_cbuffers.h.
static_assert( sizeof( DX12LightingViewConstantsV1 ) == 672, "DX12LightingViewConstantsV1 size" );
static_assert( sizeof( RuntimeShadowLightGpu ) == 592, "RuntimeShadowLightGpu size" );
static constexpr EngineCBufferMemberDX12 kDX12LightingViewConstantsV1Members[] = {
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cShadowView0, 0 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cShadowView1, 16 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cShadowViewport, 32 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cSunRadiance, 48 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cSunTravel, 64 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cSunBasisX, 80 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cSunBasisY, 96 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cEyePosition, 112 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cViewForward, 128 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cCascadeSplits, 144 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cCascadeBlend, 160 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cShadowDepthRecords, 176 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cSunWorldToClip, 256 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cStaticSunWorldToClip, 512 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cCascadeRects, 576 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cStaticSunRect, 640 ),
    DX12_ENGINE_MEMBER( DX12LightingViewConstantsV1, cSunIdentity, 656 ),
};
// ABI 4 explicit highres per-draw block, b0 space3. Styles are packed four per HLSL row.
struct alignas(16) DX12HighresDrawConstants
{
	uint32_t cHlightRoute[4];
	float cHlightModelToWorld[3][4];
	float cHlightStyles[16][4];
};
static_assert(sizeof(DX12HighresDrawConstants) == 320, "DX12HighresDrawConstants size");
static constexpr EngineCBufferMemberDX12 kDX12HighresDrawConstantsMembers[] = {
	DX12_ENGINE_MEMBER(DX12HighresDrawConstants, cHlightRoute, 0),
	DX12_ENGINE_MEMBER(DX12HighresDrawConstants, cHlightModelToWorld, 16),
	DX12_ENGINE_MEMBER(DX12HighresDrawConstants, cHlightStyles, 64),
};
#undef DX12_ENGINE_MEMBER
#undef DX12_ENGINE_MEMBER_SIZED
static constexpr EngineCBufferLayoutDX12 kEngineCBufferLayouts[] = {
    { "DX12VSEngine", kStageVertex, 0, sizeof( DX12VSEngine ), sizeof( kDX12VSEngineMembers ) / sizeof( *kDX12VSEngineMembers ), kDX12VSEngineMembers },
    { "DX12VSBones", kStageVertex, 1, sizeof( DX12VSBones ), sizeof( kDX12VSBonesMembers ) / sizeof( *kDX12VSBonesMembers ), kDX12VSBonesMembers },
    { "DX12PSEngine", kStagePixel, 0, sizeof( DX12PSEngine ), sizeof( kDX12PSEngineMembers ) / sizeof( *kDX12PSEngineMembers ), kDX12PSEngineMembers },
	{ "DX12ComboFoldPS", kStagePixel, 2, sizeof( DX12ComboFold ), 1, kDX12ComboFoldMembers },
	{ "DX12ComboFoldVS", kStageVertex, 3, sizeof( DX12ComboFold ), 1, kDX12ComboFoldMembers },
    { "DX12MotionVS", kStageVertex, 7, sizeof( DX12MotionVS ), sizeof( kDX12MotionVSMembers ) / sizeof( *kDX12MotionVSMembers ), kDX12MotionVSMembers },
};
// Space-2 lighting block (pixel stage). shaderRegister is the register inside DX12_LIGHTING_REGISTER_SPACE.
static constexpr EngineCBufferLayoutDX12 kLightingCBufferLayouts[] = {
    { "DX12LightingViewConstantsV1", kStagePixel, DX12_LIGHTING_B_VIEW, sizeof( DX12LightingViewConstantsV1 ), sizeof( kDX12LightingViewConstantsV1Members ) / sizeof( *kDX12LightingViewConstantsV1Members ), kDX12LightingViewConstantsV1Members },
};
// RuntimeShadowLightGpu (StructuredBuffer at t1026 space2) member table for reflection validation (packer + runtime).
// valueClass / scalarType use the D3D_SHADER_VARIABLE_CLASS / D3D_SHADER_VARIABLE_TYPE numeric values
// (D3D_SVC_SCALAR 0, D3D_SVC_VECTOR 1, D3D_SVC_MATRIX_ROWS 2; D3D_SVT_UINT 19, D3D_SVT_FLOAT 3) so this header
// stays free of d3dcommon.h. Offsets/rows/columns/elements are the HLSL declaration in dx12_engine_cbuffers.h.
struct LightingStructuredMemberDX12
{
	const char *name;
	uint32_t offset, valueClass, scalarType, rows, columns, elements;
};
#define DX12_LSM_SCALAR		0u
#define DX12_LSM_VECTOR		1u
#define DX12_LSM_MATRIX_ROWS	2u
#define DX12_LST_FLOAT		3u
#define DX12_LST_UINT		19u
static constexpr LightingStructuredMemberDX12 kRuntimeShadowLightGpuMembers[] = {
    { "lightId", 0, DX12_LSM_SCALAR, DX12_LST_UINT, 1, 1, 0 },
    { "type", 4, DX12_LSM_SCALAR, DX12_LST_UINT, 1, 1, 0 },
    { "style", 8, DX12_LSM_SCALAR, DX12_LST_UINT, 1, 1, 0 },
    { "faceCount", 12, DX12_LSM_SCALAR, DX12_LST_UINT, 1, 1, 0 },
    { "origin", 16, DX12_LSM_VECTOR, DX12_LST_FLOAT, 1, 3, 0 },
    { "attenuationRadius", 28, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "travelDirection", 32, DX12_LSM_VECTOR, DX12_LST_FLOAT, 1, 3, 0 },
    { "innerConeCos", 44, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "radiance", 48, DX12_LSM_VECTOR, DX12_LST_FLOAT, 1, 3, 0 },
    { "outerConeCos", 60, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "constantAttn", 64, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "linearAttn", 68, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "quadraticAttn", 72, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "exponent", 76, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "fadeStart", 80, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "fadeEnd", 84, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "capDist", 88, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "shadowSourceRadius", 92, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "shadowNear", 96, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "shadowFar", 100, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "planeToTexel", 104, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "tanRenderedHalfFov", 108, DX12_LSM_SCALAR, DX12_LST_FLOAT, 1, 1, 0 },
    { "worldToClip", 112, DX12_LSM_MATRIX_ROWS, DX12_LST_FLOAT, 4, 4, 6 },
    { "faces", 496, DX12_LSM_VECTOR, DX12_LST_UINT, 1, 4, 6 },
};
static_assert( offsetof( RuntimeShadowLightGpu, worldToClip ) == 112 && offsetof( RuntimeShadowLightGpu, faces ) == 496, "RuntimeShadowLightGpu layout" );
} // namespace dx12native
#endif
