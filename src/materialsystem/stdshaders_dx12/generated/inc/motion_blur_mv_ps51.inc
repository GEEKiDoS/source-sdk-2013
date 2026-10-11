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
#include "motion_blur_mv.h"
typedef dx12cb::motion_blur_mv motion_blur_mv_ps51_Block;
class motion_blur_mv_ps51_Static_Index
{
public:
	motion_blur_mv_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_motion_blur_mv_ps51 1


class motion_blur_mv_ps51_Dynamic_Index
{
	unsigned int m_nQUALITY : 3;
	unsigned int m_nTILE : 2;
#ifdef _DEBUG
	bool m_bQUALITY : 1;
	bool m_bTILE : 1;
#endif	// _DEBUG
public:
	void SetQUALITY( int i )
	{
		Assert( i >= 0 && i <= 3 );
		m_nQUALITY = i;
#ifdef _DEBUG
		m_bQUALITY = true;
#endif	// _DEBUG
	}

	void SetTILE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTILE = i;
#ifdef _DEBUG
		m_bTILE = true;
#endif	// _DEBUG
	}

	motion_blur_mv_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< motion_blur_mv_ps51_Block >( dx12native::kStagePixel );
		m_nQUALITY = 0;
		m_nTILE = 0;
#ifdef _DEBUG
		m_bQUALITY = false;
		m_bTILE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bQUALITY && m_bTILE );
		return ( 1 * m_nQUALITY ) + ( 4 * m_nTILE ) + 0;
	}
};

#define shaderDynamicTest_motion_blur_mv_ps51 psh_forgot_to_set_dynamic_QUALITY + psh_forgot_to_set_dynamic_TILE

