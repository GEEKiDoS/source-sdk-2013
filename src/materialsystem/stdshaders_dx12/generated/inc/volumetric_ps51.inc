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
typedef DX12NoNativeBlock volumetric_ps51_Block;
class volumetric_ps51_Static_Index
{
	unsigned int m_nMSAA : 2;
#ifdef _DEBUG
	bool m_bMSAA : 1;
#endif	// _DEBUG
public:
	void SetMSAA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMSAA = i;
#ifdef _DEBUG
		m_bMSAA = true;
#endif	// _DEBUG
	}

	volumetric_ps51_Static_Index(  )
	{
		m_nMSAA = 0;
#ifdef _DEBUG
		m_bMSAA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bMSAA );
		return ( 2 * m_nMSAA ) + 0;
	}
};

#define shaderStaticTest_volumetric_ps51 psh_forgot_to_set_static_MSAA


class volumetric_ps51_Dynamic_Index
{
	unsigned int m_nQUARTER : 2;
#ifdef _DEBUG
	bool m_bQUARTER : 1;
#endif	// _DEBUG
public:
	void SetQUARTER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nQUARTER = i;
#ifdef _DEBUG
		m_bQUARTER = true;
#endif	// _DEBUG
	}

	volumetric_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< volumetric_ps51_Block >( dx12native::kStagePixel );
		m_nQUARTER = 0;
#ifdef _DEBUG
		m_bQUARTER = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bQUARTER );
		return ( 1 * m_nQUARTER ) + 0;
	}
};

#define shaderDynamicTest_volumetric_ps51 psh_forgot_to_set_dynamic_QUARTER

