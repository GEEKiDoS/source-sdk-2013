//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Motion vector pass state shared by the DX12 shader API
//
//=============================================================================//

#ifndef MOTION_VECTORS_DX12_H
#define MOTION_VECTORS_DX12_H
#pragma once

#include "materialsystem/shaderapidx12/native_engine_cbuffers_dx12.h"
#include "tier1/utlhashtable.h"
#include "tier1/utlvector.h"
#include <cstdint>

namespace shaderapidx12
{

enum class MotionPassStateDX12 : uint8_t
{
	None,
	Active,
	Suppressed
};

struct MotionHistoryEntryDX12
{
	uint32_t offset = 0;
	uint32_t count = 0;
	uint64_t materialVS = 0;
};

struct MotionHistoryTableDX12
{
	CUtlHashtable<uint64_t, MotionHistoryEntryDX12> entries;
	CUtlVector<float> rows;

	void Clear()
	{
		entries.RemoveAll();
		rows.RemoveAll();
	}
};

struct ShaderRecordDX12;
// Reflects a native record's cbuffers once (shaderapi_dx12.cpp); shared with the private motion shaders.
bool ReflectNativeCBuffersDX12( ShaderRecordDX12 *pRecord );

} // namespace shaderapidx12

#endif // MOTION_VECTORS_DX12_H
