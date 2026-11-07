#pragma once
#include "native_engine_cbuffers_dx12.h"
namespace dx12cb {
// Engine-owned: the backend fills this block from native state; materials never write it.
using DX12VSBones = dx12native::DX12VSBones;
} // namespace dx12cb
