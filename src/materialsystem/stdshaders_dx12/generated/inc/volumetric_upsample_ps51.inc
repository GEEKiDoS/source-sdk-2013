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
		return ( 2 * m_nMSAA ) + 0;
	}
};

#define shaderStaticTest_volumetric_upsample_ps51 psh_forgot_to_set_static_MSAA


class volumetric_upsample_ps51_Dynamic_Index
{
	unsigned int m_nQUARTER : 2;
#ifdef _DEBUG
	bool m_bQUARTER : 1;
#endif	// _DEBUG
public:
	void SetQUARTER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nQUARTER = i;
#ifdef _DEBUG
		m_bQUARTER = true;
#endif	// _DEBUG
	}

	volumetric_upsample_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< volumetric_upsample_ps51_Block >( dx12native::kStagePixel );
		m_nQUARTER = 0;
#ifdef _DEBUG
		m_bQUARTER = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bQUARTER );
		return ( 1 * m_nQUARTER ) + 0;
	}
};

#define shaderDynamicTest_volumetric_upsample_ps51 psh_forgot_to_set_dynamic_QUARTER

