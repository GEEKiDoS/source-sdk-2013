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
#include "worldtwotextureblend_ps2x_shadowmap.h"
typedef dx12cb::worldtwotextureblend_ps2x_shadowmap worldtwotextureblend_shadowmap_ps51_Block;
class worldtwotextureblend_shadowmap_ps51_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nSEAMLESS : 2;
#ifdef _DEBUG
	bool m_bCONVERT_TO_SRGB : 1;
	bool m_bSEAMLESS : 1;
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

	void SetSEAMLESS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEAMLESS = i;
#ifdef _DEBUG
		m_bSEAMLESS = true;
#endif	// _DEBUG
	}

	worldtwotextureblend_shadowmap_ps51_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = 0;
		m_nSEAMLESS = 0;
#ifdef _DEBUG
		m_bCONVERT_TO_SRGB = false;
		m_bSEAMLESS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCONVERT_TO_SRGB && m_bSEAMLESS );
		return ( 1 * m_nCONVERT_TO_SRGB ) + ( 2 * m_nSEAMLESS ) + 0;
	}
};

#define shaderStaticTest_worldtwotextureblend_shadowmap_ps51 psh_forgot_to_set_static_CONVERT_TO_SRGB + psh_forgot_to_set_static_SEAMLESS


class worldtwotextureblend_shadowmap_ps51_Dynamic_Index
{
public:
	worldtwotextureblend_shadowmap_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< worldtwotextureblend_shadowmap_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_worldtwotextureblend_shadowmap_ps51 1

