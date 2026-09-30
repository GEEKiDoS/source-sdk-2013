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
#include "Engine_Post_ps2x.h"
typedef dx12cb::Engine_Post_ps2x engine_post_ps51_Block;
class engine_post_ps51_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nLINEAR_INPUT : 2;
	unsigned int m_nLINEAR_OUTPUT : 2;
#ifdef _DEBUG
	bool m_bLINEAR_INPUT : 1;
	bool m_bLINEAR_OUTPUT : 1;
#endif	// _DEBUG
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	void SetLINEAR_INPUT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLINEAR_INPUT = i;
#ifdef _DEBUG
		m_bLINEAR_INPUT = true;
#endif	// _DEBUG
	}

	void SetLINEAR_OUTPUT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLINEAR_OUTPUT = i;
#ifdef _DEBUG
		m_bLINEAR_OUTPUT = true;
#endif	// _DEBUG
	}

	engine_post_ps51_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
		m_nLINEAR_INPUT = 0;
		m_nLINEAR_OUTPUT = 0;
#ifdef _DEBUG
		m_bLINEAR_INPUT = false;
		m_bLINEAR_OUTPUT = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLINEAR_INPUT && m_bLINEAR_OUTPUT );
		return ( 40 * m_nCONVERT_TO_SRGB ) + ( 80 * m_nLINEAR_INPUT ) + ( 160 * m_nLINEAR_OUTPUT ) + 0;
	}
};

#define shaderStaticTest_engine_post_ps51 psh_forgot_to_set_static_LINEAR_INPUT + psh_forgot_to_set_static_LINEAR_OUTPUT


class engine_post_ps51_Dynamic_Index
{
	unsigned int m_nAA_ENABLE : 2;
	unsigned int m_nAA_QUALITY_MODE : 2;
	unsigned int m_nAA_REDUCE_ONE_PIXEL_LINE_BLUR : 2;
	unsigned int m_nCOL_CORRECT_NUM_LOOKUPS : 3;
#ifdef _DEBUG
	bool m_bAA_ENABLE : 1;
	bool m_bAA_QUALITY_MODE : 1;
	bool m_bAA_REDUCE_ONE_PIXEL_LINE_BLUR : 1;
	bool m_bCOL_CORRECT_NUM_LOOKUPS : 1;
#endif	// _DEBUG
public:
	void SetAA_ENABLE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nAA_ENABLE = i;
#ifdef _DEBUG
		m_bAA_ENABLE = true;
#endif	// _DEBUG
	}

	void SetAA_QUALITY_MODE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nAA_QUALITY_MODE = i;
#ifdef _DEBUG
		m_bAA_QUALITY_MODE = true;
#endif	// _DEBUG
	}

	void SetAA_REDUCE_ONE_PIXEL_LINE_BLUR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nAA_REDUCE_ONE_PIXEL_LINE_BLUR = i;
#ifdef _DEBUG
		m_bAA_REDUCE_ONE_PIXEL_LINE_BLUR = true;
#endif	// _DEBUG
	}

	void SetCOL_CORRECT_NUM_LOOKUPS( int i )
	{
		Assert( i >= 0 && i <= 4 );
		m_nCOL_CORRECT_NUM_LOOKUPS = i;
#ifdef _DEBUG
		m_bCOL_CORRECT_NUM_LOOKUPS = true;
#endif	// _DEBUG
	}

	engine_post_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< engine_post_ps51_Block >( dx12native::kStagePixel );
		m_nAA_ENABLE = 0;
		m_nAA_QUALITY_MODE = 0;
		m_nAA_REDUCE_ONE_PIXEL_LINE_BLUR = 0;
		m_nCOL_CORRECT_NUM_LOOKUPS = 0;
#ifdef _DEBUG
		m_bAA_ENABLE = false;
		m_bAA_QUALITY_MODE = false;
		m_bAA_REDUCE_ONE_PIXEL_LINE_BLUR = false;
		m_bCOL_CORRECT_NUM_LOOKUPS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bAA_ENABLE && m_bAA_QUALITY_MODE && m_bAA_REDUCE_ONE_PIXEL_LINE_BLUR && m_bCOL_CORRECT_NUM_LOOKUPS );
		return ( 1 * m_nAA_ENABLE ) + ( 2 * m_nAA_QUALITY_MODE ) + ( 4 * m_nAA_REDUCE_ONE_PIXEL_LINE_BLUR ) + ( 8 * m_nCOL_CORRECT_NUM_LOOKUPS ) + 0;
	}
};

#define shaderDynamicTest_engine_post_ps51 psh_forgot_to_set_dynamic_AA_ENABLE + psh_forgot_to_set_dynamic_AA_QUALITY_MODE + psh_forgot_to_set_dynamic_AA_REDUCE_ONE_PIXEL_LINE_BLUR + psh_forgot_to_set_dynamic_COL_CORRECT_NUM_LOOKUPS

