// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $APPROX_SRGB_ADAPTER && ( $CLEAR_COLOR || ( $KERNEL == 0 ) )

#pragma once
#include "shaderlib/cshader.h"
class l4d2_glow_blurfilter_ps20b_Static_Index
{
	unsigned int m_nKERNEL : 3;
	unsigned int m_nCLEAR_COLOR : 2;
	unsigned int m_nAPPROX_SRGB_ADAPTER : 2;
#ifdef _DEBUG
	bool m_bKERNEL : 1;
	bool m_bCLEAR_COLOR : 1;
	bool m_bAPPROX_SRGB_ADAPTER : 1;
#endif	// _DEBUG
public:
	void SetKERNEL( int i )
	{
		Assert( i >= 0 && i <= 4 );
		m_nKERNEL = i;
#ifdef _DEBUG
		m_bKERNEL = true;
#endif	// _DEBUG
	}

	void SetCLEAR_COLOR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCLEAR_COLOR = i;
#ifdef _DEBUG
		m_bCLEAR_COLOR = true;
#endif	// _DEBUG
	}

	void SetAPPROX_SRGB_ADAPTER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nAPPROX_SRGB_ADAPTER = i;
#ifdef _DEBUG
		m_bAPPROX_SRGB_ADAPTER = true;
#endif	// _DEBUG
	}

	l4d2_glow_blurfilter_ps20b_Static_Index(  )
	{
		m_nKERNEL = 0;
		m_nCLEAR_COLOR = 0;
		m_nAPPROX_SRGB_ADAPTER = 0;
#ifdef _DEBUG
		m_bKERNEL = false;
		m_bCLEAR_COLOR = false;
		m_bAPPROX_SRGB_ADAPTER = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bKERNEL && m_bCLEAR_COLOR && m_bAPPROX_SRGB_ADAPTER );
		AssertMsg( !( m_nAPPROX_SRGB_ADAPTER && ( m_nCLEAR_COLOR || ( m_nKERNEL == 0 ) ) ), "Invalid combo combination ( APPROX_SRGB_ADAPTER && ( CLEAR_COLOR || ( KERNEL == 0 ) ) )" );
		return ( 1 * m_nKERNEL ) + ( 5 * m_nCLEAR_COLOR ) + ( 10 * m_nAPPROX_SRGB_ADAPTER ) + 0;
	}
};

#define shaderStaticTest_l4d2_glow_blurfilter_ps20b psh_forgot_to_set_static_KERNEL + psh_forgot_to_set_static_CLEAR_COLOR + psh_forgot_to_set_static_APPROX_SRGB_ADAPTER


class l4d2_glow_blurfilter_ps20b_Dynamic_Index
{
public:
	l4d2_glow_blurfilter_ps20b_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_l4d2_glow_blurfilter_ps20b 1

