// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// (!$DETAILTEXTURE) && ($DETAIL_BLEND_MODE != 0)
// (!$FLASHLIGHT) && ($FLASHLIGHTSHADOWS || $FLASHLIGHTDEPTHFILTERMODE)
// (!$SELFILLUM) && $SELFILLUMFRESNEL
// $FLASHLIGHT && ($SELFILLUM || $CUBEMAP || $NUM_LIGHTS || $LIGHTWARPTEXTURE || $WRITEWATERFOGTODESTALPHA)
// $BLENDTINTBYBASEALPHA && $SELFILLUM
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
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class l4d2_phong_ps20b_Static_Index
{
	unsigned int m_nHALFLAMBERT : 2;
	unsigned int m_nCUBEMAP : 2;
	unsigned int m_nSELFILLUM : 2;
	unsigned int m_nSELFILLUMFRESNEL : 2;
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nLIGHTWARPTEXTURE : 2;
	unsigned int m_nPHONGWARPTEXTURE : 1;
	unsigned int m_nWRINKLEMAP : 2;
	unsigned int m_nDETAIL_BLEND_MODE : 3;
	unsigned int m_nDETAILTEXTURE : 2;
	unsigned int m_nWOUNDED : 1;
	unsigned int m_nRIMLIGHT : 1;
	unsigned int m_nFLASHLIGHTDEPTHFILTERMODE : 2;
	unsigned int m_nSHADER_SRGB_READ : 1;
	unsigned int m_nBLENDTINTBYBASEALPHA : 2;
	unsigned int m_nSHINYBLOOD : 2;
	unsigned int m_nWORLD_NORMAL : 1;
#ifdef _DEBUG
	bool m_bHALFLAMBERT : 1;
	bool m_bCUBEMAP : 1;
	bool m_bSELFILLUM : 1;
	bool m_bSELFILLUMFRESNEL : 1;
	bool m_bFLASHLIGHT : 1;
	bool m_bLIGHTWARPTEXTURE : 1;
	bool m_bPHONGWARPTEXTURE : 1;
	bool m_bWRINKLEMAP : 1;
	bool m_bDETAIL_BLEND_MODE : 1;
	bool m_bDETAILTEXTURE : 1;
	bool m_bWOUNDED : 1;
	bool m_bRIMLIGHT : 1;
	bool m_bFLASHLIGHTDEPTHFILTERMODE : 1;
	bool m_bSHADER_SRGB_READ : 1;
	bool m_bBLENDTINTBYBASEALPHA : 1;
	bool m_bSHINYBLOOD : 1;
	bool m_bWORLD_NORMAL : 1;
#endif	// _DEBUG
public:
	void SetHALFLAMBERT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHALFLAMBERT = i;
#ifdef _DEBUG
		m_bHALFLAMBERT = true;
#endif	// _DEBUG
	}

	void SetCUBEMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCUBEMAP = i;
#ifdef _DEBUG
		m_bCUBEMAP = true;
#endif	// _DEBUG
	}

	void SetSELFILLUM( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSELFILLUM = i;
#ifdef _DEBUG
		m_bSELFILLUM = true;
#endif	// _DEBUG
	}

	void SetSELFILLUMFRESNEL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSELFILLUMFRESNEL = i;
#ifdef _DEBUG
		m_bSELFILLUMFRESNEL = true;
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

	void SetLIGHTWARPTEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLIGHTWARPTEXTURE = i;
#ifdef _DEBUG
		m_bLIGHTWARPTEXTURE = true;
#endif	// _DEBUG
	}

	void SetPHONGWARPTEXTURE( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nPHONGWARPTEXTURE = i;
#ifdef _DEBUG
		m_bPHONGWARPTEXTURE = true;
#endif	// _DEBUG
	}

	void SetWRINKLEMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWRINKLEMAP = i;
#ifdef _DEBUG
		m_bWRINKLEMAP = true;
