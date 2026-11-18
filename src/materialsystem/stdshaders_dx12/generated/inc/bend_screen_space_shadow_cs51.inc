#pragma once
#include "shaderlib/cshader.h"
class bend_screen_space_shadow_cs51_Static_Index
{
public:
	bend_screen_space_shadow_cs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_bend_screen_space_shadow_cs51 1


class bend_screen_space_shadow_cs51_Dynamic_Index
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

	bend_screen_space_shadow_cs51_Dynamic_Index(  )
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

#define shaderDynamicTest_bend_screen_space_shadow_cs51 csh_forgot_to_set_dynamic_MSAA

