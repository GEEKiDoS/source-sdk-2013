// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
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
class l4d2_unlittwotexture_ps20b_Static_Index
{
	unsigned int m_nTRANSLUCENT : 2;
	unsigned int m_nCONVERT_TO_SRGB : 2;
#ifdef _DEBUG
	bool m_bTRANSLUCENT : 1;
	bool m_bCONVERT_TO_SRGB : 1;
#endif	// _DEBUG
public:
	void SetTRANSLUCENT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTRANSLUCENT = i;
#ifdef _DEBUG
		m_bTRANSLUCENT = true;
#endif	// _DEBUG
	}

	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
#ifdef _DEBUG
		m_bCONVERT_TO_SRGB = true;
#endif	// _DEBUG
	}

	l4d2_unlittwotexture_ps20b_Static_Index(  )
	{
		m_nTRANSLUCENT = 0;
		m_nCONVERT_TO_SRGB = 0;
#ifdef _DEBUG
		m_bTRANSLUCENT = false;
		m_bCONVERT_TO_SRGB = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bTRANSLUCENT && m_bCONVERT_TO_SRGB );
		return ( 12 * m_nTRANSLUCENT ) + ( 24 * m_nCONVERT_TO_SRGB ) + 0;
	}
};

#define shaderStaticTest_l4d2_unlittwotexture_ps20b psh_forgot_to_set_static_TRANSLUCENT + psh_forgot_to_set_static_CONVERT_TO_SRGB


class l4d2_unlittwotexture_ps20b_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
	unsigned int m_nLIGHTING_PREVIEW : 2;
	unsigned int m_nWRITE_DEPTH_TO_DESTALPHA : 2;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
	bool m_bLIGHTING_PREVIEW : 1;
	bool m_bWRITE_DEPTH_TO_DESTALPHA : 1;
#endif	// _DEBUG
public:
	void SetPIXELFOGTYPE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPIXELFOGTYPE = i;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = true;
#endif	// _DEBUG
	}

	void SetLIGHTING_PREVIEW( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nLIGHTING_PREVIEW = i;
#ifdef _DEBUG
		m_bLIGHTING_PREVIEW = true;
#endif	// _DEBUG
	}

	void SetWRITE_DEPTH_TO_DESTALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWRITE_DEPTH_TO_DESTALPHA = i;
#ifdef _DEBUG
		m_bWRITE_DEPTH_TO_DESTALPHA = true;
#endif	// _DEBUG
	}

	l4d2_unlittwotexture_ps20b_Dynamic_Index(  )
	{
		m_nPIXELFOGTYPE = 0;
		m_nLIGHTING_PREVIEW = 0;
		m_nWRITE_DEPTH_TO_DESTALPHA = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
		m_bLIGHTING_PREVIEW = false;
		m_bWRITE_DEPTH_TO_DESTALPHA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE && m_bLIGHTING_PREVIEW && m_bWRITE_DEPTH_TO_DESTALPHA );
		return ( 1 * m_nPIXELFOGTYPE ) + ( 2 * m_nLIGHTING_PREVIEW ) + ( 6 * m_nWRITE_DEPTH_TO_DESTALPHA ) + 0;
	}
};

#define shaderDynamicTest_l4d2_unlittwotexture_ps20b psh_forgot_to_set_dynamic_PIXELFOGTYPE + psh_forgot_to_set_dynamic_LIGHTING_PREVIEW + psh_forgot_to_set_dynamic_WRITE_DEPTH_TO_DESTALPHA

