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
#include "sky_hdr_compressed_rgbs_ps2x.h"
typedef dx12cb::sky_hdr_compressed_rgbs_ps2x sky_hdr_compressed_rgbs_ps51_Block;
class sky_hdr_compressed_rgbs_ps51_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	sky_hdr_compressed_rgbs_ps51_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
	}

	int GetIndex() const
	{
		return ( 2 * m_nCONVERT_TO_SRGB ) + 0;
	}
};

#define shaderStaticTest_sky_hdr_compressed_rgbs_ps51 1


class sky_hdr_compressed_rgbs_ps51_Dynamic_Index
{
	unsigned int m_nWRITE_DEPTH_TO_DESTALPHA : 2;
#ifdef _DEBUG
	bool m_bWRITE_DEPTH_TO_DESTALPHA : 1;
#endif	// _DEBUG
public:
	void SetWRITE_DEPTH_TO_DESTALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWRITE_DEPTH_TO_DESTALPHA = i;
#ifdef _DEBUG
		m_bWRITE_DEPTH_TO_DESTALPHA = true;
#endif	// _DEBUG
	}

	sky_hdr_compressed_rgbs_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< sky_hdr_compressed_rgbs_ps51_Block >( dx12native::kStagePixel );
		m_nWRITE_DEPTH_TO_DESTALPHA = 0;
#ifdef _DEBUG
		m_bWRITE_DEPTH_TO_DESTALPHA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bWRITE_DEPTH_TO_DESTALPHA );
		return ( 1 * m_nWRITE_DEPTH_TO_DESTALPHA ) + 0;
	}
};

#define shaderDynamicTest_sky_hdr_compressed_rgbs_ps51 psh_forgot_to_set_dynamic_WRITE_DEPTH_TO_DESTALPHA

