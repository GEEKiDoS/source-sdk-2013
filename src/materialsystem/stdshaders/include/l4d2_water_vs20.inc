// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $CHEAP && ($BASETEXTURE || $FLASHLIGHT || $LIGHTMAPWATERFOG)
// $BASETEXTURE && ($MULTITEXTURE || $FLASHLIGHT || $LIGHTMAPWATERFOG)
// $MULTITEXTURE && ($FLASHLIGHT || $LIGHTMAPWATERFOG)
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class l4d2_water_vs20_Static_Index
{
	unsigned int m_nCHEAP : 2;
	unsigned int m_nBASETEXTURE : 2;
	unsigned int m_nMULTITEXTURE : 2;
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nLIGHTMAPWATERFOG : 2;
#ifdef _DEBUG
	bool m_bCHEAP : 1;
	bool m_bBASETEXTURE : 1;
	bool m_bMULTITEXTURE : 1;
	bool m_bFLASHLIGHT : 1;
	bool m_bLIGHTMAPWATERFOG : 1;
#endif	// _DEBUG
public:
	void SetCHEAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCHEAP = i;
#ifdef _DEBUG
		m_bCHEAP = true;
#endif	// _DEBUG
	}

	void SetBASETEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASETEXTURE = i;
#ifdef _DEBUG
		m_bBASETEXTURE = true;
#endif	// _DEBUG
	}

	void SetMULTITEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMULTITEXTURE = i;
#ifdef _DEBUG
		m_bMULTITEXTURE = true;
#endif	// _DEBUG
	}

	void SetFLASHLIGHT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHT = i;
#ifdef _DEBUG
		m_bFLASHLIGHT = true;
#endif	// _DEBUG
	}

	void SetLIGHTMAPWATERFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLIGHTMAPWATERFOG = i;
#ifdef _DEBUG
		m_bLIGHTMAPWATERFOG = true;
#endif	// _DEBUG
	}

	l4d2_water_vs20_Static_Index(  )
	{
		m_nCHEAP = 0;
		m_nBASETEXTURE = 0;
		m_nMULTITEXTURE = 0;
		m_nFLASHLIGHT = 0;
		m_nLIGHTMAPWATERFOG = 0;
#ifdef _DEBUG
		m_bCHEAP = false;
		m_bBASETEXTURE = false;
		m_bMULTITEXTURE = false;
		m_bFLASHLIGHT = false;
		m_bLIGHTMAPWATERFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCHEAP && m_bBASETEXTURE && m_bMULTITEXTURE && m_bFLASHLIGHT && m_bLIGHTMAPWATERFOG );
		AssertMsg( !( m_nCHEAP && ( m_nBASETEXTURE || ( m_nFLASHLIGHT || m_nLIGHTMAPWATERFOG ) ) ), "Invalid combo combination ( CHEAP && ( BASETEXTURE || ( FLASHLIGHT || LIGHTMAPWATERFOG ) ) )" );
		AssertMsg( !( m_nBASETEXTURE && ( m_nMULTITEXTURE || ( m_nFLASHLIGHT || m_nLIGHTMAPWATERFOG ) ) ), "Invalid combo combination ( BASETEXTURE && ( MULTITEXTURE || ( FLASHLIGHT || LIGHTMAPWATERFOG ) ) )" );
		AssertMsg( !( m_nMULTITEXTURE && ( m_nFLASHLIGHT || m_nLIGHTMAPWATERFOG ) ), "Invalid combo combination ( MULTITEXTURE && ( FLASHLIGHT || LIGHTMAPWATERFOG ) )" );
		return ( 1 * m_nCHEAP ) + ( 2 * m_nBASETEXTURE ) + ( 4 * m_nMULTITEXTURE ) + ( 8 * m_nFLASHLIGHT ) + ( 16 * m_nLIGHTMAPWATERFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_water_vs20 vsh_forgot_to_set_static_CHEAP + vsh_forgot_to_set_static_BASETEXTURE + vsh_forgot_to_set_static_MULTITEXTURE + vsh_forgot_to_set_static_FLASHLIGHT + vsh_forgot_to_set_static_LIGHTMAPWATERFOG


class l4d2_water_vs20_Dynamic_Index
{
public:
	l4d2_water_vs20_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_l4d2_water_vs20 1

