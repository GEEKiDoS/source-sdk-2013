// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $INFECTED && $VERTEXCOLOR
// ( $ENHANCED == 0 ) && ( $LIGHTPASS != 0 )
// ( $LIGHTPASS == 2 ) && ( $ALPHATEST == 1 )
// ( $ENHANCED == 0 ) && ( $SHADOWQUALITY != 0 )
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
typedef dx12cb::PBRMaterialPS pbr_model_ps51_Block;
class pbr_model_ps51_Static_Index
{
	unsigned int m_nTANGENT : 2;
	unsigned int m_nNMS : 2;
	unsigned int m_nL4N : 2;
	unsigned int m_nENHANCED : 2;
	unsigned int m_nALPHATEST : 2;
	unsigned int m_nINFECTED : 2;
	unsigned int m_nVERTEXCOLOR : 2;
	unsigned int m_nLIGHTPASS : 2;
#ifdef _DEBUG
	bool m_bTANGENT : 1;
	bool m_bNMS : 1;
	bool m_bL4N : 1;
	bool m_bENHANCED : 1;
	bool m_bALPHATEST : 1;
	bool m_bINFECTED : 1;
	bool m_bVERTEXCOLOR : 1;
	bool m_bLIGHTPASS : 1;
#endif	// _DEBUG
public:
	void SetTANGENT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTANGENT = i;
#ifdef _DEBUG
		m_bTANGENT = true;
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

	void SetENHANCED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nENHANCED = i;
#ifdef _DEBUG
		m_bENHANCED = true;
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

	void SetINFECTED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nINFECTED = i;
#ifdef _DEBUG
		m_bINFECTED = true;
#endif	// _DEBUG
	}

	void SetVERTEXCOLOR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVERTEXCOLOR = i;
#ifdef _DEBUG
		m_bVERTEXCOLOR = true;
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

	pbr_model_ps51_Static_Index(  )
	{
		m_nTANGENT = 0;
		m_nNMS = 0;
		m_nL4N = 0;
		m_nENHANCED = 0;
		m_nALPHATEST = 0;
		m_nINFECTED = 0;
		m_nVERTEXCOLOR = 0;
		m_nLIGHTPASS = 0;
#ifdef _DEBUG
		m_bTANGENT = false;
		m_bNMS = false;
		m_bL4N = false;
		m_bENHANCED = false;
		m_bALPHATEST = false;
		m_bINFECTED = false;
		m_bVERTEXCOLOR = false;
		m_bLIGHTPASS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bTANGENT && m_bNMS && m_bL4N && m_bENHANCED && m_bALPHATEST && m_bINFECTED && m_bVERTEXCOLOR && m_bLIGHTPASS );
		AssertMsg( !( m_nINFECTED && m_nVERTEXCOLOR ), "Invalid combo combination ( INFECTED && VERTEXCOLOR )" );
		AssertMsg( !( ( m_nENHANCED == 0 ) && ( m_nLIGHTPASS != 0 ) ), "Invalid combo combination ( ( ENHANCED == 0 ) && ( LIGHTPASS != 0 ) )" );
		AssertMsg( !( ( m_nLIGHTPASS == 2 ) && ( m_nALPHATEST == 1 ) ), "Invalid combo combination ( ( LIGHTPASS == 2 ) && ( ALPHATEST == 1 ) )" );
		return ( 30 * m_nTANGENT ) + ( 60 * m_nNMS ) + ( 120 * m_nL4N ) + ( 240 * m_nENHANCED ) + ( 480 * m_nALPHATEST ) + ( 960 * m_nINFECTED ) + ( 1920 * m_nVERTEXCOLOR ) + ( 3840 * m_nLIGHTPASS ) + 0;
	}
};

#define shaderStaticTest_pbr_model_ps51 psh_forgot_to_set_static_TANGENT + psh_forgot_to_set_static_NMS + psh_forgot_to_set_static_L4N + psh_forgot_to_set_static_ENHANCED + psh_forgot_to_set_static_ALPHATEST + psh_forgot_to_set_static_INFECTED + psh_forgot_to_set_static_VERTEXCOLOR + psh_forgot_to_set_static_LIGHTPASS


class pbr_model_ps51_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
	unsigned int m_nSHADOWQUALITY : 3;
	unsigned int m_nLOCAL_LIGHTS : 2;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
	bool m_bSHADOWQUALITY : 1;
	bool m_bLOCAL_LIGHTS : 1;
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

	void SetLOCAL_LIGHTS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLOCAL_LIGHTS = i;
#ifdef _DEBUG
		m_bLOCAL_LIGHTS = true;
#endif	// _DEBUG
	}

	pbr_model_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pbr_model_ps51_Block >( dx12native::kStagePixel );
		m_nPIXELFOGTYPE = 0;
		m_nSHADOWQUALITY = 0;
		m_nLOCAL_LIGHTS = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
		m_bSHADOWQUALITY = false;
		m_bLOCAL_LIGHTS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE && m_bSHADOWQUALITY && m_bLOCAL_LIGHTS );
		return ( 1 * m_nPIXELFOGTYPE ) + ( 3 * m_nSHADOWQUALITY ) + ( 15 * m_nLOCAL_LIGHTS ) + 0;
	}
};

#define shaderDynamicTest_pbr_model_ps51 psh_forgot_to_set_dynamic_PIXELFOGTYPE + psh_forgot_to_set_dynamic_SHADOWQUALITY + psh_forgot_to_set_dynamic_LOCAL_LIGHTS

