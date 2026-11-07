// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $LIGHTING_PREVIEW == 2
// $DETAIL_BLEND_MODE == 1 || $DETAIL_BLEND_MODE == 4 || $DETAIL_BLEND_MODE == 6 || $DETAIL_BLEND_MODE == 9
// !$BUMPMAP && ($BUMPMAP2 || $DIFFUSEBUMPMAP || $BUMPMASK)
// !$CUBEMAP && ($BASETEXTURENOENVMAP || $BASETEXTURE2NOENVMAP)
// !$BASETEXTURE2 && ($BASETEXTURENOENVMAP || $BASETEXTURE2NOENVMAP)
// $ENVMAPMASK && ($BUMPMAP2 || $BASEALPHAENVMAPMASK || $NORMALMAPALPHAENVMAPMASK)
// $BASEALPHAENVMAPMASK && ($SELFILLUM || $NORMALMAPALPHAENVMAPMASK)
// $BUMPMASK && ($SEAMLESS || $DETAIL_BLEND_MODE != 12 || $SELFILLUM || $BASETEXTURE2)
// $SEAMLESS && ($DETAIL_BLEND_MODE != 12 || $FANCY_BLENDING || $MASKEDBLENDING)
// !$FASTPATH && $FASTPATHENVMAPCONTRAST
// $WRITEWATERFOGTODESTALPHA && $WRITE_DEPTH_TO_DESTALPHA
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class l4d2_lightmappedgeneric_ps20b_Static_Index
{
	unsigned int m_nMASKEDBLENDING : 2;
	unsigned int m_nBASETEXTURE2 : 2;
	unsigned int m_nBUMPMAP : 2;
	unsigned int m_nBUMPMAP2 : 2;
	unsigned int m_nCUBEMAP : 2;
	unsigned int m_nENVMAPMASK : 2;
	unsigned int m_nBASEALPHAENVMAPMASK : 2;
	unsigned int m_nSELFILLUM : 2;
	unsigned int m_nNORMALMAPALPHAENVMAPMASK : 2;
	unsigned int m_nDIFFUSEBUMPMAP : 2;
	unsigned int m_nBASETEXTURENOENVMAP : 2;
	unsigned int m_nBASETEXTURE2NOENVMAP : 2;
	unsigned int m_nWARPLIGHTING : 2;
	unsigned int m_nFANCY_BLENDING : 2;
	unsigned int m_nSEAMLESS : 2;
	unsigned int m_nBUMPMASK : 2;
	unsigned int m_nDETAIL_BLEND_MODE : 4;
	unsigned int m_nBLENDTINTBYBASEALPHA : 2;
	unsigned int m_nPIXELFOG : 2;
#ifdef _DEBUG
	bool m_bMASKEDBLENDING : 1;
	bool m_bBASETEXTURE2 : 1;
	bool m_bBUMPMAP : 1;
	bool m_bBUMPMAP2 : 1;
	bool m_bCUBEMAP : 1;
	bool m_bENVMAPMASK : 1;
	bool m_bBASEALPHAENVMAPMASK : 1;
	bool m_bSELFILLUM : 1;
	bool m_bNORMALMAPALPHAENVMAPMASK : 1;
	bool m_bDIFFUSEBUMPMAP : 1;
	bool m_bBASETEXTURENOENVMAP : 1;
	bool m_bBASETEXTURE2NOENVMAP : 1;
	bool m_bWARPLIGHTING : 1;
	bool m_bFANCY_BLENDING : 1;
	bool m_bSEAMLESS : 1;
	bool m_bBUMPMASK : 1;
	bool m_bDETAIL_BLEND_MODE : 1;
	bool m_bBLENDTINTBYBASEALPHA : 1;
	bool m_bPIXELFOG : 1;
#endif	// _DEBUG
public:
	void SetMASKEDBLENDING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMASKEDBLENDING = i;
#ifdef _DEBUG
		m_bMASKEDBLENDING = true;
#endif	// _DEBUG
	}

	void SetBASETEXTURE2( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASETEXTURE2 = i;
#ifdef _DEBUG
		m_bBASETEXTURE2 = true;
#endif	// _DEBUG
	}

	void SetBUMPMAP( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nBUMPMAP = i;
#ifdef _DEBUG
		m_bBUMPMAP = true;
#endif	// _DEBUG
	}

	void SetBUMPMAP2( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMPMAP2 = i;
#ifdef _DEBUG
		m_bBUMPMAP2 = true;
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

	void SetNORMALMAPALPHAENVMAPMASK( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nNORMALMAPALPHAENVMAPMASK = i;
#ifdef _DEBUG
		m_bNORMALMAPALPHAENVMAPMASK = true;
#endif	// _DEBUG
	}

	void SetDIFFUSEBUMPMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDIFFUSEBUMPMAP = i;
#ifdef _DEBUG
		m_bDIFFUSEBUMPMAP = true;
#endif	// _DEBUG
	}

	void SetBASETEXTURENOENVMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASETEXTURENOENVMAP = i;
