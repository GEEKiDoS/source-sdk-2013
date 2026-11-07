#pragma once
#include "shaderlib/cshader.h"
class terror_post_prepare_ps20b_Static_Index
{
	unsigned int m_nPASS : 3;
#ifdef _DEBUG
	bool m_bPASS : 1;
#endif	// _DEBUG
public:
	void SetPASS( int i )
	{
		Assert( i >= 0 && i <= 4 );
		m_nPASS = i;
#ifdef _DEBUG
		m_bPASS = true;
#endif	// _DEBUG
	}

	terror_post_prepare_ps20b_Static_Index(  )
	{
		m_nPASS = 0;
#ifdef _DEBUG
		m_bPASS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPASS );
		return ( 2 * m_nPASS ) + 0;
	}
};

#define shaderStaticTest_terror_post_prepare_ps20b psh_forgot_to_set_static_PASS


class terror_post_prepare_ps20b_Dynamic_Index
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

	terror_post_prepare_ps20b_Dynamic_Index(  )
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

#define shaderDynamicTest_terror_post_prepare_ps20b psh_forgot_to_set_dynamic_FLOAT_BACK_BUFFER

