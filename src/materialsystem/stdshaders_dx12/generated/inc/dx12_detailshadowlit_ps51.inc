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
#include "BaseVSShaderDX12.h"
#include "DetailShadowLitMaterial.h"
typedef dx12cb::DetailShadowLitMaterial dx12_detailshadowlit_ps51_Block;
class dx12_detailshadowlit_ps51_Static_Index
{
	unsigned int m_nHDRTYPE : 2;
#ifdef _DEBUG
	bool m_bHDRTYPE : 1;
#endif	// _DEBUG
public:
	void SetHDRTYPE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nHDRTYPE = i;
#ifdef _DEBUG
		m_bHDRTYPE = true;
#endif	// _DEBUG
	}

	dx12_detailshadowlit_ps51_Static_Index(  )
	{
		m_nHDRTYPE = 0;
#ifdef _DEBUG
		m_bHDRTYPE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bHDRTYPE );
		return ( 3 * m_nHDRTYPE ) + 0;
	}
};

#define shaderStaticTest_dx12_detailshadowlit_ps51 psh_forgot_to_set_static_HDRTYPE


class dx12_detailshadowlit_ps51_Dynamic_Index
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

	dx12_detailshadowlit_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< dx12_detailshadowlit_ps51_Block >( dx12native::kStagePixel );
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

#define shaderDynamicTest_dx12_detailshadowlit_ps51 psh_forgot_to_set_dynamic_PIXELFOGTYPE

