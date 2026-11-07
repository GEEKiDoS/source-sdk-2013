// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $BLURRED_VIGNETTE_ENABLE && !$LOCAL_CONTRAST_ENABLE

#pragma once
#include "shaderlib/cshader.h"
class terror_engine_post_ps20b_Static_Index
{
	unsigned int m_nCONVERT_FROM_LINEAR : 2;
#ifdef _DEBUG
	bool m_bCONVERT_FROM_LINEAR : 1;
#endif	// _DEBUG
public:
	void SetCONVERT_FROM_LINEAR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_FROM_LINEAR = i;
#ifdef _DEBUG
		m_bCONVERT_FROM_LINEAR = true;
#endif	// _DEBUG
	}

	terror_engine_post_ps20b_Static_Index(  )
	{
		m_nCONVERT_FROM_LINEAR = 0;
#ifdef _DEBUG
		m_bCONVERT_FROM_LINEAR = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCONVERT_FROM_LINEAR );
		return ( 128 * m_nCONVERT_FROM_LINEAR ) + 0;
	}
};

#define shaderStaticTest_terror_engine_post_ps20b psh_forgot_to_set_static_CONVERT_FROM_LINEAR


class terror_engine_post_ps20b_Dynamic_Index
{
	unsigned int m_nCOL_CORRECT_NUM_LOOKUPS : 3;
	unsigned int m_nNOISE_ENABLE : 2;
	unsigned int m_nVIGNETTE_ENABLE : 2;
	unsigned int m_nLOCAL_CONTRAST_ENABLE : 2;
	unsigned int m_nBLURRED_VIGNETTE_ENABLE : 2;
	unsigned int m_nVOMIT_ENABLE : 2;
#ifdef _DEBUG
	bool m_bCOL_CORRECT_NUM_LOOKUPS : 1;
	bool m_bNOISE_ENABLE : 1;
	bool m_bVIGNETTE_ENABLE : 1;
	bool m_bLOCAL_CONTRAST_ENABLE : 1;
	bool m_bBLURRED_VIGNETTE_ENABLE : 1;
	bool m_bVOMIT_ENABLE : 1;
#endif	// _DEBUG
public:
	void SetCOL_CORRECT_NUM_LOOKUPS( int i )
	{
		Assert( i >= 0 && i <= 3 );
		m_nCOL_CORRECT_NUM_LOOKUPS = i;
#ifdef _DEBUG
		m_bCOL_CORRECT_NUM_LOOKUPS = true;
#endif	// _DEBUG
	}

	void SetNOISE_ENABLE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nNOISE_ENABLE = i;
#ifdef _DEBUG
		m_bNOISE_ENABLE = true;
#endif	// _DEBUG
	}

	void SetVIGNETTE_ENABLE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVIGNETTE_ENABLE = i;
#ifdef _DEBUG
		m_bVIGNETTE_ENABLE = true;
#endif	// _DEBUG
	}

	void SetLOCAL_CONTRAST_ENABLE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLOCAL_CONTRAST_ENABLE = i;
#ifdef _DEBUG
		m_bLOCAL_CONTRAST_ENABLE = true;
#endif	// _DEBUG
	}

	void SetBLURRED_VIGNETTE_ENABLE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBLURRED_VIGNETTE_ENABLE = i;
#ifdef _DEBUG
		m_bBLURRED_VIGNETTE_ENABLE = true;
#endif	// _DEBUG
	}

	void SetVOMIT_ENABLE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVOMIT_ENABLE = i;
#ifdef _DEBUG
		m_bVOMIT_ENABLE = true;
#endif	// _DEBUG
	}

	terror_engine_post_ps20b_Dynamic_Index(  )
	{
		m_nCOL_CORRECT_NUM_LOOKUPS = 0;
		m_nNOISE_ENABLE = 0;
		m_nVIGNETTE_ENABLE = 0;
		m_nLOCAL_CONTRAST_ENABLE = 0;
		m_nBLURRED_VIGNETTE_ENABLE = 0;
		m_nVOMIT_ENABLE = 0;
#ifdef _DEBUG
		m_bCOL_CORRECT_NUM_LOOKUPS = false;
		m_bNOISE_ENABLE = false;
		m_bVIGNETTE_ENABLE = false;
		m_bLOCAL_CONTRAST_ENABLE = false;
		m_bBLURRED_VIGNETTE_ENABLE = false;
		m_bVOMIT_ENABLE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCOL_CORRECT_NUM_LOOKUPS && m_bNOISE_ENABLE && m_bVIGNETTE_ENABLE && m_bLOCAL_CONTRAST_ENABLE && m_bBLURRED_VIGNETTE_ENABLE && m_bVOMIT_ENABLE );
		AssertMsg( !( m_nBLURRED_VIGNETTE_ENABLE && !m_nLOCAL_CONTRAST_ENABLE ), "Invalid combo combination ( BLURRED_VIGNETTE_ENABLE && !LOCAL_CONTRAST_ENABLE )" );
		return ( 1 * m_nCOL_CORRECT_NUM_LOOKUPS ) + ( 4 * m_nNOISE_ENABLE ) + ( 8 * m_nVIGNETTE_ENABLE ) + ( 16 * m_nLOCAL_CONTRAST_ENABLE ) + ( 32 * m_nBLURRED_VIGNETTE_ENABLE ) + ( 64 * m_nVOMIT_ENABLE ) + 0;
	}
};

#define shaderDynamicTest_terror_engine_post_ps20b psh_forgot_to_set_dynamic_COL_CORRECT_NUM_LOOKUPS + psh_forgot_to_set_dynamic_NOISE_ENABLE + psh_forgot_to_set_dynamic_VIGNETTE_ENABLE + psh_forgot_to_set_dynamic_LOCAL_CONTRAST_ENABLE + psh_forgot_to_set_dynamic_BLURRED_VIGNETTE_ENABLE + psh_forgot_to_set_dynamic_VOMIT_ENABLE

