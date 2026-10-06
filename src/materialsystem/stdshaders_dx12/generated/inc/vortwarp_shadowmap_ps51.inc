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
#include "vortwarp_ps2x.h"
typedef dx12cb::vortwarp_ps2x vortwarp_shadowmap_ps51_Block;
class vortwarp_shadowmap_ps51_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nFLASHLIGHT : 2;
#ifdef _DEBUG
	bool m_bCONVERT_TO_SRGB : 1;
	bool m_bFLASHLIGHT : 1;
#endif	// _DEBUG
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
#ifdef _DEBUG
		m_bCONVERT_TO_SRGB = true;
#endif	// _DEBUG
	}

	void SetFLASHLIGHT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHT = i;
#ifdef _DEBUG
		m_bFLASHLIGHT = true;
#endif	// _DEBUG
	}

	vortwarp_shadowmap_ps51_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = 0;
		m_nFLASHLIGHT = 0;
#ifdef _DEBUG
		m_bCONVERT_TO_SRGB = false;
		m_bFLASHLIGHT = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCONVERT_TO_SRGB && m_bFLASHLIGHT );
		AssertMsg( !m_nFLASHLIGHT, "Invalid combo combination FLASHLIGHT" );
		return ( 1 * m_nCONVERT_TO_SRGB ) + ( 2 * m_nFLASHLIGHT ) + 0;
	}
};

#define shaderStaticTest_vortwarp_shadowmap_ps51 psh_forgot_to_set_static_CONVERT_TO_SRGB + psh_forgot_to_set_static_FLASHLIGHT


class vortwarp_shadowmap_ps51_Dynamic_Index
{
public:
	vortwarp_shadowmap_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< vortwarp_shadowmap_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_vortwarp_shadowmap_ps51 1

