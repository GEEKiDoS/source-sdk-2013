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
#include "BaseVSShaderDX12.h"
#include "PBRMaterialPS.h"
typedef dx12cb::PBRMaterialPS pbr_model_ps51_Block;
class pbr_model_ps51_Static_Index
{
	unsigned int m_nTANGENT : 2;
	unsigned int m_nENHANCED : 2;
	unsigned int m_nALPHATEST : 2;
#ifdef _DEBUG
	bool m_bTANGENT : 1;
	bool m_bENHANCED : 1;
	bool m_bALPHATEST : 1;
#endif	// _DEBUG
public:
	void SetTANGENT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTANGENT = i;
#ifdef _DEBUG
		m_bTANGENT = true;
#endif	// _DEBUG
	}

	void SetENHANCED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nENHANCED = i;
#ifdef _DEBUG
		m_bENHANCED = true;
#endif	// _DEBUG
	}

	void SetALPHATEST( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nALPHATEST = i;
#ifdef _DEBUG
		m_bALPHATEST = true;
#endif	// _DEBUG
	}

	pbr_model_ps51_Static_Index(  )
	{
		m_nTANGENT = 0;
		m_nENHANCED = 0;
		m_nALPHATEST = 0;
#ifdef _DEBUG
		m_bTANGENT = false;
		m_bENHANCED = false;
		m_bALPHATEST = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bTANGENT && m_bENHANCED && m_bALPHATEST );
		return ( 3 * m_nTANGENT ) + ( 6 * m_nENHANCED ) + ( 12 * m_nALPHATEST ) + 0;
	}
};

#define shaderStaticTest_pbr_model_ps51 psh_forgot_to_set_static_TANGENT + psh_forgot_to_set_static_ENHANCED + psh_forgot_to_set_static_ALPHATEST


class pbr_model_ps51_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
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

	pbr_model_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pbr_model_ps51_Block >( dx12native::kStagePixel );
		m_nPIXELFOGTYPE = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE );
		return ( 1 * m_nPIXELFOGTYPE ) + 0;
	}
};

#define shaderDynamicTest_pbr_model_ps51 psh_forgot_to_set_dynamic_PIXELFOGTYPE

