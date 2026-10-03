#pragma once
#include "shaderlib/cshader.h"
class gtao_prefilter_cs51_Static_Index
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

	gtao_prefilter_cs51_Static_Index(  )
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

#define shaderStaticTest_gtao_prefilter_cs51 csh_forgot_to_set_static_MSAA


class gtao_prefilter_cs51_Dynamic_Index
{
public:
	gtao_prefilter_cs51_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_gtao_prefilter_cs51 1

