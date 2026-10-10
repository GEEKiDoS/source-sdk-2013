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
#include "PBRMaterialPS.h"
typedef dx12cb::PBRMaterialPS pbr_world_highres_earlydepth_ps51_Block;
class pbr_world_highres_earlydepth_ps51_Static_Index
{
	unsigned int m_nLAYERED : 2;
	unsigned int m_nALPHATEST : 2;
#ifdef _DEBUG
	bool m_bLAYERED : 1;
	bool m_bALPHATEST : 1;
#endif	// _DEBUG
public:
	void SetLAYERED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLAYERED = i;
#ifdef _DEBUG
		m_bLAYERED = true;
#endif	// _DEBUG
	}

	void SetALPHATEST( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nALPHATEST = i;
#ifdef _DEBUG
		m_bALPHATEST = true;
#endif	// _DEBUG
	}

	pbr_world_highres_earlydepth_ps51_Static_Index(  )
	{
		m_nLAYERED = 0;
		m_nALPHATEST = 0;
#ifdef _DEBUG
		m_bLAYERED = false;
		m_bALPHATEST = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLAYERED && m_bALPHATEST );
		return ( 3 * m_nLAYERED ) + ( 6 * m_nALPHATEST ) + 0;
	}
};

#define shaderStaticTest_pbr_world_highres_earlydepth_ps51 psh_forgot_to_set_static_LAYERED + psh_forgot_to_set_static_ALPHATEST


class pbr_world_highres_earlydepth_ps51_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
#endif	// _DEBUG
public:
	void SetPIXELFOGTYPE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nPIXELFOGTYPE = i;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = true;
#endif	// _DEBUG
	}

	pbr_world_highres_earlydepth_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pbr_world_highres_earlydepth_ps51_Block >( dx12native::kStagePixel );
		m_nPIXELFOGTYPE = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE );
		return ( 1 * m_nPIXELFOGTYPE ) + 0;
	}
};

#define shaderDynamicTest_pbr_world_highres_earlydepth_ps51 psh_forgot_to_set_dynamic_PIXELFOGTYPE

