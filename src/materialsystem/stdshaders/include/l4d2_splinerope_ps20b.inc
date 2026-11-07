// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class l4d2_splinerope_ps20b_Static_Index
{
	unsigned int m_nSHADOWDEPTH : 2;
#ifdef _DEBUG
	bool m_bSHADOWDEPTH : 1;
#endif	// _DEBUG
public:
	void SetSHADOWDEPTH( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSHADOWDEPTH = i;
#ifdef _DEBUG
		m_bSHADOWDEPTH = true;
#endif	// _DEBUG
	}

	l4d2_splinerope_ps20b_Static_Index(  )
	{
		m_nSHADOWDEPTH = 0;
#ifdef _DEBUG
		m_bSHADOWDEPTH = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSHADOWDEPTH );
		return ( 1 * m_nSHADOWDEPTH ) + 0;
	}
};

#define shaderStaticTest_l4d2_splinerope_ps20b psh_forgot_to_set_static_SHADOWDEPTH


class l4d2_splinerope_ps20b_Dynamic_Index
{
public:
	l4d2_splinerope_ps20b_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_l4d2_splinerope_ps20b 1

