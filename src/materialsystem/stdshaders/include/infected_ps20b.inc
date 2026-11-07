// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// !$FLASHLIGHT && ( $FLASHLIGHTSHADOWS || $FLASHLIGHTDEPTHFILTERMODE )
// $FLASHLIGHT && ( $NUM_LIGHTS > 0 )
// !$DISABLEVARIATION && ( $BUMP || $SHINYBLOOD )
// $DISABLEVARIATION && ( $DETAILTEXTURE || $BURNDETAILTEXTURE || $EYEGLOW )
// $DEPTHONLY && ( $FLASHLIGHT || $DETAILTEXTURE || $BURNDETAILTEXTURE || $EYEGLOW || $DISABLEVARIATION || $SHINYBLOOD || $BUMP )
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class infected_ps20b_Static_Index
{
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nFLASHLIGHTDEPTHFILTERMODE : 2;
	unsigned int m_nDETAILTEXTURE : 2;
	unsigned int m_nWOUNDED : 2;
	unsigned int m_nDEPTHONLY : 2;
	unsigned int m_nBURNDETAILTEXTURE : 2;
	unsigned int m_nEYEGLOW : 2;
	unsigned int m_nTRANSLUCENT : 2;
	unsigned int m_nDISABLEVARIATION : 2;
	unsigned int m_nSHINYBLOOD : 2;
	unsigned int m_nBUMP : 2;
#ifdef _DEBUG
	bool m_bFLASHLIGHT : 1;
	bool m_bFLASHLIGHTDEPTHFILTERMODE : 1;
	bool m_bDETAILTEXTURE : 1;
	bool m_bWOUNDED : 1;
	bool m_bDEPTHONLY : 1;
	bool m_bBURNDETAILTEXTURE : 1;
	bool m_bEYEGLOW : 1;
	bool m_bTRANSLUCENT : 1;
	bool m_bDISABLEVARIATION : 1;
	bool m_bSHINYBLOOD : 1;
	bool m_bBUMP : 1;
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

	void SetFLASHLIGHTDEPTHFILTERMODE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nFLASHLIGHTDEPTHFILTERMODE = i;
#ifdef _DEBUG
		m_bFLASHLIGHTDEPTHFILTERMODE = true;
#endif	// _DEBUG
	}

	void SetDETAILTEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDETAILTEXTURE = i;
#ifdef _DEBUG
		m_bDETAILTEXTURE = true;
#endif	// _DEBUG
	}

	void SetWOUNDED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWOUNDED = i;
#ifdef _DEBUG
		m_bWOUNDED = true;
#endif	// _DEBUG
	}

	void SetDEPTHONLY( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDEPTHONLY = i;
#ifdef _DEBUG
		m_bDEPTHONLY = true;
#endif	// _DEBUG
	}

	void SetBURNDETAILTEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBURNDETAILTEXTURE = i;
#ifdef _DEBUG
		m_bBURNDETAILTEXTURE = true;
#endif	// _DEBUG
	}

	void SetEYEGLOW( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nEYEGLOW = i;
#ifdef _DEBUG
		m_bEYEGLOW = true;
#endif	// _DEBUG
	}

	void SetTRANSLUCENT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTRANSLUCENT = i;
#ifdef _DEBUG
		m_bTRANSLUCENT = true;
#endif	// _DEBUG
	}

	void SetDISABLEVARIATION( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDISABLEVARIATION = i;
#ifdef _DEBUG
		m_bDISABLEVARIATION = true;
#endif	// _DEBUG
	}

	void SetSHINYBLOOD( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSHINYBLOOD = i;
#ifdef _DEBUG
		m_bSHINYBLOOD = true;
#endif	// _DEBUG
	}

	void SetBUMP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMP = i;
#ifdef _DEBUG
		m_bBUMP = true;
