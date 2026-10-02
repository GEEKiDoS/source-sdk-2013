//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: dxsupport.cfg profile selection for the DX12 device manager
//
//=============================================================================//

#ifndef DXSUPPORT_DX12_H
#define DXSUPPORT_DX12_H
#pragma once

#include <stdint.h>

class IFileSystem;
class KeyValues;

namespace shaderapidx12
{
struct DXSupportCapsDX12
{
	int recommended = 95, max = 95;
	unsigned vendor = 0, device = 0;
	uint64_t memory = 0; // Dedicated video memory in bytes.
	bool fastClipping = false, centroidHack = false, disableShaderOptimizations = false;
};

class CDXSupportDX12
{
public:
	CDXSupportDX12() = default;
	~CDXSupportDX12();
	CDXSupportDX12( const CDXSupportDX12 & ) = delete;
	CDXSupportDX12 &operator=( const CDXSupportDX12 & ) = delete;
	// Missing files are valid. Malformed input discards all overrides and logs.
	bool Load( IFileSystem *pFileSystem );
	void Clear();
	void ReadDXSupportLevels( DXSupportCapsDX12 &caps ) const;
	void ReadHardwareCaps( DXSupportCapsDX12 &caps, int nDXLevel ) const;
	bool GetRecommendedConfigurationInfo( const DXSupportCapsDX12 &caps, int nDXLevel, KeyValues *pConfiguration ) const;
	bool GetRecommendedConfigurationInfo( const DXSupportCapsDX12 &caps, int nDXLevel, unsigned nVendor, unsigned nDevice, KeyValues *pConfiguration ) const;

private:
	KeyValues *m_pConfig = nullptr;
};
} // namespace shaderapidx12

#endif // DXSUPPORT_DX12_H