#endif	// _DEBUG
	}

	void SetDETAIL_BLEND_MODE( int i )
	{
		Assert( i >= 0 && i <= 6 );
		m_nDETAIL_BLEND_MODE = i;
#ifdef _DEBUG
		m_bDETAIL_BLEND_MODE = true;
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
		Assert( i >= 0 && i <= 0 );
		m_nWOUNDED = i;
#ifdef _DEBUG
		m_bWOUNDED = true;
#endif	// _DEBUG
	}

	void SetRIMLIGHT( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nRIMLIGHT = i;
#ifdef _DEBUG
		m_bRIMLIGHT = true;
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

	void SetSHADER_SRGB_READ( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nSHADER_SRGB_READ = i;
#ifdef _DEBUG
		m_bSHADER_SRGB_READ = true;
#endif	// _DEBUG
	}

	void SetBLENDTINTBYBASEALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBLENDTINTBYBASEALPHA = i;
#ifdef _DEBUG
		m_bBLENDTINTBYBASEALPHA = true;
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

	void SetWORLD_NORMAL( int i )
	{
		Assert( i >= 0 && i <= 0 );
		m_nWORLD_NORMAL = i;
#ifdef _DEBUG
		m_bWORLD_NORMAL = true;
#endif	// _DEBUG
	}

	l4d2_phong_ps20b_Static_Index(  )
	{
		m_nHALFLAMBERT = 0;
		m_nCUBEMAP = 0;
		m_nSELFILLUM = 0;
		m_nSELFILLUMFRESNEL = 0;
		m_nFLASHLIGHT = 0;
		m_nLIGHTWARPTEXTURE = 0;
		m_nPHONGWARPTEXTURE = 0;
		m_nWRINKLEMAP = 0;
		m_nDETAIL_BLEND_MODE = 0;
		m_nDETAILTEXTURE = 0;
		m_nWOUNDED = 0;
		m_nRIMLIGHT = 0;
		m_nFLASHLIGHTDEPTHFILTERMODE = 0;
		m_nSHADER_SRGB_READ = 0;
		m_nBLENDTINTBYBASEALPHA = 0;
		m_nSHINYBLOOD = 0;
		m_nWORLD_NORMAL = 0;
#ifdef _DEBUG
		m_bHALFLAMBERT = false;
		m_bCUBEMAP = false;
		m_bSELFILLUM = false;
		m_bSELFILLUMFRESNEL = false;
		m_bFLASHLIGHT = false;
		m_bLIGHTWARPTEXTURE = false;
		m_bPHONGWARPTEXTURE = false;
		m_bWRINKLEMAP = false;
		m_bDETAIL_BLEND_MODE = false;
		m_bDETAILTEXTURE = false;
		m_bWOUNDED = false;
		m_bRIMLIGHT = false;
		m_bFLASHLIGHTDEPTHFILTERMODE = false;
		m_bSHADER_SRGB_READ = false;
		m_bBLENDTINTBYBASEALPHA = false;
		m_bSHINYBLOOD = false;
		m_bWORLD_NORMAL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bHALFLAMBERT && m_bCUBEMAP && m_bSELFILLUM && m_bSELFILLUMFRESNEL && m_bFLASHLIGHT && m_bLIGHTWARPTEXTURE && m_bPHONGWARPTEXTURE && m_bWRINKLEMAP && m_bDETAIL_BLEND_MODE && m_bDETAILTEXTURE && m_bWOUNDED && m_bRIMLIGHT && m_bFLASHLIGHTDEPTHFILTERMODE && m_bSHADER_SRGB_READ && m_bBLENDTINTBYBASEALPHA && m_bSHINYBLOOD && m_bWORLD_NORMAL );
		AssertMsg( !( !m_nDETAILTEXTURE && ( m_nDETAIL_BLEND_MODE != 0 ) ), "Invalid combo combination ( !DETAILTEXTURE && ( DETAIL_BLEND_MODE != 0 ) )" );
		AssertMsg( !( !m_nSELFILLUM && m_nSELFILLUMFRESNEL ), "Invalid combo combination ( !SELFILLUM && SELFILLUMFRESNEL )" );
		AssertMsg( !( m_nBLENDTINTBYBASEALPHA && m_nSELFILLUM ), "Invalid combo combination ( BLENDTINTBYBASEALPHA && SELFILLUM )" );
		return ( 20 * m_nHALFLAMBERT ) + ( 40 * m_nCUBEMAP ) + ( 80 * m_nSELFILLUM ) + ( 160 * m_nSELFILLUMFRESNEL ) + ( 320 * m_nFLASHLIGHT ) + ( 640 * m_nLIGHTWARPTEXTURE ) + ( 1280 * m_nPHONGWARPTEXTURE ) + ( 1280 * m_nWRINKLEMAP ) + ( 2560 * m_nDETAIL_BLEND_MODE ) + ( 17920 * m_nDETAILTEXTURE ) + ( 35840 * m_nWOUNDED ) + ( 35840 * m_nRIMLIGHT ) + ( 35840 * m_nFLASHLIGHTDEPTHFILTERMODE ) + ( 107520 * m_nSHADER_SRGB_READ ) + ( 107520 * m_nBLENDTINTBYBASEALPHA ) + ( 215040 * m_nSHINYBLOOD ) + ( 430080 * m_nWORLD_NORMAL ) + 0;
	}
};

