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
#include "warp_ps2x.h"
typedef dx12cb::warp_ps2x warp_ps51_Block;
class warp_ps51_Static_Index
{
public:
	warp_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_warp_ps51 1


class warp_ps51_Dynamic_Index
{
	unsigned int m_nDISTORT_TYPE : 2;
#ifdef _DEBUG
	bool m_bDISTORT_TYPE : 1;
#endif	// _DEBUG
public:
	void SetDISTORT_TYPE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nDISTORT_TYPE = i;
#ifdef _DEBUG
		m_bDISTORT_TYPE = true;
#endif	// _DEBUG
	}

	warp_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< warp_ps51_Block >( dx12native::kStagePixel );
		m_nDISTORT_TYPE = 0;
#ifdef _DEBUG
		m_bDISTORT_TYPE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bDISTORT_TYPE );
		return ( 1 * m_nDISTORT_TYPE ) + 0;
	}
};

#define shaderDynamicTest_warp_ps51 psh_forgot_to_set_dynamic_DISTORT_TYPE

