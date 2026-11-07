// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class l4d2_black_vs20_Static_Index
{
	unsigned int m_nHARDWAREFOGBLEND : 2;
#ifdef _DEBUG
	bool m_bHARDWAREFOGBLEND : 1;
#endif	// _DEBUG
public:
	void SetHARDWAREFOGBLEND( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHARDWAREFOGBLEND = i;
#ifdef _DEBUG
		m_bHARDWAREFOGBLEND = true;
#endif	// _DEBUG
	}

	l4d2_black_vs20_Static_Index(  )
	{
		m_nHARDWAREFOGBLEND = 0;
#ifdef _DEBUG
		m_bHARDWAREFOGBLEND = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bHARDWAREFOGBLEND );
		return ( 4 * m_nHARDWAREFOGBLEND ) + 0;
	}
};

#define shaderStaticTest_l4d2_black_vs20 vsh_forgot_to_set_static_HARDWAREFOGBLEND


class l4d2_black_vs20_Dynamic_Index
{
	unsigned int m_nSKINNING : 2;
	unsigned int m_nSCENEFOG : 2;
#ifdef _DEBUG
	bool m_bSKINNING : 1;
	bool m_bSCENEFOG : 1;
#endif	// _DEBUG
public:
	void SetSKINNING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSKINNING = i;
#ifdef _DEBUG
		m_bSKINNING = true;
#endif	// _DEBUG
	}

	void SetSCENEFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSCENEFOG = i;
#ifdef _DEBUG
		m_bSCENEFOG = true;
#endif	// _DEBUG
	}

	l4d2_black_vs20_Dynamic_Index(  )
	{
		m_nSKINNING = 0;
		m_nSCENEFOG = 0;
#ifdef _DEBUG
		m_bSKINNING = false;
		m_bSCENEFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSKINNING && m_bSCENEFOG );
		return ( 1 * m_nSKINNING ) + ( 2 * m_nSCENEFOG ) + 0;
	}
};

#define shaderDynamicTest_l4d2_black_vs20 vsh_forgot_to_set_dynamic_SKINNING + vsh_forgot_to_set_dynamic_SCENEFOG

