//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CPU vertex layout for the current Source mesh descriptor and DX12 input assembly.
//
//=============================================================================//

#ifndef SHADERAPIDX12_VERTEX_LAYOUT_DX12_H
#define SHADERAPIDX12_VERTEX_LAYOUT_DX12_H
#pragma once

#include "materialsystem/imesh.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include <dxgiformat.h>
#include <stdint.h>

namespace shaderapidx12
{
struct ShaderLinkageDX12;

// D3DCOLOR in Source memory is BGRA. DX12 consumes its bytes as RGBA_UNORM;
// translated vertex shaders must exchange R/B for these semantic inputs.
// Packed normal/tangent bytes and packed bone weights are integer inputs,
// not normalized vertex attributes; Source shader code decodes them itself.
struct VertexInputDX12
{
	const char *semantic;
	uint32_t semanticIndex;
	DXGI_FORMAT format;
	uint32_t inputSlot;
	uint32_t byteOffset;
	// DX12 IA uses R8G8B8A8_UNORM for Source D3DCOLOR bytes. The translator
	// must swap R/B; do not substitute B8G8R8A8_UNORM, which is not a
	// portable FL11 input-layout format.
	bool swapRedBlue;
	// SHORT2/UBYTE4 fetches must be cast numerically to the legacy float
	// input register; unlike D3DCOLOR they are not normalized.
	bool integerToFloat;
};

enum
{
	MAX_VERTEX_INPUTS_DX12 = 23
};

// Optional streams are declared only when the matching VS input is requested.
// Mesh binding supplies stream 1 (lighting), 2 (flex), 3 (morph), or a
// fence-owned zero-stride stream if a shader reads a missing element.
struct VertexInputStreamsDX12
{
	bool staticLighting;
	bool flex;
	bool morph;
	bool flexWrinkle;
};

struct VertexLayoutDX12
{
	uint32_t stride;
	uint32_t inputCount;
	VertexInputDX12 inputs[MAX_VERTEX_INPUTS_DX12];
	bool valid;
};

struct ShaderInputElementDX12
{
	CUtlString semantic;
	uint32_t semanticIndex = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

// Reflect once when a shader variant is created. Missing IA fields use a
// zero-stride stream of four zero words, with the signature's component type.
bool ReadShaderInputSignatureDX12( const void *pBytecode, size_t nBytes,
    CUtlVector<ShaderInputElementDX12> &inputs,
    uint32_t ( *pConstantRegisters )[3] = nullptr,
    CUtlVector<ShaderLinkageDX12> *pOutputs = nullptr );

// Sole source of field offsets, stride, and input declaration metadata.
// pVertexData may be null for format-only queries; present descriptor pointers
// then remain null and must not be dereferenced. Absent fields always point to
// thread-local dummy storage with a zero per-vertex advance.
// The returned layout is independent of the lifetime of pVertexData/pDesc.
VertexLayoutDX12 ComputeVertexLayoutDX12( VertexFormat_t format, unsigned char *pVertexData = nullptr,
    VertexDesc_t *pDesc = nullptr,
    VertexInputStreamsDX12 streams = { false, false, false, false } );

inline int VertexFormatSizeDX12( VertexFormat_t format )
{
	// Per-thread memo: callers (mesh locks, dynamic mesh setup) repeat a handful of formats every frame.
	struct Entry
	{
		VertexFormat_t format;
		int size;
	};

	thread_local Entry s_Cache[8] = {};
	thread_local bool s_bValid[8] = {};
	const unsigned nSlot = static_cast<unsigned>( ( format ^ ( format >> 17 ) ^ ( format >> 37 ) ) & 7 );
	if ( s_bValid[nSlot] && s_Cache[nSlot].format == format )
		return s_Cache[nSlot].size;
	const VertexLayoutDX12 layout = ComputeVertexLayoutDX12( format );
	const int nSize = layout.valid ? static_cast<int>( layout.stride ) : 0;
	s_Cache[nSlot] = { format, nSize };
	s_bValid[nSlot] = true;
	return nSize;
}

} // namespace shaderapidx12

#endif // SHADERAPIDX12_VERTEX_LAYOUT_DX12_H
