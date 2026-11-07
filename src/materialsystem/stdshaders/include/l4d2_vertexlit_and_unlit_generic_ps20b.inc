// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// !$DETAILTEXTURE && ($DETAIL_BLEND_MODE || $SEAMLESS_DETAIL)
// !$FLASHLIGHT && ($FLASHLIGHTDEPTHFILTERMODE || $FLASHLIGHTSHADOWS)
// $FLASHLIGHT && ($CUBEMAP || $SELFILLUM || $ENVMAPMASK || $WRITEWATERFOGTODESTALPHA)
// $BASEALPHAENVMAPMASK && ($ENVMAPMASK || $SELFILLUM)
// $SELFILLUM_ENVMAPMASK_ALPHA && (!$ENVMAPMASK || $SELFILLUM)
// $ENVMAPFRESNEL && !$CUBEMAP
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
class l4d2_vertexlit_and_unlit_generic_ps20b_Static_Index
{
	unsigned int m_nDETAILTEXTURE : 2;
	unsigned int m_nCUBEMAP : 2;
	unsigned int m_nDIFFUSELIGHTING : 2;
	unsigned int m_nENVMAPMASK : 2;
	unsigned int m_nBASEALPHAENVMAPMASK : 2;
	unsigned int m_nSELFILLUM : 2;
	unsigned int m_nVERTEXCOLOR : 2;
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nSELFILLUM_ENVMAPMASK_ALPHA : 2;
	unsigned int m_nDETAIL_BLEND_MODE : 4;
	unsigned int m_nSEAMLESS_BASE : 2;
	unsigned int m_nSEAMLESS_DETAIL : 2;
	unsigned int m_nFLASHLIGHTDEPTHFILTERMODE : 2;
	unsigned int m_nBLENDTINTBYBASEALPHA : 2;
	unsigned int m_nENVMAPFRESNEL : 2;
	unsigned int m_nPIXELFOG : 2;
#ifdef _DEBUG
	bool m_bDETAILTEXTURE : 1;
	bool m_bCUBEMAP : 1;
	bool m_bDIFFUSELIGHTING : 1;
	bool m_bENVMAPMASK : 1;
	bool m_bBASEALPHAENVMAPMASK : 1;
	bool m_bSELFILLUM : 1;
	bool m_bVERTEXCOLOR : 1;
	bool m_bFLASHLIGHT : 1;
	bool m_bSELFILLUM_ENVMAPMASK_ALPHA : 1;
	bool m_bDETAIL_BLEND_MODE : 1;
	bool m_bSEAMLESS_BASE : 1;
	bool m_bSEAMLESS_DETAIL : 1;
	bool m_bFLASHLIGHTDEPTHFILTERMODE : 1;
	bool m_bBLENDTINTBYBASEALPHA : 1;
	bool m_bENVMAPFRESNEL : 1;
	bool m_bPIXELFOG : 1;
#endif	// _DEBUG
public:
	void SetDETAILTEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDETAILTEXTURE = i;
#ifdef _DEBUG
		m_bDETAILTEXTURE = true;
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

	void SetDIFFUSELIGHTING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDIFFUSELIGHTING = i;
#ifdef _DEBUG
		m_bDIFFUSELIGHTING = true;
#endif	// _DEBUG
	}

	void SetENVMAPMASK( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nENVMAPMASK = i;
#ifdef _DEBUG
		m_bENVMAPMASK = true;
#endif	// _DEBUG
	}

	void SetBASEALPHAENVMAPMASK( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASEALPHAENVMAPMASK = i;
#ifdef _DEBUG
		m_bBASEALPHAENVMAPMASK = true;
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

	void SetVERTEXCOLOR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVERTEXCOLOR = i;
#ifdef _DEBUG
		m_bVERTEXCOLOR = true;
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

	void SetSELFILLUM_ENVMAPMASK_ALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSELFILLUM_ENVMAPMASK_ALPHA = i;
#ifdef _DEBUG
		m_bSELFILLUM_ENVMAPMASK_ALPHA = true;
#endif	// _DEBUG
	}

	void SetDETAIL_BLEND_MODE( int i )
	{
		Assert( i >= 0 && i <= 9 );
		m_nDETAIL_BLEND_MODE = i;
#ifdef _DEBUG
		m_bDETAIL_BLEND_MODE = true;
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

	void SetFLASHLIGHTDEPTHFILTERMODE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nFLASHLIGHTDEPTHFILTERMODE = i;
#ifdef _DEBUG
		m_bFLASHLIGHTDEPTHFILTERMODE = true;
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

	void SetENVMAPFRESNEL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nENVMAPFRESNEL = i;
#ifdef _DEBUG
		m_bENVMAPFRESNEL = true;
#endif	// _DEBUG
	}

	void SetPIXELFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPIXELFOG = i;
