#pragma once
#include "shaderlib/cshader.h"
class postfx_downsample_cs51_Static_Index
{
	unsigned int m_nTHRESHOLD : 2;
#ifdef _DEBUG
	bool m_bTHRESHOLD : 1;
#endif	// _DEBUG
public:
	void SetTHRESHOLD( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTHRESHOLD = i;
#ifdef _DEBUG
		m_bTHRESHOLD = true;
#endif	// _DEBUG
	}

	postfx_downsample_cs51_Static_Index(  )
	{
		m_nTHRESHOLD = 0;
#ifdef _DEBUG
		m_bTHRESHOLD = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bTHRESHOLD );
		return ( 1 * m_nTHRESHOLD ) + 0;
	}
};

#define shaderStaticTest_postfx_downsample_cs51 csh_forgot_to_set_static_THRESHOLD


class postfx_downsample_cs51_Dynamic_Index
{
public:
	postfx_downsample_cs51_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_postfx_downsample_cs51 1

