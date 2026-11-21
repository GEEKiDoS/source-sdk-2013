// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// ( $LIGHTPASS == 2 ) && ( $ALPHATEST == 1 )
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
	unsigned int m_nNMS : 2;
	unsigned int m_nL4N : 2;
	unsigned int m_nALPHATEST : 2;
	unsigned int m_nLIGHTPASS : 2;
#ifdef _DEBUG
	bool m_bLAYERED : 1;
	bool m_bNMS : 1;
	bool m_bL4N : 1;
	bool m_bALPHATEST : 1;
	bool m_bLIGHTPASS : 1;
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

	void SetNMS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nNMS = i;
#ifdef _DEBUG
		m_bNMS = true;
#endif	// _DEBUG
	}

	void SetL4N( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nL4N = i;
#ifdef _DEBUG
		m_bL4N = true;
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

	void SetLIGHTPASS( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nLIGHTPASS = i;
#ifdef _DEBUG
		m_bLIGHTPASS = true;
#endif	// _DEBUG
	}

	pbr_world_highres_earlydepth_ps51_Static_Index(  )
	{
		m_nLAYERED = 0;
		m_nNMS = 0;
		m_nL4N = 0;
		m_nALPHATEST = 0;
		m_nLIGHTPASS = 0;
#ifdef _DEBUG
		m_bLAYERED = false;
		m_bNMS = false;
		m_bL4N = false;
		m_bALPHATEST = false;
		m_bLIGHTPASS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLAYERED && m_bNMS && m_bL4N && m_bALPHATEST && m_bLIGHTPASS );
		AssertMsg( !( ( m_nLIGHTPASS == 2 ) && ( m_nALPHATEST == 1 ) ), "Invalid combo combination ( ( LIGHTPASS == 2 ) && ( ALPHATEST == 1 ) )" );
		return ( 15 * m_nLAYERED ) + ( 30 * m_nNMS ) + ( 60 * m_nL4N ) + ( 120 * m_nALPHATEST ) + ( 240 * m_nLIGHTPASS ) + 0;
	}
};

#define shaderStaticTest_pbr_world_highres_earlydepth_ps51 psh_forgot_to_set_static_LAYERED + psh_forgot_to_set_static_NMS + psh_forgot_to_set_static_L4N + psh_forgot_to_set_static_ALPHATEST + psh_forgot_to_set_static_LIGHTPASS


class pbr_world_highres_earlydepth_ps51_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
	unsigned int m_nSHADOWQUALITY : 3;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
	bool m_bSHADOWQUALITY : 1;
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

	void SetSHADOWQUALITY( int i )
	{
		Assert( i >= 0 && i <= 4 );
		m_nSHADOWQUALITY = i;
#ifdef _DEBUG
		m_bSHADOWQUALITY = true;
#endif	// _DEBUG
	}

	pbr_world_highres_earlydepth_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pbr_world_highres_earlydepth_ps51_Block >( dx12native::kStagePixel );
		m_nPIXELFOGTYPE = 0;
		m_nSHADOWQUALITY = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
		m_bSHADOWQUALITY = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE && m_bSHADOWQUALITY );
		return ( 1 * m_nPIXELFOGTYPE ) + ( 3 * m_nSHADOWQUALITY ) + 0;
	}
};

#define shaderDynamicTest_pbr_world_highres_earlydepth_ps51 psh_forgot_to_set_dynamic_PIXELFOGTYPE + psh_forgot_to_set_dynamic_SHADOWQUALITY