#ifdef _DEBUG
		m_bPIXELFOG = true;
#endif	// _DEBUG
	}

	l4d2_vertexlit_and_unlit_generic_ps20b_Static_Index(  )
	{
		m_nDETAILTEXTURE = 0;
		m_nCUBEMAP = 0;
		m_nDIFFUSELIGHTING = 0;
		m_nENVMAPMASK = 0;
		m_nBASEALPHAENVMAPMASK = 0;
		m_nSELFILLUM = 0;
		m_nVERTEXCOLOR = 0;
		m_nFLASHLIGHT = 0;
		m_nSELFILLUM_ENVMAPMASK_ALPHA = 0;
		m_nDETAIL_BLEND_MODE = 0;
		m_nSEAMLESS_BASE = 0;
		m_nSEAMLESS_DETAIL = 0;
		m_nFLASHLIGHTDEPTHFILTERMODE = 0;
		m_nBLENDTINTBYBASEALPHA = 0;
		m_nENVMAPFRESNEL = 0;
		m_nPIXELFOG = 0;
#ifdef _DEBUG
		m_bDETAILTEXTURE = false;
		m_bCUBEMAP = false;
		m_bDIFFUSELIGHTING = false;
		m_bENVMAPMASK = false;
		m_bBASEALPHAENVMAPMASK = false;
		m_bSELFILLUM = false;
		m_bVERTEXCOLOR = false;
		m_bFLASHLIGHT = false;
		m_bSELFILLUM_ENVMAPMASK_ALPHA = false;
		m_bDETAIL_BLEND_MODE = false;
		m_bSEAMLESS_BASE = false;
		m_bSEAMLESS_DETAIL = false;
		m_bFLASHLIGHTDEPTHFILTERMODE = false;
		m_bBLENDTINTBYBASEALPHA = false;
		m_bENVMAPFRESNEL = false;
		m_bPIXELFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bDETAILTEXTURE && m_bCUBEMAP && m_bDIFFUSELIGHTING && m_bENVMAPMASK && m_bBASEALPHAENVMAPMASK && m_bSELFILLUM && m_bVERTEXCOLOR && m_bFLASHLIGHT && m_bSELFILLUM_ENVMAPMASK_ALPHA && m_bDETAIL_BLEND_MODE && m_bSEAMLESS_BASE && m_bSEAMLESS_DETAIL && m_bFLASHLIGHTDEPTHFILTERMODE && m_bBLENDTINTBYBASEALPHA && m_bENVMAPFRESNEL && m_bPIXELFOG );
		AssertMsg( !( !m_nDETAILTEXTURE && ( m_nDETAIL_BLEND_MODE || m_nSEAMLESS_DETAIL ) ), "Invalid combo combination ( !DETAILTEXTURE && ( DETAIL_BLEND_MODE || SEAMLESS_DETAIL ) )" );
		AssertMsg( !( m_nBASEALPHAENVMAPMASK && ( m_nENVMAPMASK || m_nSELFILLUM ) ), "Invalid combo combination ( BASEALPHAENVMAPMASK && ( ENVMAPMASK || SELFILLUM ) )" );
		AssertMsg( !( m_nSELFILLUM_ENVMAPMASK_ALPHA && ( !m_nENVMAPMASK || m_nSELFILLUM ) ), "Invalid combo combination ( SELFILLUM_ENVMAPMASK_ALPHA && ( !ENVMAPMASK || SELFILLUM ) )" );
		AssertMsg( !( m_nENVMAPFRESNEL && !m_nCUBEMAP ), "Invalid combo combination ( ENVMAPFRESNEL && !CUBEMAP )" );
		return ( 4 * m_nDETAILTEXTURE ) + ( 8 * m_nCUBEMAP ) + ( 16 * m_nDIFFUSELIGHTING ) + ( 32 * m_nENVMAPMASK ) + ( 64 * m_nBASEALPHAENVMAPMASK ) + ( 128 * m_nSELFILLUM ) + ( 256 * m_nVERTEXCOLOR ) + ( 512 * m_nFLASHLIGHT ) + ( 1024 * m_nSELFILLUM_ENVMAPMASK_ALPHA ) + ( 2048 * m_nDETAIL_BLEND_MODE ) + ( 20480 * m_nSEAMLESS_BASE ) + ( 40960 * m_nSEAMLESS_DETAIL ) + ( 81920 * m_nFLASHLIGHTDEPTHFILTERMODE ) + ( 245760 * m_nBLENDTINTBYBASEALPHA ) + ( 491520 * m_nENVMAPFRESNEL ) + ( 983040 * m_nPIXELFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_vertexlit_and_unlit_generic_ps20b psh_forgot_to_set_static_DETAILTEXTURE + psh_forgot_to_set_static_CUBEMAP + psh_forgot_to_set_static_DIFFUSELIGHTING + psh_forgot_to_set_static_ENVMAPMASK + psh_forgot_to_set_static_BASEALPHAENVMAPMASK + psh_forgot_to_set_static_SELFILLUM + psh_forgot_to_set_static_VERTEXCOLOR + psh_forgot_to_set_static_FLASHLIGHT + psh_forgot_to_set_static_SELFILLUM_ENVMAPMASK_ALPHA + psh_forgot_to_set_static_DETAIL_BLEND_MODE + psh_forgot_to_set_static_SEAMLESS_BASE + psh_forgot_to_set_static_SEAMLESS_DETAIL + psh_forgot_to_set_static_FLASHLIGHTDEPTHFILTERMODE + psh_forgot_to_set_static_BLENDTINTBYBASEALPHA + psh_forgot_to_set_static_ENVMAPFRESNEL + psh_forgot_to_set_static_PIXELFOG


class l4d2_vertexlit_and_unlit_generic_ps20b_Dynamic_Index
{
	unsigned int m_nWRITEWATERFOGTODESTALPHA : 2;
	unsigned int m_nFLASHLIGHTSHADOWS : 2;
#ifdef _DEBUG
	bool m_bWRITEWATERFOGTODESTALPHA : 1;
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

	void SetFLASHLIGHTSHADOWS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFLASHLIGHTSHADOWS = i;
#ifdef _DEBUG
		m_bFLASHLIGHTSHADOWS = true;
#endif	// _DEBUG
	}

	l4d2_vertexlit_and_unlit_generic_ps20b_Dynamic_Index(  )
	{
		m_nWRITEWATERFOGTODESTALPHA = 0;
		m_nFLASHLIGHTSHADOWS = 0;
#ifdef _DEBUG
		m_bWRITEWATERFOGTODESTALPHA = false;
		m_bFLASHLIGHTSHADOWS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bWRITEWATERFOGTODESTALPHA && m_bFLASHLIGHTSHADOWS );
		return ( 1 * m_nWRITEWATERFOGTODESTALPHA ) + ( 2 * m_nFLASHLIGHTSHADOWS ) + 0;
	}
};

#define shaderDynamicTest_l4d2_vertexlit_and_unlit_generic_ps20b psh_forgot_to_set_dynamic_WRITEWATERFOGTODESTALPHA + psh_forgot_to_set_dynamic_FLASHLIGHTSHADOWS

