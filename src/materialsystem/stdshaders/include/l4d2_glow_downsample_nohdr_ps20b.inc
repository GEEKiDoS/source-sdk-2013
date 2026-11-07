#pragma once
#include "shaderlib/cshader.h"
class l4d2_glow_downsample_nohdr_ps20b_Static_Index
{
	unsigned int m_nBLOOMTYPE : 2;
	unsigned int m_nSRGB_ADAPTER : 2;
#ifdef _DEBUG
	bool m_bBLOOMTYPE : 1;
	bool m_bSRGB_ADAPTER : 1;
#endif	// _DEBUG
public:
	void SetBLOOMTYPE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBLOOMTYPE = i;
#ifdef _DEBUG
		m_bBLOOMTYPE = true;
#endif	// _DEBUG
	}

	void SetSRGB_ADAPTER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSRGB_ADAPTER = i;
#ifdef _DEBUG
		m_bSRGB_ADAPTER = true;
#endif	// _DEBUG
	}

	l4d2_glow_downsample_nohdr_ps20b_Static_Index(  )
	{
		m_nBLOOMTYPE = 0;
		m_nSRGB_ADAPTER = 0;
#ifdef _DEBUG
		m_bBLOOMTYPE = false;
		m_bSRGB_ADAPTER = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bBLOOMTYPE && m_bSRGB_ADAPTER );
		return ( 2 * m_nBLOOMTYPE ) + ( 4 * m_nSRGB_ADAPTER ) + 0;
	}
};

#define shaderStaticTest_l4d2_glow_downsample_nohdr_ps20b psh_forgot_to_set_static_BLOOMTYPE + psh_forgot_to_set_static_SRGB_ADAPTER


class l4d2_glow_downsample_nohdr_ps20b_Dynamic_Index
{
	unsigned int m_nFLOAT_BACK_BUFFER : 2;
#ifdef _DEBUG
	bool m_bFLOAT_BACK_BUFFER : 1;
#endif	// _DEBUG
public:
	void SetFLOAT_BACK_BUFFER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLOAT_BACK_BUFFER = i;
#ifdef _DEBUG
		m_bFLOAT_BACK_BUFFER = true;
#endif	// _DEBUG
	}

	l4d2_glow_downsample_nohdr_ps20b_Dynamic_Index(  )
	{
		m_nFLOAT_BACK_BUFFER = 0;
#ifdef _DEBUG
		m_bFLOAT_BACK_BUFFER = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFLOAT_BACK_BUFFER );
		return ( 1 * m_nFLOAT_BACK_BUFFER ) + 0;
	}
};

#define shaderDynamicTest_l4d2_glow_downsample_nohdr_ps20b psh_forgot_to_set_dynamic_FLOAT_BACK_BUFFER

