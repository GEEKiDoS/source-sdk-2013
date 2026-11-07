// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $FLASHLIGHT
// $LIGHTING_PREVIEW != 0
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
#include "vertexlit_and_unlit_generic_ps2x.h"
typedef dx12cb::vertexlit_and_unlit_generic_ps2x vertexlit_and_unlit_generic_shadowmap_ps51_Block;
class vertexlit_and_unlit_generic_shadowmap_ps51_Static_Index
{
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nSEAMLESS_BASE : 2;
	unsigned int m_nSEAMLESS_DETAIL : 2;
	unsigned int m_nCUBEMAP_SPHERE_LEGACY : 2;
#ifdef _DEBUG
	bool m_bFLASHLIGHT : 1;
	bool m_bSEAMLESS_BASE : 1;
	bool m_bSEAMLESS_DETAIL : 1;
	bool m_bCUBEMAP_SPHERE_LEGACY : 1;
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

	void SetSEAMLESS_BASE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEAMLESS_BASE = i;
#ifdef _DEBUG
		m_bSEAMLESS_BASE = true;
#endif	// _DEBUG
	}

	void SetSEAMLESS_DETAIL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEAMLESS_DETAIL = i;
#ifdef _DEBUG
		m_bSEAMLESS_DETAIL = true;
#endif	// _DEBUG
	}

	void SetCUBEMAP_SPHERE_LEGACY( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCUBEMAP_SPHERE_LEGACY = i;
#ifdef _DEBUG
		m_bCUBEMAP_SPHERE_LEGACY = true;
#endif	// _DEBUG
	}

	vertexlit_and_unlit_generic_shadowmap_ps51_Static_Index(  )
	{
		m_nFLASHLIGHT = 0;
		m_nSEAMLESS_BASE = 0;
		m_nSEAMLESS_DETAIL = 0;
		m_nCUBEMAP_SPHERE_LEGACY = 0;
#ifdef _DEBUG
		m_bFLASHLIGHT = false;
		m_bSEAMLESS_BASE = false;
		m_bSEAMLESS_DETAIL = false;
		m_bCUBEMAP_SPHERE_LEGACY = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFLASHLIGHT && m_bSEAMLESS_BASE && m_bSEAMLESS_DETAIL && m_bCUBEMAP_SPHERE_LEGACY );
		AssertMsg( !m_nFLASHLIGHT, "Invalid combo combination FLASHLIGHT" );
		return ( 3 * m_nFLASHLIGHT ) + ( 6 * m_nSEAMLESS_BASE ) + ( 12 * m_nSEAMLESS_DETAIL ) + ( 24 * m_nCUBEMAP_SPHERE_LEGACY ) + 0;
	}
};

#define shaderStaticTest_vertexlit_and_unlit_generic_shadowmap_ps51 psh_forgot_to_set_static_FLASHLIGHT + psh_forgot_to_set_static_SEAMLESS_BASE + psh_forgot_to_set_static_SEAMLESS_DETAIL + psh_forgot_to_set_static_CUBEMAP_SPHERE_LEGACY


class vertexlit_and_unlit_generic_shadowmap_ps51_Dynamic_Index
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

	vertexlit_and_unlit_generic_shadowmap_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< vertexlit_and_unlit_generic_shadowmap_ps51_Block >( dx12native::kStagePixel );
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

#define shaderDynamicTest_vertexlit_and_unlit_generic_shadowmap_ps51 psh_forgot_to_set_dynamic_LIGHTING_PREVIEW

