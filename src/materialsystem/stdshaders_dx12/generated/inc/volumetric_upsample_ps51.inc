#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock volumetric_upsample_ps51_Block;
class volumetric_upsample_ps51_Static_Index
{
	unsigned int m_nMSAA : 2;
#ifdef _DEBUG
	bool m_bMSAA : 1;
#endif	// _DEBUG
public:
	void SetMSAA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMSAA = i;
#ifdef _DEBUG
		m_bMSAA = true;
#endif	// _DEBUG
	}

	volumetric_upsample_ps51_Static_Index(  )
	{
		m_nMSAA = 0;
#ifdef _DEBUG
		m_bMSAA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bMSAA );
		return ( 1 * m_nMSAA ) + 0;
	}
};

#define shaderStaticTest_volumetric_upsample_ps51 psh_forgot_to_set_static_MSAA


class volumetric_upsample_ps51_Dynamic_Index
{
public:
	volumetric_upsample_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< volumetric_upsample_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_volumetric_upsample_ps51 1

