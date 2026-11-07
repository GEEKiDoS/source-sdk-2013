#pragma once
#include "shaderlib/cshader.h"
class terror_motion_blur_ps20b_Static_Index
{
public:
	terror_motion_blur_ps20b_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_terror_motion_blur_ps20b 1


class terror_motion_blur_ps20b_Dynamic_Index
{
	unsigned int m_nQUALITY : 4;
#ifdef _DEBUG
	bool m_bQUALITY : 1;
#endif	// _DEBUG
public:
	void SetQUALITY( int i )
	{
		Assert( i >= 0 && i <= 14 );
		m_nQUALITY = i;
#ifdef _DEBUG
		m_bQUALITY = true;
#endif	// _DEBUG
	}

	terror_motion_blur_ps20b_Dynamic_Index(  )
	{
		m_nQUALITY = 0;
#ifdef _DEBUG
		m_bQUALITY = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bQUALITY );
		return ( 1 * m_nQUALITY ) + 0;
	}
};

#define shaderDynamicTest_terror_motion_blur_ps20b psh_forgot_to_set_dynamic_QUALITY

