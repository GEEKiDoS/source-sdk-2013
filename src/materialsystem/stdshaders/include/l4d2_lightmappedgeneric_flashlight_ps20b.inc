// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// !$NORMALMAP && $NORMALMAP2
// !$WORLDVERTEXTRANSITION && $NORMALMAP2
// $SEAMLESS && $DETAIL
// !$DETAIL && $DETAIL_BLEND_MODE
// $DETAIL_BLEND_MODE == 1 || $DETAIL_BLEND_MODE == 4 || $DETAIL_BLEND_MODE == 6
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class l4d2_lightmappedgeneric_flashlight_ps20b_Static_Index
{
	unsigned int m_nNORMALMAP : 2;
	unsigned int m_nNORMALMAP2 : 2;
	unsigned int m_nWORLDVERTEXTRANSITION : 2;
	unsigned int m_nSEAMLESS : 2;
	unsigned int m_nDETAIL : 2;
	unsigned int m_nDETAIL_BLEND_MODE : 4;
	unsigned int m_nFLASHLIGHTDEPTHFILTERMODE : 2;
	unsigned int m_nBLENDTINTBYBASEALPHA : 2;
	unsigned int m_nPIXELFOG : 2;
#ifdef _DEBUG
	bool m_bNORMALMAP : 1;
	bool m_bNORMALMAP2 : 1;
	bool m_bWORLDVERTEXTRANSITION : 1;
	bool m_bSEAMLESS : 1;
	bool m_bDETAIL : 1;
	bool m_bDETAIL_BLEND_MODE : 1;
	bool m_bFLASHLIGHTDEPTHFILTERMODE : 1;
	bool m_bBLENDTINTBYBASEALPHA : 1;
	bool m_bPIXELFOG : 1;
#endif	// _DEBUG
public:
	void SetNORMALMAP( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nNORMALMAP = i;
#ifdef _DEBUG
		m_bNORMALMAP = true;
#endif	// _DEBUG
	}

	void SetNORMALMAP2( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nNORMALMAP2 = i;
#ifdef _DEBUG
		m_bNORMALMAP2 = true;
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

	void SetDETAIL_BLEND_MODE( int i )
	{
		Assert( i >= 0 && i <= 11 );
		m_nDETAIL_BLEND_MODE = i;
#ifdef _DEBUG
		m_bDETAIL_BLEND_MODE = true;
#endif	// _DEBUG
	}

	void SetFLASHLIGHTDEPTHFILTERMODE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nFLASHLIGHTDEPTHFILTERMODE = i;
#ifdef _DEBUG
		m_bFLASHLIGHTDEPTHFILTERMODE = true;
#endif	// _DEBUG
	}

	void SetBLENDTINTBYBASEALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBLENDTINTBYBASEALPHA = i;
#ifdef _DEBUG
		m_bBLENDTINTBYBASEALPHA = true;
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

	l4d2_lightmappedgeneric_flashlight_ps20b_Static_Index(  )
	{
		m_nNORMALMAP = 0;
		m_nNORMALMAP2 = 0;
		m_nWORLDVERTEXTRANSITION = 0;
		m_nSEAMLESS = 0;
		m_nDETAIL = 0;
		m_nDETAIL_BLEND_MODE = 0;
		m_nFLASHLIGHTDEPTHFILTERMODE = 0;
		m_nBLENDTINTBYBASEALPHA = 0;
		m_nPIXELFOG = 0;
#ifdef _DEBUG
		m_bNORMALMAP = false;
		m_bNORMALMAP2 = false;
		m_bWORLDVERTEXTRANSITION = false;
		m_bSEAMLESS = false;
		m_bDETAIL = false;
		m_bDETAIL_BLEND_MODE = false;
		m_bFLASHLIGHTDEPTHFILTERMODE = false;
		m_bBLENDTINTBYBASEALPHA = false;
		m_bPIXELFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bNORMALMAP && m_bNORMALMAP2 && m_bWORLDVERTEXTRANSITION && m_bSEAMLESS && m_bDETAIL && m_bDETAIL_BLEND_MODE && m_bFLASHLIGHTDEPTHFILTERMODE && m_bBLENDTINTBYBASEALPHA && m_bPIXELFOG );
		AssertMsg( !( !m_nNORMALMAP && m_nNORMALMAP2 ), "Invalid combo combination ( !NORMALMAP && NORMALMAP2 )" );
		AssertMsg( !( !m_nWORLDVERTEXTRANSITION && m_nNORMALMAP2 ), "Invalid combo combination ( !WORLDVERTEXTRANSITION && NORMALMAP2 )" );
		AssertMsg( !( m_nSEAMLESS && m_nDETAIL ), "Invalid combo combination ( SEAMLESS && DETAIL )" );
		AssertMsg( !( !m_nDETAIL && m_nDETAIL_BLEND_MODE ), "Invalid combo combination ( !DETAIL && DETAIL_BLEND_MODE )" );
		AssertMsg( !( ( m_nDETAIL_BLEND_MODE == 1 ) || ( ( m_nDETAIL_BLEND_MODE == 4 ) || ( m_nDETAIL_BLEND_MODE == 6 ) ) ), "Invalid combo combination ( ( DETAIL_BLEND_MODE == 1 ) || ( ( DETAIL_BLEND_MODE == 4 ) || ( DETAIL_BLEND_MODE == 6 ) ) )" );
		return ( 2 * m_nNORMALMAP ) + ( 6 * m_nNORMALMAP2 ) + ( 12 * m_nWORLDVERTEXTRANSITION ) + ( 24 * m_nSEAMLESS ) + ( 48 * m_nDETAIL ) + ( 96 * m_nDETAIL_BLEND_MODE ) + ( 1152 * m_nFLASHLIGHTDEPTHFILTERMODE ) + ( 3456 * m_nBLENDTINTBYBASEALPHA ) + ( 6912 * m_nPIXELFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_lightmappedgeneric_flashlight_ps20b psh_forgot_to_set_static_NORMALMAP + psh_forgot_to_set_static_NORMALMAP2 + psh_forgot_to_set_static_WORLDVERTEXTRANSITION + psh_forgot_to_set_static_SEAMLESS + psh_forgot_to_set_static_DETAIL + psh_forgot_to_set_static_DETAIL_BLEND_MODE + psh_forgot_to_set_static_FLASHLIGHTDEPTHFILTERMODE + psh_forgot_to_set_static_BLENDTINTBYBASEALPHA + psh_forgot_to_set_static_PIXELFOG


class l4d2_lightmappedgeneric_flashlight_ps20b_Dynamic_Index
{
	unsigned int m_nFLASHLIGHTSHADOWS : 2;
#ifdef _DEBUG
	bool m_bFLASHLIGHTSHADOWS : 1;
#endif	// _DEBUG
public:
	void SetFLASHLIGHTSHADOWS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHTSHADOWS = i;
#ifdef _DEBUG
		m_bFLASHLIGHTSHADOWS = true;
#endif	// _DEBUG
	}

	l4d2_lightmappedgeneric_flashlight_ps20b_Dynamic_Index(  )
	{
		m_nFLASHLIGHTSHADOWS = 0;
#ifdef _DEBUG
		m_bFLASHLIGHTSHADOWS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFLASHLIGHTSHADOWS );
		return ( 1 * m_nFLASHLIGHTSHADOWS ) + 0;
	}
};

#define shaderDynamicTest_l4d2_lightmappedgeneric_flashlight_ps20b psh_forgot_to_set_dynamic_FLASHLIGHTSHADOWS

