//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Effective depth/blend/alpha state derived from a shadow snapshot plus overrides.
//
//=============================================================================//

#ifndef SHADER_STATE_DX12_H
#define SHADER_STATE_DX12_H
#pragma once

#include "shaderapi/ishaderapi.h"
#include "shadershadow_dx12.h"

namespace shaderapidx12
{
struct EffectiveStateDX12
{
	ShaderRasterState_t raster{};
	bool depthEnable = true, depthWrite = true, blendEnable = false, alphaTest = false, scissor = false, alphaToCoverage = false, colorWrite = true;
	ShaderBlendFactor_t srcBlend = SHADER_BLEND_ONE, dstBlend = SHADER_BLEND_ZERO;
	ShaderBlendOp_t blendOp = SHADER_BLEND_OP_ADD;
	float alphaRef = 0.0f;
	uint32 stencilReadMask = 0xff, stencilWriteMask = 0xff;
	int stencilRef = 0;
};

class CShaderStateDX12
{
public:
	void Reset();
	void ApplyShadow( const CShaderShadowDX12 &shadow );
	void OverrideDepth( bool bEnabled, bool bWrite );
	void OverrideColorWrite( bool bEnabled, bool bWrite );

	const EffectiveStateDX12 &Effective() const { return m_State; }

private:
	EffectiveStateDX12 m_State{};
};
} // namespace shaderapidx12

#endif // SHADER_STATE_DX12_H
