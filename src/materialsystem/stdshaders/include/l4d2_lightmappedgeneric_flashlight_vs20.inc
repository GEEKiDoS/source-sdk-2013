// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $SEAMLESS && $DETAIL
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class l4d2_lightmappedgeneric_flashlight_vs20_Static_Index
{
	unsigned int m_nNORMALMAP : 2;
	unsigned int m_nWORLDVERTEXTRANSITION : 2;
	unsigned int m_nSEAMLESS : 2;
	unsigned int m_nDETAIL : 2;
	unsigned int m_nPIXELFOG : 2;
#ifdef _DEBUG
	bool m_bNORMALMAP : 1;
	bool m_bWORLDVERTEXTRANSITION : 1;
	bool m_bSEAMLESS : 1;
	bool m_bDETAIL : 1;
	bool m_bPIXELFOG : 1;
#endif	// _DEBUG
public:
	void SetNORMALMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nNORMALMAP = i;
#ifdef _DEBUG
		m_bNORMALMAP = true;
#endif	// _DEBUG
	}

	void SetWORLDVERTEXTRANSITION( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWORLDVERTEXTRANSITION = i;
#ifdef _DEBUG
		m_bWORLDVERTEXTRANSITION = true;
#endif	// _DEBUG
	}

	void SetSEAMLESS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEAMLESS = i;
#ifdef _DEBUG
		m_bSEAMLESS = true;
#endif	// _DEBUG
	}

	void SetDETAIL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDETAIL = i;
#ifdef _DEBUG
		m_bDETAIL = true;
#endif	// _DEBUG
	}

	void SetPIXELFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPIXELFOG = i;
#ifdef _DEBUG
		m_bPIXELFOG = true;
#endif	// _DEBUG
	}

	l4d2_lightmappedgeneric_flashlight_vs20_Static_Index(  )
	{
		m_nNORMALMAP = 0;
		m_nWORLDVERTEXTRANSITION = 0;
		m_nSEAMLESS = 0;
		m_nDETAIL = 0;
		m_nPIXELFOG = 0;
#ifdef _DEBUG
		m_bNORMALMAP = false;
		m_bWORLDVERTEXTRANSITION = false;
		m_bSEAMLESS = false;
		m_bDETAIL = false;
		m_bPIXELFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bNORMALMAP && m_bWORLDVERTEXTRANSITION && m_bSEAMLESS && m_bDETAIL && m_bPIXELFOG );
		AssertMsg( !( m_nSEAMLESS && m_nDETAIL ), "Invalid combo combination ( SEAMLESS && DETAIL )" );
		return ( 1 * m_nNORMALMAP ) + ( 2 * m_nWORLDVERTEXTRANSITION ) + ( 4 * m_nSEAMLESS ) + ( 8 * m_nDETAIL ) + ( 16 * m_nPIXELFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_lightmappedgeneric_flashlight_vs20 vsh_forgot_to_set_static_NORMALMAP + vsh_forgot_to_set_static_WORLDVERTEXTRANSITION + vsh_forgot_to_set_static_SEAMLESS + vsh_forgot_to_set_static_DETAIL + vsh_forgot_to_set_static_PIXELFOG


class l4d2_lightmappedgeneric_flashlight_vs20_Dynamic_Index
{
public:
	l4d2_lightmappedgeneric_flashlight_vs20_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_l4d2_lightmappedgeneric_flashlight_vs20 1

