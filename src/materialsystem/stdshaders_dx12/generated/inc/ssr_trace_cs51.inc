#pragma once
#include "shaderlib/cshader.h"
class ssr_trace_cs51_Static_Index
{
	unsigned int m_nRAY_RECONSTRUCTION : 2;
#ifdef _DEBUG
	bool m_bRAY_RECONSTRUCTION : 1;
#endif	// _DEBUG
public:
	void SetRAY_RECONSTRUCTION( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nRAY_RECONSTRUCTION = i;
#ifdef _DEBUG
		m_bRAY_RECONSTRUCTION = true;
#endif	// _DEBUG
	}

	ssr_trace_cs51_Static_Index(  )
	{
		m_nRAY_RECONSTRUCTION = 0;
#ifdef _DEBUG
		m_bRAY_RECONSTRUCTION = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bRAY_RECONSTRUCTION );
		return ( 1 * m_nRAY_RECONSTRUCTION ) + 0;
	}
};

#define shaderStaticTest_ssr_trace_cs51 csh_forgot_to_set_static_RAY_RECONSTRUCTION


class ssr_trace_cs51_Dynamic_Index
{
public:
	ssr_trace_cs51_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_ssr_trace_cs51 1

