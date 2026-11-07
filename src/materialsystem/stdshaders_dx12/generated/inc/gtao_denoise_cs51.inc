#pragma once
#include "shaderlib/cshader.h"
class gtao_denoise_cs51_Static_Index
{
	unsigned int m_nFINAL : 2;
#ifdef _DEBUG
	bool m_bFINAL : 1;
#endif	// _DEBUG
public:
	void SetFINAL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFINAL = i;
#ifdef _DEBUG
		m_bFINAL = true;
#endif	// _DEBUG
	}

	gtao_denoise_cs51_Static_Index(  )
	{
		m_nFINAL = 0;
#ifdef _DEBUG
		m_bFINAL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFINAL );
		return ( 1 * m_nFINAL ) + 0;
	}
};

#define shaderStaticTest_gtao_denoise_cs51 csh_forgot_to_set_static_FINAL


class gtao_denoise_cs51_Dynamic_Index
{
public:
	gtao_denoise_cs51_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_gtao_denoise_cs51 1

