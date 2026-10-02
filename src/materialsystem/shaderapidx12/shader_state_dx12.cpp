//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Effective depth/blend/alpha state derived from a shadow snapshot plus overrides.
//
//=============================================================================//

#include "shader_state_dx12.h"

namespace shaderapidx12
{
//-----------------------------------------------------------------------------
// Purpose: Restores the default effective state
//-----------------------------------------------------------------------------
void CShaderStateDX12::Reset()
{
	m_State = EffectiveStateDX12{};
}

//-----------------------------------------------------------------------------
// Purpose: Dynamic depth test/write override
//-----------------------------------------------------------------------------
void CShaderStateDX12::OverrideDepth( bool bEnabled, bool bWrite )
{
	m_State.depthEnable = bEnabled;
	m_State.depthWrite = bWrite;
}

//-----------------------------------------------------------------------------
// Purpose: Takes depth/blend/alpha-test state from a shadow snapshot
//-----------------------------------------------------------------------------
void CShaderStateDX12::ApplyShadow( const CShaderShadowDX12 &shadow )
{
	m_State.depthEnable = shadow.DepthTest();
	m_State.depthWrite = shadow.DepthWrites();
	m_State.blendEnable = shadow.Blending();
	m_State.alphaTest = shadow.AlphaTest();
}

//-----------------------------------------------------------------------------
// Purpose: Dynamic color-write override; ignored unless enabled
//-----------------------------------------------------------------------------
void CShaderStateDX12::OverrideColorWrite( bool bEnabled, bool bWrite )
{
	if ( bEnabled )
		m_State.colorWrite = bWrite;
}
} // namespace shaderapidx12
