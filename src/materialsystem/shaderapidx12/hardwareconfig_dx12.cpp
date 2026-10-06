//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 implementation of IMaterialSystemHardwareConfig
//
//=============================================================================//

#include "hardwareconfig_dx12.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include <climits>

namespace shaderapidx12
{

CHardwareConfigDX12 *g_pHardwareConfigDX12 = nullptr;

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CHardwareConfigDX12::CHardwareConfigDX12()
{
	memset( &m_Adapter, 0, sizeof( m_Adapter ) );
	V_strncpy( m_Adapter.m_pDriverName, "Native DirectX 12", sizeof( m_Adapter.m_pDriverName ) );
	m_Adapter.m_nDXSupportLevel = kSourceDXLevel;
	m_Adapter.m_nMaxDXSupportLevel = kSourceDXLevel;
}

//-----------------------------------------------------------------------------
// Purpose: Stores the adapter description chosen by the device manager
//-----------------------------------------------------------------------------
void CHardwareConfigDX12::SetAdapter( const MaterialAdapterInfo_t &adapter, uint64_t nDedicatedVideoMemory, bool bAaEnabled )
{
	m_Adapter = adapter;
	m_nDedicatedVideoMemory = nDedicatedVideoMemory;
	m_bAaEnabled = bAaEnabled;
	m_Adapter.m_nDXSupportLevel = kSourceDXLevel;
	m_Adapter.m_nMaxDXSupportLevel = kSourceDXLevel;
	// Source selects float HDR only for the explicit mat_hdr_level 3 mode.
	ConVarRef hdrLevel( "mat_hdr_level", true );
	m_HdrType = hdrLevel.IsValid() && hdrLevel.GetInt() == 3 ? HDR_TYPE_FLOAT : HDR_TYPE_INTEGER;
}

//-----------------------------------------------------------------------------
// Purpose: Stores the dxsupport.cfg capability overrides
//-----------------------------------------------------------------------------
void CHardwareConfigDX12::SetSupportCaps( bool bFastClipping, bool bCentroidHack, bool bDisableShaderOptimizations )
{
	m_bFastClipping = bFastClipping;
	m_bCentroidHack = bCentroidHack;
	m_bDisableShaderOptimizations = bDisableShaderOptimizations;
}

//-----------------------------------------------------------------------------
// Purpose: Dedicated video memory in bytes, saturated to INT_MAX
//-----------------------------------------------------------------------------
int CHardwareConfigDX12::TextureMemorySize() const
{
	return m_nDedicatedVideoMemory > static_cast<uint64_t>( INT_MAX ) ? INT_MAX : static_cast<int>( m_nDedicatedVideoMemory );
}

} // namespace shaderapidx12
