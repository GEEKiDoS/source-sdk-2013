// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// !$ENHANCED && $SHADOWQUALITY
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "NekoModelPS.h"
typedef dx12cb::NekoModelPS nekotoon_model_ps51_Block;
class nekotoon_model_ps51_Static_Index
{
	unsigned int m_nENHANCED : 2;
	unsigned int m_nBUMP : 2;
	unsigned int m_nHULL : 2;
#ifdef _DEBUG
	bool m_bENHANCED : 1;
	bool m_bBUMP : 1;
	bool m_bHULL : 1;
#endif	// _DEBUG
public:
	void SetENHANCED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nENHANCED = i;
#ifdef _DEBUG
		m_bENHANCED = true;
#endif	// _DEBUG
	}

	void SetBUMP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMP = i;
#ifdef _DEBUG
		m_bBUMP = true;
#endif	// _DEBUG
	}

	void SetHULL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHULL = i;
#ifdef _DEBUG
		m_bHULL = true;
#endif	// _DEBUG
	}

	nekotoon_model_ps51_Static_Index(  )
	{
		m_nENHANCED = 0;
		m_nBUMP = 0;
		m_nHULL = 0;
#ifdef _DEBUG
		m_bENHANCED = false;
		m_bBUMP = false;
		m_bHULL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bENHANCED && m_bBUMP && m_bHULL );
		return ( 12 * m_nENHANCED ) + ( 24 * m_nBUMP ) + ( 48 * m_nHULL ) + 0;
	}
};

#define shaderStaticTest_nekotoon_model_ps51 psh_forgot_to_set_static_ENHANCED + psh_forgot_to_set_static_BUMP + psh_forgot_to_set_static_HULL


class nekotoon_model_ps51_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
	unsigned int m_nSHADOWQUALITY : 3;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
	bool m_bSHADOWQUALITY : 1;
#endif	// _DEBUG
public:
	void SetPIXELFOGTYPE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nPIXELFOGTYPE = i;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = true;
#endif	// _DEBUG
	}

	void SetSHADOWQUALITY( int i )
	{
		Assert( i >= 0 && i <= 3 );
		m_nSHADOWQUALITY = i;
#ifdef _DEBUG
		m_bSHADOWQUALITY = true;
#endif	// _DEBUG
	}

	nekotoon_model_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< nekotoon_model_ps51_Block >( dx12native::kStagePixel );
		m_nPIXELFOGTYPE = 0;
		m_nSHADOWQUALITY = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
		m_bSHADOWQUALITY = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE && m_bSHADOWQUALITY );
		return ( 1 * m_nPIXELFOGTYPE ) + ( 3 * m_nSHADOWQUALITY ) + 0;
	}
};

#define shaderDynamicTest_nekotoon_model_ps51 psh_forgot_to_set_dynamic_PIXELFOGTYPE + psh_forgot_to_set_dynamic_SHADOWQUALITY

