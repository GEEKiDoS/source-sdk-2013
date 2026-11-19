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
#include "RcasParams.h"
typedef dx12cb::RcasParams postfx_rcas_ps51_Block;
class postfx_rcas_ps51_Static_Index
{
public:
	postfx_rcas_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_postfx_rcas_ps51 1


class postfx_rcas_ps51_Dynamic_Index
{
	unsigned int m_nCOPY_ONLY : 2;
#ifdef _DEBUG
	bool m_bCOPY_ONLY : 1;
#endif	// _DEBUG
public:
	void SetCOPY_ONLY( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCOPY_ONLY = i;
#ifdef _DEBUG
		m_bCOPY_ONLY = true;
#endif	// _DEBUG
	}

	postfx_rcas_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< postfx_rcas_ps51_Block >( dx12native::kStagePixel );
		m_nCOPY_ONLY = 0;
#ifdef _DEBUG
		m_bCOPY_ONLY = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCOPY_ONLY );
		return ( 1 * m_nCOPY_ONLY ) + 0;
	}
};

#define shaderDynamicTest_postfx_rcas_ps51 psh_forgot_to_set_dynamic_COPY_ONLY

