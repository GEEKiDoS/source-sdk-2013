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
#include "Downsample_nohdr_ps2x.h"
typedef dx12cb::Downsample_nohdr_ps2x downsample_nohdr_ps51_Block;
class downsample_nohdr_ps51_Static_Index
{
	unsigned int m_nSRGB_ADAPTER : 2;
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nCSTRIKE : 2;
#ifdef _DEBUG
	bool m_bSRGB_ADAPTER : 1;
	bool m_bCSTRIKE : 1;
#endif	// _DEBUG
public:
	void SetSRGB_ADAPTER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSRGB_ADAPTER = i;
#ifdef _DEBUG
		m_bSRGB_ADAPTER = true;
#endif	// _DEBUG
	}

	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	void SetCSTRIKE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCSTRIKE = i;
#ifdef _DEBUG
		m_bCSTRIKE = true;
#endif	// _DEBUG
	}

	downsample_nohdr_ps51_Static_Index(  )
	{
		m_nSRGB_ADAPTER = 0;
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
		m_nCSTRIKE = 0;
#ifdef _DEBUG
		m_bSRGB_ADAPTER = false;
		m_bCSTRIKE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSRGB_ADAPTER && m_bCSTRIKE );
		return ( 1 * m_nSRGB_ADAPTER ) + ( 2 * m_nCONVERT_TO_SRGB ) + ( 4 * m_nCSTRIKE ) + 0;
	}
};

#define shaderStaticTest_downsample_nohdr_ps51 psh_forgot_to_set_static_SRGB_ADAPTER + psh_forgot_to_set_static_CSTRIKE


class downsample_nohdr_ps51_Dynamic_Index
{
public:
	downsample_nohdr_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< downsample_nohdr_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_downsample_nohdr_ps51 1