#ifdef _DEBUG
		m_bBASETEXTURENOENVMAP = true;
#endif	// _DEBUG
	}

	void SetBASETEXTURE2NOENVMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASETEXTURE2NOENVMAP = i;
#ifdef _DEBUG
		m_bBASETEXTURE2NOENVMAP = true;
#endif	// _DEBUG
	}

	void SetWARPLIGHTING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWARPLIGHTING = i;
#ifdef _DEBUG
		m_bWARPLIGHTING = true;
#endif	// _DEBUG
	}

	void SetFANCY_BLENDING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFANCY_BLENDING = i;
#ifdef _DEBUG
		m_bFANCY_BLENDING = true;
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

	void SetBUMPMASK( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMPMASK = i;
#ifdef _DEBUG
		m_bBUMPMASK = true;
#endif	// _DEBUG
	}

	void SetDETAIL_BLEND_MODE( int i )
	{
		Assert( i >= 0 && i <= 12 );
		m_nDETAIL_BLEND_MODE = i;
#ifdef _DEBUG
		m_bDETAIL_BLEND_MODE = true;
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

	void SetPIXELFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPIXELFOG = i;
#ifdef _DEBUG
		m_bPIXELFOG = true;
#endif	// _DEBUG
	}

	l4d2_lightmappedgeneric_ps20b_Static_Index(  )
	{
		m_nMASKEDBLENDING = 0;
		m_nBASETEXTURE2 = 0;
		m_nBUMPMAP = 0;
		m_nBUMPMAP2 = 0;
		m_nCUBEMAP = 0;
		m_nENVMAPMASK = 0;
		m_nBASEALPHAENVMAPMASK = 0;
		m_nSELFILLUM = 0;
		m_nNORMALMAPALPHAENVMAPMASK = 0;
		m_nDIFFUSEBUMPMAP = 0;
		m_nBASETEXTURENOENVMAP = 0;
		m_nBASETEXTURE2NOENVMAP = 0;
		m_nWARPLIGHTING = 0;
		m_nFANCY_BLENDING = 0;
		m_nSEAMLESS = 0;
		m_nBUMPMASK = 0;
		m_nDETAIL_BLEND_MODE = 0;
		m_nBLENDTINTBYBASEALPHA = 0;
		m_nPIXELFOG = 0;
#ifdef _DEBUG
		m_bMASKEDBLENDING = false;
		m_bBASETEXTURE2 = false;
		m_bBUMPMAP = false;
		m_bBUMPMAP2 = false;
		m_bCUBEMAP = false;
		m_bENVMAPMASK = false;
		m_bBASEALPHAENVMAPMASK = false;
		m_bSELFILLUM = false;
		m_bNORMALMAPALPHAENVMAPMASK = false;
		m_bDIFFUSEBUMPMAP = false;
		m_bBASETEXTURENOENVMAP = false;
		m_bBASETEXTURE2NOENVMAP = false;
		m_bWARPLIGHTING = false;
		m_bFANCY_BLENDING = false;
		m_bSEAMLESS = false;
		m_bBUMPMASK = false;
		m_bDETAIL_BLEND_MODE = false;
		m_bBLENDTINTBYBASEALPHA = false;
		m_bPIXELFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bMASKEDBLENDING && m_bBASETEXTURE2 && m_bBUMPMAP && m_bBUMPMAP2 && m_bCUBEMAP && m_bENVMAPMASK && m_bBASEALPHAENVMAPMASK && m_bSELFILLUM && m_bNORMALMAPALPHAENVMAPMASK && m_bDIFFUSEBUMPMAP && m_bBASETEXTURENOENVMAP && m_bBASETEXTURE2NOENVMAP && m_bWARPLIGHTING && m_bFANCY_BLENDING && m_bSEAMLESS && m_bBUMPMASK && m_bDETAIL_BLEND_MODE && m_bBLENDTINTBYBASEALPHA && m_bPIXELFOG );
		AssertMsg( !( ( m_nDETAIL_BLEND_MODE == 1 ) || ( ( m_nDETAIL_BLEND_MODE == 4 ) || ( ( m_nDETAIL_BLEND_MODE == 6 ) || ( m_nDETAIL_BLEND_MODE == 9 ) ) ) ), "Invalid combo combination ( ( DETAIL_BLEND_MODE == 1 ) || ( ( DETAIL_BLEND_MODE == 4 ) || ( ( DETAIL_BLEND_MODE == 6 ) || ( DETAIL_BLEND_MODE == 9 ) ) ) )" );
		AssertMsg( !( !m_nBUMPMAP && ( m_nBUMPMAP2 || ( m_nDIFFUSEBUMPMAP || m_nBUMPMASK ) ) ), "Invalid combo combination ( !BUMPMAP && ( BUMPMAP2 || ( DIFFUSEBUMPMAP || BUMPMASK ) ) )" );
		AssertMsg( !( !m_nCUBEMAP && ( m_nBASETEXTURENOENVMAP || m_nBASETEXTURE2NOENVMAP ) ), "Invalid combo combination ( !CUBEMAP && ( BASETEXTURENOENVMAP || BASETEXTURE2NOENVMAP ) )" );
		AssertMsg( !( !m_nBASETEXTURE2 && ( m_nBASETEXTURENOENVMAP || m_nBASETEXTURE2NOENVMAP ) ), "Invalid combo combination ( !BASETEXTURE2 && ( BASETEXTURENOENVMAP || BASETEXTURE2NOENVMAP ) )" );
		AssertMsg( !( m_nENVMAPMASK && ( m_nBUMPMAP2 || ( m_nBASEALPHAENVMAPMASK || m_nNORMALMAPALPHAENVMAPMASK ) ) ), "Invalid combo combination ( ENVMAPMASK && ( BUMPMAP2 || ( BASEALPHAENVMAPMASK || NORMALMAPALPHAENVMAPMASK ) ) )" );
		AssertMsg( !( m_nBASEALPHAENVMAPMASK && ( m_nSELFILLUM || m_nNORMALMAPALPHAENVMAPMASK ) ), "Invalid combo combination ( BASEALPHAENVMAPMASK && ( SELFILLUM || NORMALMAPALPHAENVMAPMASK ) )" );
		AssertMsg( !( m_nBUMPMASK && ( m_nSEAMLESS || ( ( m_nDETAIL_BLEND_MODE != 12 ) || ( m_nSELFILLUM || m_nBASETEXTURE2 ) ) ) ), "Invalid combo combination ( BUMPMASK && ( SEAMLESS || ( ( DETAIL_BLEND_MODE != 12 ) || ( SELFILLUM || BASETEXTURE2 ) ) ) )" );
		AssertMsg( !( m_nSEAMLESS && ( ( m_nDETAIL_BLEND_MODE != 12 ) || ( m_nFANCY_BLENDING || m_nMASKEDBLENDING ) ) ), "Invalid combo combination ( SEAMLESS && ( ( DETAIL_BLEND_MODE != 12 ) || ( FANCY_BLENDING || MASKEDBLENDING ) ) )" );
		return ( 64 * m_nMASKEDBLENDING ) + ( 128 * m_nBASETEXTURE2 ) + ( 256 * m_nBUMPMAP ) + ( 768 * m_nBUMPMAP2 ) + ( 1536 * m_nCUBEMAP ) + ( 3072 * m_nENVMAPMASK ) + ( 6144 * m_nBASEALPHAENVMAPMASK ) + ( 12288 * m_nSELFILLUM ) + ( 24576 * m_nNORMALMAPALPHAENVMAPMASK ) + ( 49152 * m_nDIFFUSEBUMPMAP ) + ( 98304 * m_nBASETEXTURENOENVMAP ) + ( 196608 * m_nBASETEXTURE2NOENVMAP ) + ( 393216 * m_nWARPLIGHTING ) + ( 786432 * m_nFANCY_BLENDING ) + ( 1572864 * m_nSEAMLESS ) + ( 3145728 * m_nBUMPMASK ) + ( 6291456 * m_nDETAIL_BLEND_MODE ) + ( 81788928 * m_nBLENDTINTBYBASEALPHA ) + ( 163577856 * m_nPIXELFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_lightmappedgeneric_ps20b psh_forgot_to_set_static_MASKEDBLENDING + psh_forgot_to_set_static_BASETEXTURE2 + psh_forgot_to_set_static_BUMPMAP + psh_forgot_to_set_static_BUMPMAP2 + psh_forgot_to_set_static_CUBEMAP + psh_forgot_to_set_static_ENVMAPMASK + psh_forgot_to_set_static_BASEALPHAENVMAPMASK + psh_forgot_to_set_static_SELFILLUM + psh_forgot_to_set_static_NORMALMAPALPHAENVMAPMASK + psh_forgot_to_set_static_DIFFUSEBUMPMAP + psh_forgot_to_set_static_BASETEXTURENOENVMAP + psh_forgot_to_set_static_BASETEXTURE2NOENVMAP + psh_forgot_to_set_static_WARPLIGHTING + psh_forgot_to_set_static_FANCY_BLENDING + psh_forgot_to_set_static_SEAMLESS + psh_forgot_to_set_static_BUMPMASK + psh_forgot_to_set_static_DETAIL_BLEND_MODE + psh_forgot_to_set_static_BLENDTINTBYBASEALPHA + psh_forgot_to_set_static_PIXELFOG


class l4d2_lightmappedgeneric_ps20b_Dynamic_Index
{
	unsigned int m_nLIGHTING_PREVIEW : 3;
	unsigned int m_nFASTPATH : 2;
	unsigned int m_nFASTPATHENVMAPCONTRAST : 2;
	unsigned int m_nWRITEWATERFOGTODESTALPHA : 2;
	unsigned int m_nWRITE_DEPTH_TO_DESTALPHA : 2;
#ifdef _DEBUG
	bool m_bLIGHTING_PREVIEW : 1;
	bool m_bFASTPATH : 1;
	bool m_bFASTPATHENVMAPCONTRAST : 1;
	bool m_bWRITEWATERFOGTODESTALPHA : 1;
	bool m_bWRITE_DEPTH_TO_DESTALPHA : 1;
#endif	// _DEBUG
public:
	void SetLIGHTING_PREVIEW( int i )
	{
		Assert( i >= 0 && i <= 3 );
		m_nLIGHTING_PREVIEW = i;
#ifdef _DEBUG
		m_bLIGHTING_PREVIEW = true;
#endif	// _DEBUG
	}

	void SetFASTPATH( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFASTPATH = i;
#ifdef _DEBUG
		m_bFASTPATH = true;
#endif	// _DEBUG
	}

	void SetFASTPATHENVMAPCONTRAST( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFASTPATHENVMAPCONTRAST = i;
#ifdef _DEBUG
		m_bFASTPATHENVMAPCONTRAST = true;
#endif	// _DEBUG
	}

	void SetWRITEWATERFOGTODESTALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWRITEWATERFOGTODESTALPHA = i;
#ifdef _DEBUG
		m_bWRITEWATERFOGTODESTALPHA = true;
#endif	// _DEBUG
	}

	void SetWRITE_DEPTH_TO_DESTALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWRITE_DEPTH_TO_DESTALPHA = i;
#ifdef _DEBUG
		m_bWRITE_DEPTH_TO_DESTALPHA = true;
#endif	// _DEBUG
	}

	l4d2_lightmappedgeneric_ps20b_Dynamic_Index(  )
	{
		m_nLIGHTING_PREVIEW = 0;
		m_nFASTPATH = 0;
		m_nFASTPATHENVMAPCONTRAST = 0;
		m_nWRITEWATERFOGTODESTALPHA = 0;
		m_nWRITE_DEPTH_TO_DESTALPHA = 0;
#ifdef _DEBUG
		m_bLIGHTING_PREVIEW = false;
		m_bFASTPATH = false;
		m_bFASTPATHENVMAPCONTRAST = false;
		m_bWRITEWATERFOGTODESTALPHA = false;
		m_bWRITE_DEPTH_TO_DESTALPHA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLIGHTING_PREVIEW && m_bFASTPATH && m_bFASTPATHENVMAPCONTRAST && m_bWRITEWATERFOGTODESTALPHA && m_bWRITE_DEPTH_TO_DESTALPHA );
		AssertMsg( !( m_nLIGHTING_PREVIEW == 2 ), "Invalid combo combination ( LIGHTING_PREVIEW == 2 )" );
		AssertMsg( !( !m_nFASTPATH && m_nFASTPATHENVMAPCONTRAST ), "Invalid combo combination ( !FASTPATH && FASTPATHENVMAPCONTRAST )" );
		AssertMsg( !( m_nWRITEWATERFOGTODESTALPHA && m_nWRITE_DEPTH_TO_DESTALPHA ), "Invalid combo combination ( WRITEWATERFOGTODESTALPHA && WRITE_DEPTH_TO_DESTALPHA )" );
		AssertMsg( !( 1 && ( 1 && ( m_nLIGHTING_PREVIEW && m_nFASTPATHENVMAPCONTRAST ) ) ), "Invalid combo combination ( 1 && ( 1 && ( LIGHTING_PREVIEW && FASTPATHENVMAPCONTRAST ) ) )" );
		AssertMsg( !( 1 && ( 1 && ( m_nLIGHTING_PREVIEW && m_nFASTPATH ) ) ), "Invalid combo combination ( 1 && ( 1 && ( LIGHTING_PREVIEW && FASTPATH ) ) )" );
		return ( 1 * m_nLIGHTING_PREVIEW ) + ( 4 * m_nFASTPATH ) + ( 8 * m_nFASTPATHENVMAPCONTRAST ) + ( 16 * m_nWRITEWATERFOGTODESTALPHA ) + ( 32 * m_nWRITE_DEPTH_TO_DESTALPHA ) + 0;
	}
};

#define shaderDynamicTest_l4d2_lightmappedgeneric_ps20b psh_forgot_to_set_dynamic_LIGHTING_PREVIEW + psh_forgot_to_set_dynamic_FASTPATH + psh_forgot_to_set_dynamic_FASTPATHENVMAPCONTRAST + psh_forgot_to_set_dynamic_WRITEWATERFOGTODESTALPHA + psh_forgot_to_set_dynamic_WRITE_DEPTH_TO_DESTALPHA

