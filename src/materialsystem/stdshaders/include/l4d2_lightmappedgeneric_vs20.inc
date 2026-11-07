// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// !$BUMPMAP && $DIFFUSEBUMPMAP
// $BUMPMASK && ($SEAMLESS || $DETAILTEXTURE)
// $SEAMLESS && $DETAILTEXTURE
// $LIGHTING_PREVIEW == 2
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class l4d2_lightmappedgeneric_vs20_Static_Index
{
	unsigned int m_nENVMAP_MASK : 2;
	unsigned int m_nTANGENTSPACE : 2;
	unsigned int m_nBUMPMAP : 2;
	unsigned int m_nDETAILTEXTURE : 2;
	unsigned int m_nDIFFUSEBUMPMAP : 2;
	unsigned int m_nVERTEXCOLOR : 2;
	unsigned int m_nVERTEXALPHATEXBLENDFACTOR : 2;
	unsigned int m_nSEAMLESS : 2;
	unsigned int m_nBUMPMASK : 2;
	unsigned int m_nFANCY_BLENDING : 2;
	unsigned int m_nSELFILLUM : 2;
	unsigned int m_nPIXELFOG : 2;
#ifdef _DEBUG
	bool m_bENVMAP_MASK : 1;
	bool m_bTANGENTSPACE : 1;
	bool m_bBUMPMAP : 1;
	bool m_bDETAILTEXTURE : 1;
	bool m_bDIFFUSEBUMPMAP : 1;
	bool m_bVERTEXCOLOR : 1;
	bool m_bVERTEXALPHATEXBLENDFACTOR : 1;
	bool m_bSEAMLESS : 1;
	bool m_bBUMPMASK : 1;
	bool m_bFANCY_BLENDING : 1;
	bool m_bSELFILLUM : 1;
	bool m_bPIXELFOG : 1;
#endif	// _DEBUG
public:
	void SetENVMAP_MASK( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nENVMAP_MASK = i;
#ifdef _DEBUG
		m_bENVMAP_MASK = true;
#endif	// _DEBUG
	}

	void SetTANGENTSPACE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTANGENTSPACE = i;
#ifdef _DEBUG
		m_bTANGENTSPACE = true;
#endif	// _DEBUG
	}

	void SetBUMPMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMPMAP = i;
#ifdef _DEBUG
		m_bBUMPMAP = true;
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

	void SetDIFFUSEBUMPMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDIFFUSEBUMPMAP = i;
#ifdef _DEBUG
		m_bDIFFUSEBUMPMAP = true;
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

	void SetVERTEXALPHATEXBLENDFACTOR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVERTEXALPHATEXBLENDFACTOR = i;
#ifdef _DEBUG
		m_bVERTEXALPHATEXBLENDFACTOR = true;
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

	void SetFANCY_BLENDING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFANCY_BLENDING = i;
#ifdef _DEBUG
		m_bFANCY_BLENDING = true;
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

	void SetPIXELFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPIXELFOG = i;
#ifdef _DEBUG
		m_bPIXELFOG = true;
