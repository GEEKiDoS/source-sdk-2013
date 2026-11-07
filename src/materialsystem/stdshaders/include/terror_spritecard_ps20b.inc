// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// !$DISTANCEALPHA && ( $SOFTEDGES || $OUTLINE )
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class terror_spritecard_ps20b_Static_Index
{
	unsigned int m_nDISTANCEALPHA : 2;
	unsigned int m_nSOFTEDGES : 2;
	unsigned int m_nOUTLINE : 2;
	unsigned int m_nANIMBLEND : 2;
#ifdef _DEBUG
	bool m_bDISTANCEALPHA : 1;
	bool m_bSOFTEDGES : 1;
	bool m_bOUTLINE : 1;
	bool m_bANIMBLEND : 1;
#endif	// _DEBUG
public:
	void SetDISTANCEALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDISTANCEALPHA = i;
#ifdef _DEBUG
		m_bDISTANCEALPHA = true;
#endif	// _DEBUG
	}

	void SetSOFTEDGES( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSOFTEDGES = i;
#ifdef _DEBUG
		m_bSOFTEDGES = true;
#endif	// _DEBUG
	}

	void SetOUTLINE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nOUTLINE = i;
#ifdef _DEBUG
		m_bOUTLINE = true;
#endif	// _DEBUG
	}

	void SetANIMBLEND( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nANIMBLEND = i;
#ifdef _DEBUG
		m_bANIMBLEND = true;
#endif	// _DEBUG
	}

	terror_spritecard_ps20b_Static_Index(  )
	{
		m_nDISTANCEALPHA = 0;
		m_nSOFTEDGES = 0;
		m_nOUTLINE = 0;
		m_nANIMBLEND = 0;
#ifdef _DEBUG
		m_bDISTANCEALPHA = false;
		m_bSOFTEDGES = false;
		m_bOUTLINE = false;
		m_bANIMBLEND = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bDISTANCEALPHA && m_bSOFTEDGES && m_bOUTLINE && m_bANIMBLEND );
		AssertMsg( !( !m_nDISTANCEALPHA && ( m_nSOFTEDGES || m_nOUTLINE ) ), "Invalid combo combination ( !DISTANCEALPHA && ( SOFTEDGES || OUTLINE ) )" );
		return ( 1 * m_nDISTANCEALPHA ) + ( 2 * m_nSOFTEDGES ) + ( 4 * m_nOUTLINE ) + ( 8 * m_nANIMBLEND ) + 0;
	}
};

#define shaderStaticTest_terror_spritecard_ps20b psh_forgot_to_set_static_DISTANCEALPHA + psh_forgot_to_set_static_SOFTEDGES + psh_forgot_to_set_static_OUTLINE + psh_forgot_to_set_static_ANIMBLEND


class terror_spritecard_ps20b_Dynamic_Index
{
public:
	terror_spritecard_ps20b_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_terror_spritecard_ps20b 1