#endif	// _DEBUG
	}

	infected_ps20b_Static_Index(  )
	{
		m_nFLASHLIGHT = 0;
		m_nFLASHLIGHTDEPTHFILTERMODE = 0;
		m_nDETAILTEXTURE = 0;
		m_nWOUNDED = 0;
		m_nDEPTHONLY = 0;
		m_nBURNDETAILTEXTURE = 0;
		m_nEYEGLOW = 0;
		m_nTRANSLUCENT = 0;
		m_nDISABLEVARIATION = 0;
		m_nSHINYBLOOD = 0;
		m_nBUMP = 0;
#ifdef _DEBUG
		m_bFLASHLIGHT = false;
		m_bFLASHLIGHTDEPTHFILTERMODE = false;
		m_bDETAILTEXTURE = false;
		m_bWOUNDED = false;
		m_bDEPTHONLY = false;
		m_bBURNDETAILTEXTURE = false;
		m_bEYEGLOW = false;
		m_bTRANSLUCENT = false;
		m_bDISABLEVARIATION = false;
		m_bSHINYBLOOD = false;
		m_bBUMP = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFLASHLIGHT && m_bFLASHLIGHTDEPTHFILTERMODE && m_bDETAILTEXTURE && m_bWOUNDED && m_bDEPTHONLY && m_bBURNDETAILTEXTURE && m_bEYEGLOW && m_bTRANSLUCENT && m_bDISABLEVARIATION && m_bSHINYBLOOD && m_bBUMP );
		AssertMsg( !( !m_nDISABLEVARIATION && ( m_nBUMP || m_nSHINYBLOOD ) ), "Invalid combo combination ( !DISABLEVARIATION && ( BUMP || SHINYBLOOD ) )" );
		AssertMsg( !( m_nDISABLEVARIATION && ( m_nDETAILTEXTURE || ( m_nBURNDETAILTEXTURE || m_nEYEGLOW ) ) ), "Invalid combo combination ( DISABLEVARIATION && ( DETAILTEXTURE || ( BURNDETAILTEXTURE || EYEGLOW ) ) )" );
		AssertMsg( !( m_nDEPTHONLY && ( m_nFLASHLIGHT || ( m_nDETAILTEXTURE || ( m_nBURNDETAILTEXTURE || ( m_nEYEGLOW || ( m_nDISABLEVARIATION || ( m_nSHINYBLOOD || m_nBUMP ) ) ) ) ) ) ), "Invalid combo combination ( DEPTHONLY && ( FLASHLIGHT || ( DETAILTEXTURE || ( BURNDETAILTEXTURE || ( EYEGLOW || ( DISABLEVARIATION || ( SHINYBLOOD || BUMP ) ) ) ) ) ) )" );
		return ( 10 * m_nFLASHLIGHT ) + ( 20 * m_nFLASHLIGHTDEPTHFILTERMODE ) + ( 60 * m_nDETAILTEXTURE ) + ( 120 * m_nWOUNDED ) + ( 240 * m_nDEPTHONLY ) + ( 480 * m_nBURNDETAILTEXTURE ) + ( 960 * m_nEYEGLOW ) + ( 1920 * m_nTRANSLUCENT ) + ( 3840 * m_nDISABLEVARIATION ) + ( 7680 * m_nSHINYBLOOD ) + ( 15360 * m_nBUMP ) + 0;
	}
};

#define shaderStaticTest_infected_ps20b psh_forgot_to_set_static_FLASHLIGHT + psh_forgot_to_set_static_FLASHLIGHTDEPTHFILTERMODE + psh_forgot_to_set_static_DETAILTEXTURE + psh_forgot_to_set_static_WOUNDED + psh_forgot_to_set_static_DEPTHONLY + psh_forgot_to_set_static_BURNDETAILTEXTURE + psh_forgot_to_set_static_EYEGLOW + psh_forgot_to_set_static_TRANSLUCENT + psh_forgot_to_set_static_DISABLEVARIATION + psh_forgot_to_set_static_SHINYBLOOD + psh_forgot_to_set_static_BUMP


class infected_ps20b_Dynamic_Index
{
	unsigned int m_nNUM_LIGHTS : 3;
	unsigned int m_nFLASHLIGHTSHADOWS : 2;
#ifdef _DEBUG
	bool m_bNUM_LIGHTS : 1;
	bool m_bFLASHLIGHTSHADOWS : 1;
#endif	// _DEBUG
public:
	void SetNUM_LIGHTS( int i )
	{
		Assert( i >= 0 && i <= 4 );
		m_nNUM_LIGHTS = i;
#ifdef _DEBUG
		m_bNUM_LIGHTS = true;
#endif	// _DEBUG
	}

	void SetFLASHLIGHTSHADOWS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHTSHADOWS = i;
#ifdef _DEBUG
		m_bFLASHLIGHTSHADOWS = true;
#endif	// _DEBUG
	}

	infected_ps20b_Dynamic_Index(  )
	{
		m_nNUM_LIGHTS = 0;
		m_nFLASHLIGHTSHADOWS = 0;
#ifdef _DEBUG
		m_bNUM_LIGHTS = false;
		m_bFLASHLIGHTSHADOWS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bNUM_LIGHTS && m_bFLASHLIGHTSHADOWS );
		return ( 1 * m_nNUM_LIGHTS ) + ( 5 * m_nFLASHLIGHTSHADOWS ) + 0;
	}
};

#define shaderDynamicTest_infected_ps20b psh_forgot_to_set_dynamic_NUM_LIGHTS + psh_forgot_to_set_dynamic_FLASHLIGHTSHADOWS