#endif	// _DEBUG
	}

	l4d2_lightmappedgeneric_vs20_Static_Index(  )
	{
		m_nENVMAP_MASK = 0;
		m_nTANGENTSPACE = 0;
		m_nBUMPMAP = 0;
		m_nDETAILTEXTURE = 0;
		m_nDIFFUSEBUMPMAP = 0;
		m_nVERTEXCOLOR = 0;
		m_nVERTEXALPHATEXBLENDFACTOR = 0;
		m_nSEAMLESS = 0;
		m_nBUMPMASK = 0;
		m_nFANCY_BLENDING = 0;
		m_nSELFILLUM = 0;
		m_nPIXELFOG = 0;
#ifdef _DEBUG
		m_bENVMAP_MASK = false;
		m_bTANGENTSPACE = false;
		m_bBUMPMAP = false;
		m_bDETAILTEXTURE = false;
		m_bDIFFUSEBUMPMAP = false;
		m_bVERTEXCOLOR = false;
		m_bVERTEXALPHATEXBLENDFACTOR = false;
		m_bSEAMLESS = false;
		m_bBUMPMASK = false;
		m_bFANCY_BLENDING = false;
		m_bSELFILLUM = false;
		m_bPIXELFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bENVMAP_MASK && m_bTANGENTSPACE && m_bBUMPMAP && m_bDETAILTEXTURE && m_bDIFFUSEBUMPMAP && m_bVERTEXCOLOR && m_bVERTEXALPHATEXBLENDFACTOR && m_bSEAMLESS && m_bBUMPMASK && m_bFANCY_BLENDING && m_bSELFILLUM && m_bPIXELFOG );
		AssertMsg( !( !m_nBUMPMAP && m_nDIFFUSEBUMPMAP ), "Invalid combo combination ( !BUMPMAP && DIFFUSEBUMPMAP )" );
		AssertMsg( !( m_nBUMPMASK && ( m_nSEAMLESS || m_nDETAILTEXTURE ) ), "Invalid combo combination ( BUMPMASK && ( SEAMLESS || DETAILTEXTURE ) )" );
		AssertMsg( !( m_nSEAMLESS && m_nDETAILTEXTURE ), "Invalid combo combination ( SEAMLESS && DETAILTEXTURE )" );
		return ( 8 * m_nENVMAP_MASK ) + ( 16 * m_nTANGENTSPACE ) + ( 32 * m_nBUMPMAP ) + ( 64 * m_nDETAILTEXTURE ) + ( 128 * m_nDIFFUSEBUMPMAP ) + ( 256 * m_nVERTEXCOLOR ) + ( 512 * m_nVERTEXALPHATEXBLENDFACTOR ) + ( 1024 * m_nSEAMLESS ) + ( 2048 * m_nBUMPMASK ) + ( 4096 * m_nFANCY_BLENDING ) + ( 8192 * m_nSELFILLUM ) + ( 16384 * m_nPIXELFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_lightmappedgeneric_vs20 vsh_forgot_to_set_static_ENVMAP_MASK + vsh_forgot_to_set_static_TANGENTSPACE + vsh_forgot_to_set_static_BUMPMAP + vsh_forgot_to_set_static_DETAILTEXTURE + vsh_forgot_to_set_static_DIFFUSEBUMPMAP + vsh_forgot_to_set_static_VERTEXCOLOR + vsh_forgot_to_set_static_VERTEXALPHATEXBLENDFACTOR + vsh_forgot_to_set_static_SEAMLESS + vsh_forgot_to_set_static_BUMPMASK + vsh_forgot_to_set_static_FANCY_BLENDING + vsh_forgot_to_set_static_SELFILLUM + vsh_forgot_to_set_static_PIXELFOG


class l4d2_lightmappedgeneric_vs20_Dynamic_Index
{
	unsigned int m_nLIGHTING_PREVIEW : 3;
	unsigned int m_nFASTPATH : 2;
#ifdef _DEBUG
	bool m_bLIGHTING_PREVIEW : 1;
	bool m_bFASTPATH : 1;
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

	l4d2_lightmappedgeneric_vs20_Dynamic_Index(  )
	{
		m_nLIGHTING_PREVIEW = 0;
		m_nFASTPATH = 0;
#ifdef _DEBUG
		m_bLIGHTING_PREVIEW = false;
		m_bFASTPATH = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLIGHTING_PREVIEW && m_bFASTPATH );
		AssertMsg( !( m_nLIGHTING_PREVIEW == 2 ), "Invalid combo combination ( LIGHTING_PREVIEW == 2 )" );
		AssertMsg( !( 1 && ( 1 && ( m_nLIGHTING_PREVIEW && m_nFASTPATH ) ) ), "Invalid combo combination ( 1 && ( 1 && ( LIGHTING_PREVIEW && FASTPATH ) ) )" );
		return ( 1 * m_nLIGHTING_PREVIEW ) + ( 4 * m_nFASTPATH ) + 0;
	}
};

#define shaderDynamicTest_l4d2_lightmappedgeneric_vs20 vsh_forgot_to_set_dynamic_LIGHTING_PREVIEW + vsh_forgot_to_set_dynamic_FASTPATH

