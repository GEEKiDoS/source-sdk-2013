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
#include "PostComposite.h"
typedef dx12cb::PostComposite postfx_composite_ps51_Block;
class postfx_composite_ps51_Static_Index
{
public:
	postfx_composite_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_postfx_composite_ps51 1


class postfx_composite_ps51_Dynamic_Index
{
	unsigned int m_nCAMERA_MODEL : 2;
#ifdef _DEBUG
	bool m_bCAMERA_MODEL : 1;
#endif	// _DEBUG
public:
	void SetCAMERA_MODEL( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nCAMERA_MODEL = i;
#ifdef _DEBUG
		m_bCAMERA_MODEL = true;
#endif	// _DEBUG
	}

	postfx_composite_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< postfx_composite_ps51_Block >( dx12native::kStagePixel );
		m_nCAMERA_MODEL = 0;
#ifdef _DEBUG
		m_bCAMERA_MODEL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCAMERA_MODEL );
		return ( 1 * m_nCAMERA_MODEL ) + 0;
	}
};

#define shaderDynamicTest_postfx_composite_ps51 psh_forgot_to_set_dynamic_CAMERA_MODEL

