// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $LIGHTING_PREVIEW != 0
// $SEAMLESS && $RELIEF_MAPPING
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
#include "lightmappedgeneric_ps2x_shadowmap.h"
typedef dx12cb::lightmappedgeneric_ps2x_shadowmap lightmappedgeneric_shadowmap_ps51_Block;
class lightmappedgeneric_shadowmap_ps51_Static_Index
{
	unsigned int m_nRELIEF_MAPPING : 1;
	unsigned int m_nSEAMLESS : 2;
	unsigned int m_nNORMAL_DECODE_MODE : 1;
	unsigned int m_nNORMALMASK_DECODE_MODE : 1;
#ifdef _DEBUG
	bool m_bRELIEF_MAPPING : 1;
	bool m_bSEAMLESS : 1;
	bool m_bNORMAL_DECODE_MODE : 1;
	bool m_bNORMALMASK_DECODE_MODE : 1;
#endif	// _DEBUG
public:
	void SetRELIEF_MAPPING( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nRELIEF_MAPPING = i;
#ifdef _DEBUG
		m_bRELIEF_MAPPING = true;
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

	void SetNORMAL_DECODE_MODE( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nNORMAL_DECODE_MODE = i;
#ifdef _DEBUG
		m_bNORMAL_DECODE_MODE = true;
#endif	// _DEBUG
	}

	void SetNORMALMASK_DECODE_MODE( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nNORMALMASK_DECODE_MODE = i;
#ifdef _DEBUG
		m_bNORMALMASK_DECODE_MODE = true;
#endif	// _DEBUG
	}

	lightmappedgeneric_shadowmap_ps51_Static_Index(  )
	{
		m_nRELIEF_MAPPING = 0;
		m_nSEAMLESS = 0;
		m_nNORMAL_DECODE_MODE = 0;
		m_nNORMALMASK_DECODE_MODE = 0;
#ifdef _DEBUG
		m_bRELIEF_MAPPING = false;
		m_bSEAMLESS = false;
		m_bNORMAL_DECODE_MODE = false;
		m_bNORMALMASK_DECODE_MODE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bRELIEF_MAPPING && m_bSEAMLESS && m_bNORMAL_DECODE_MODE && m_bNORMALMASK_DECODE_MODE );
		AssertMsg( !( m_nSEAMLESS && m_nRELIEF_MAPPING ), "Invalid combo combination ( SEAMLESS && RELIEF_MAPPING )" );
		return ( 3 * m_nRELIEF_MAPPING ) + ( 3 * m_nSEAMLESS ) + ( 6 * m_nNORMAL_DECODE_MODE ) + ( 6 * m_nNORMALMASK_DECODE_MODE ) + 0;
	}
};

#define shaderStaticTest_lightmappedgeneric_shadowmap_ps51 psh_forgot_to_set_static_RELIEF_MAPPING + psh_forgot_to_set_static_SEAMLESS + psh_forgot_to_set_static_NORMAL_DECODE_MODE + psh_forgot_to_set_static_NORMALMASK_DECODE_MODE


class lightmappedgeneric_shadowmap_ps51_Dynamic_Index
{
	unsigned int m_nLIGHTING_PREVIEW : 2;
#ifdef _DEBUG
	bool m_bLIGHTING_PREVIEW : 1;
#endif	// _DEBUG
public:
	void SetLIGHTING_PREVIEW( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nLIGHTING_PREVIEW = i;
#ifdef _DEBUG
		m_bLIGHTING_PREVIEW = true;
#endif	// _DEBUG
	}

	lightmappedgeneric_shadowmap_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< lightmappedgeneric_shadowmap_ps51_Block >( dx12native::kStagePixel );
		m_nLIGHTING_PREVIEW = 0;
#ifdef _DEBUG
		m_bLIGHTING_PREVIEW = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLIGHTING_PREVIEW );
		AssertMsg( !( m_nLIGHTING_PREVIEW != 0 ), "Invalid combo combination ( LIGHTING_PREVIEW != 0 )" );
		return ( 1 * m_nLIGHTING_PREVIEW ) + 0;
	}
};

#define shaderDynamicTest_lightmappedgeneric_shadowmap_ps51 psh_forgot_to_set_dynamic_LIGHTING_PREVIEW

