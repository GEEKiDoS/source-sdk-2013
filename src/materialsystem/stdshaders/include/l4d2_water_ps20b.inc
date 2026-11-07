// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $CHEAP && ($BASETEXTURE || $REFLECT || $REFRACT || $FLASHLIGHT || $LIGHTMAPWATERFOG || !$ABOVEWATER)
// !$CHEAP && !$FRESNEL
// $BASETEXTURE && ($MULTITEXTURE || $FLOWMAP || $FLASHLIGHT || $LIGHTMAPWATERFOG)
// $MULTITEXTURE && ($FLOWMAP || $FLASHLIGHT || $LIGHTMAPWATERFOG)
// !$FLOWMAP && $FLOW_DEBUG
// !$FLASHLIGHT && $FLASHLIGHTSHADOWS
// !$CHEAP && $HDRENABLED
// $FLASHLIGHT && $FLOW_DEBUG
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class l4d2_water_ps20b_Static_Index
{
	unsigned int m_nCHEAP : 2;
	unsigned int m_nBASETEXTURE : 2;
	unsigned int m_nMULTITEXTURE : 2;
	unsigned int m_nREFLECT : 2;
	unsigned int m_nREFRACT : 2;
	unsigned int m_nABOVEWATER : 2;
	unsigned int m_nFLOWMAP : 2;
	unsigned int m_nFLOW_DEBUG : 2;
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nLIGHTMAPWATERFOG : 2;
	unsigned int m_nNORMAL_DECODE_MODE : 2;
	unsigned int m_nFRESNEL : 2;
#ifdef _DEBUG
	bool m_bCHEAP : 1;
	bool m_bBASETEXTURE : 1;
	bool m_bMULTITEXTURE : 1;
	bool m_bREFLECT : 1;
	bool m_bREFRACT : 1;
	bool m_bABOVEWATER : 1;
	bool m_bFLOWMAP : 1;
	bool m_bFLOW_DEBUG : 1;
	bool m_bFLASHLIGHT : 1;
	bool m_bLIGHTMAPWATERFOG : 1;
	bool m_bNORMAL_DECODE_MODE : 1;
	bool m_bFRESNEL : 1;
#endif	// _DEBUG
public:
	void SetCHEAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCHEAP = i;
#ifdef _DEBUG
		m_bCHEAP = true;
#endif	// _DEBUG
	}

	void SetBASETEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASETEXTURE = i;
#ifdef _DEBUG
		m_bBASETEXTURE = true;
#endif	// _DEBUG
	}

	void SetMULTITEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMULTITEXTURE = i;
#ifdef _DEBUG
		m_bMULTITEXTURE = true;
#endif	// _DEBUG
	}

	void SetREFLECT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nREFLECT = i;
#ifdef _DEBUG
		m_bREFLECT = true;
#endif	// _DEBUG
	}

	void SetREFRACT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nREFRACT = i;
#ifdef _DEBUG
		m_bREFRACT = true;
#endif	// _DEBUG
	}

	void SetABOVEWATER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nABOVEWATER = i;
#ifdef _DEBUG
		m_bABOVEWATER = true;
#endif	// _DEBUG
	}

	void SetFLOWMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLOWMAP = i;
#ifdef _DEBUG
		m_bFLOWMAP = true;
#endif	// _DEBUG
	}

	void SetFLOW_DEBUG( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nFLOW_DEBUG = i;
#ifdef _DEBUG
		m_bFLOW_DEBUG = true;
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

	void SetLIGHTMAPWATERFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLIGHTMAPWATERFOG = i;
#ifdef _DEBUG
		m_bLIGHTMAPWATERFOG = true;
#endif	// _DEBUG
	}

	void SetNORMAL_DECODE_MODE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nNORMAL_DECODE_MODE = i;
#ifdef _DEBUG
		m_bNORMAL_DECODE_MODE = true;
#endif	// _DEBUG
	}

	void SetFRESNEL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFRESNEL = i;
#ifdef _DEBUG
		m_bFRESNEL = true;
