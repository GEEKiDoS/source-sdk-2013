#pragma once
#include "shaderlib/cshader.h"
class postfx_glare_cs51_Static_Index
{
	unsigned int m_nREDUCE : 2;
#ifdef _DEBUG
	bool m_bREDUCE : 1;
#endif	// _DEBUG
public:
	void SetREDUCE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nREDUCE = i;
#ifdef _DEBUG
		m_bREDUCE = true;
#endif	// _DEBUG
	}

	postfx_glare_cs51_Static_Index(  )
	{
		m_nREDUCE = 0;
#ifdef _DEBUG
		m_bREDUCE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bREDUCE );
		return ( 1 * m_nREDUCE ) + 0;
	}
};

#define shaderStaticTest_postfx_glare_cs51 csh_forgot_to_set_static_REDUCE


class postfx_glare_cs51_Dynamic_Index
{
public:
	postfx_glare_cs51_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_postfx_glare_cs51 1

