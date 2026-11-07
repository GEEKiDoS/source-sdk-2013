// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $FLASHLIGHT
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
#include "vertexlit_and_unlit_generic_bump_ps2x.h"
typedef dx12cb::vertexlit_and_unlit_generic_bump_ps2x vertexlit_and_unlit_generic_bump_shadowmap_ps51_Block;
class vertexlit_and_unlit_generic_bump_shadowmap_ps51_Static_Index
{
	unsigned int m_nFLASHLIGHT : 2;
#ifdef _DEBUG
	bool m_bFLASHLIGHT : 1;
#endif	// _DEBUG
public:
	void SetFLASHLIGHT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHT = i;
#ifdef _DEBUG
		m_bFLASHLIGHT = true;
#endif	// _DEBUG
	}

	vertexlit_and_unlit_generic_bump_shadowmap_ps51_Static_Index(  )
	{
		m_nFLASHLIGHT = 0;
#ifdef _DEBUG
		m_bFLASHLIGHT = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFLASHLIGHT );
		AssertMsg( !m_nFLASHLIGHT, "Invalid combo combination FLASHLIGHT" );
		return ( 1 * m_nFLASHLIGHT ) + 0;
	}
};

#define shaderStaticTest_vertexlit_and_unlit_generic_bump_shadowmap_ps51 psh_forgot_to_set_static_FLASHLIGHT


class vertexlit_and_unlit_generic_bump_shadowmap_ps51_Dynamic_Index
{
public:
	vertexlit_and_unlit_generic_bump_shadowmap_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< vertexlit_and_unlit_generic_bump_shadowmap_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_vertexlit_and_unlit_generic_bump_shadowmap_ps51 1