#endif	// _DEBUG
	}

	l4d2_water_ps20b_Static_Index(  )
	{
		m_nCHEAP = 0;
		m_nBASETEXTURE = 0;
		m_nMULTITEXTURE = 0;
		m_nREFLECT = 0;
		m_nREFRACT = 0;
		m_nABOVEWATER = 0;
		m_nFLOWMAP = 0;
		m_nFLOW_DEBUG = 0;
		m_nFLASHLIGHT = 0;
		m_nLIGHTMAPWATERFOG = 0;
		m_nNORMAL_DECODE_MODE = 0;
		m_nFRESNEL = 0;
#ifdef _DEBUG
		m_bCHEAP = false;
		m_bBASETEXTURE = false;
		m_bMULTITEXTURE = false;
		m_bREFLECT = false;
		m_bREFRACT = false;
		m_bABOVEWATER = false;
		m_bFLOWMAP = false;
		m_bFLOW_DEBUG = false;
		m_bFLASHLIGHT = false;
		m_bLIGHTMAPWATERFOG = false;
		m_bNORMAL_DECODE_MODE = false;
		m_bFRESNEL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCHEAP && m_bBASETEXTURE && m_bMULTITEXTURE && m_bREFLECT && m_bREFRACT && m_bABOVEWATER && m_bFLOWMAP && m_bFLOW_DEBUG && m_bFLASHLIGHT && m_bLIGHTMAPWATERFOG && m_bNORMAL_DECODE_MODE && m_bFRESNEL );
		AssertMsg( !( m_nCHEAP && ( m_nBASETEXTURE || ( m_nREFLECT || ( m_nREFRACT || ( m_nFLASHLIGHT || ( m_nLIGHTMAPWATERFOG || !m_nABOVEWATER ) ) ) ) ) ), "Invalid combo combination ( CHEAP && ( BASETEXTURE || ( REFLECT || ( REFRACT || ( FLASHLIGHT || ( LIGHTMAPWATERFOG || !ABOVEWATER ) ) ) ) ) )" );
		AssertMsg( !( !m_nCHEAP && !m_nFRESNEL ), "Invalid combo combination ( !CHEAP && !FRESNEL )" );
		AssertMsg( !( m_nBASETEXTURE && ( m_nMULTITEXTURE || ( m_nFLOWMAP || ( m_nFLASHLIGHT || m_nLIGHTMAPWATERFOG ) ) ) ), "Invalid combo combination ( BASETEXTURE && ( MULTITEXTURE || ( FLOWMAP || ( FLASHLIGHT || LIGHTMAPWATERFOG ) ) ) )" );
		AssertMsg( !( m_nMULTITEXTURE && ( m_nFLOWMAP || ( m_nFLASHLIGHT || m_nLIGHTMAPWATERFOG ) ) ), "Invalid combo combination ( MULTITEXTURE && ( FLOWMAP || ( FLASHLIGHT || LIGHTMAPWATERFOG ) ) )" );
		AssertMsg( !( !m_nFLOWMAP && m_nFLOW_DEBUG ), "Invalid combo combination ( !FLOWMAP && FLOW_DEBUG )" );
		AssertMsg( !( m_nFLASHLIGHT && m_nFLOW_DEBUG ), "Invalid combo combination ( FLASHLIGHT && FLOW_DEBUG )" );
		return ( 4 * m_nCHEAP ) + ( 8 * m_nBASETEXTURE ) + ( 16 * m_nMULTITEXTURE ) + ( 32 * m_nREFLECT ) + ( 64 * m_nREFRACT ) + ( 128 * m_nABOVEWATER ) + ( 256 * m_nFLOWMAP ) + ( 512 * m_nFLOW_DEBUG ) + ( 1536 * m_nFLASHLIGHT ) + ( 3072 * m_nLIGHTMAPWATERFOG ) + ( 6144 * m_nNORMAL_DECODE_MODE ) + ( 12288 * m_nFRESNEL ) + 0;
	}
};

#define shaderStaticTest_l4d2_water_ps20b psh_forgot_to_set_static_CHEAP + psh_forgot_to_set_static_BASETEXTURE + psh_forgot_to_set_static_MULTITEXTURE + psh_forgot_to_set_static_REFLECT + psh_forgot_to_set_static_REFRACT + psh_forgot_to_set_static_ABOVEWATER + psh_forgot_to_set_static_FLOWMAP + psh_forgot_to_set_static_FLOW_DEBUG + psh_forgot_to_set_static_FLASHLIGHT + psh_forgot_to_set_static_LIGHTMAPWATERFOG + psh_forgot_to_set_static_NORMAL_DECODE_MODE + psh_forgot_to_set_static_FRESNEL


class l4d2_water_ps20b_Dynamic_Index
{
	unsigned int m_nFLASHLIGHTSHADOWS : 2;
	unsigned int m_nHDRENABLED : 2;
#ifdef _DEBUG
	bool m_bFLASHLIGHTSHADOWS : 1;
	bool m_bHDRENABLED : 1;
#endif	// _DEBUG
public:
	void SetFLASHLIGHTSHADOWS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHTSHADOWS = i;
#ifdef _DEBUG
		m_bFLASHLIGHTSHADOWS = true;
#endif	// _DEBUG
	}

	void SetHDRENABLED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHDRENABLED = i;
#ifdef _DEBUG
		m_bHDRENABLED = true;
#endif	// _DEBUG
	}

	l4d2_water_ps20b_Dynamic_Index(  )
	{
		m_nFLASHLIGHTSHADOWS = 0;
		m_nHDRENABLED = 0;
#ifdef _DEBUG
		m_bFLASHLIGHTSHADOWS = false;
		m_bHDRENABLED = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFLASHLIGHTSHADOWS && m_bHDRENABLED );
		return ( 1 * m_nFLASHLIGHTSHADOWS ) + ( 2 * m_nHDRENABLED ) + 0;
	}
};

#define shaderDynamicTest_l4d2_water_ps20b psh_forgot_to_set_dynamic_FLASHLIGHTSHADOWS + psh_forgot_to_set_dynamic_HDRENABLED