#define shaderStaticTest_l4d2_phong_ps20b psh_forgot_to_set_static_HALFLAMBERT + psh_forgot_to_set_static_CUBEMAP + psh_forgot_to_set_static_SELFILLUM + psh_forgot_to_set_static_SELFILLUMFRESNEL + psh_forgot_to_set_static_FLASHLIGHT + psh_forgot_to_set_static_LIGHTWARPTEXTURE + psh_forgot_to_set_static_PHONGWARPTEXTURE + psh_forgot_to_set_static_WRINKLEMAP + psh_forgot_to_set_static_DETAIL_BLEND_MODE + psh_forgot_to_set_static_DETAILTEXTURE + psh_forgot_to_set_static_WOUNDED + psh_forgot_to_set_static_RIMLIGHT + psh_forgot_to_set_static_FLASHLIGHTDEPTHFILTERMODE + psh_forgot_to_set_static_SHADER_SRGB_READ + psh_forgot_to_set_static_BLENDTINTBYBASEALPHA + psh_forgot_to_set_static_SHINYBLOOD + psh_forgot_to_set_static_WORLD_NORMAL


class l4d2_phong_ps20b_Dynamic_Index
{
	unsigned int m_nWRITEWATERFOGTODESTALPHA : 2;
	unsigned int m_nNUM_LIGHTS : 3;
	unsigned int m_nFLASHLIGHTSHADOWS : 2;
#ifdef _DEBUG
	bool m_bWRITEWATERFOGTODESTALPHA : 1;
	bool m_bNUM_LIGHTS : 1;
	bool m_bFLASHLIGHTSHADOWS : 1;
#endif	// _DEBUG
public:
	void SetWRITEWATERFOGTODESTALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWRITEWATERFOGTODESTALPHA = i;
#ifdef _DEBUG
		m_bWRITEWATERFOGTODESTALPHA = true;
#endif	// _DEBUG
	}

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

	l4d2_phong_ps20b_Dynamic_Index(  )
	{
		m_nWRITEWATERFOGTODESTALPHA = 0;
		m_nNUM_LIGHTS = 0;
		m_nFLASHLIGHTSHADOWS = 0;
#ifdef _DEBUG
		m_bWRITEWATERFOGTODESTALPHA = false;
		m_bNUM_LIGHTS = false;
		m_bFLASHLIGHTSHADOWS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bWRITEWATERFOGTODESTALPHA && m_bNUM_LIGHTS && m_bFLASHLIGHTSHADOWS );
		return ( 1 * m_nWRITEWATERFOGTODESTALPHA ) + ( 2 * m_nNUM_LIGHTS ) + ( 10 * m_nFLASHLIGHTSHADOWS ) + 0;
	}
};

#define shaderDynamicTest_l4d2_phong_ps20b psh_forgot_to_set_dynamic_WRITEWATERFOGTODESTALPHA + psh_forgot_to_set_dynamic_NUM_LIGHTS + psh_forgot_to_set_dynamic_FLASHLIGHTSHADOWS

