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
#include "aftershock_ps2x.h"
typedef dx12cb::aftershock_ps2x aftershock_ps51_Block;
class aftershock_ps51_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	aftershock_ps51_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
	}

	int GetIndex() const
	{
		return ( 1 * m_nCONVERT_TO_SRGB ) + 0;
	}
};

#define shaderStaticTest_aftershock_ps51 1


class aftershock_ps51_Dynamic_Index
{
public:
	aftershock_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< aftershock_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_aftershock_ps51 1

