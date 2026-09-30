// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
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
#include "DebugTextureView_ps2x.h"
typedef dx12cb::DebugTextureView_ps2x debugtextureview_ps51_Block;
class debugtextureview_ps51_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nSHOWALPHA : 2;
#ifdef _DEBUG
	bool m_bSHOWALPHA : 1;
#endif	// _DEBUG
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	void SetSHOWALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSHOWALPHA = i;
#ifdef _DEBUG
		m_bSHOWALPHA = true;
#endif	// _DEBUG
	}

	debugtextureview_ps51_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
		m_nSHOWALPHA = 0;
#ifdef _DEBUG
		m_bSHOWALPHA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSHOWALPHA );
		return ( 2 * m_nCONVERT_TO_SRGB ) + ( 4 * m_nSHOWALPHA ) + 0;
	}
};

#define shaderStaticTest_debugtextureview_ps51 psh_forgot_to_set_static_SHOWALPHA


class debugtextureview_ps51_Dynamic_Index
{
	unsigned int m_nISCUBEMAP : 2;
#ifdef _DEBUG
	bool m_bISCUBEMAP : 1;
#endif	// _DEBUG
public:
	void SetISCUBEMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nISCUBEMAP = i;
#ifdef _DEBUG
		m_bISCUBEMAP = true;
#endif	// _DEBUG
	}

	debugtextureview_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< debugtextureview_ps51_Block >( dx12native::kStagePixel );
		m_nISCUBEMAP = 0;
#ifdef _DEBUG
		m_bISCUBEMAP = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bISCUBEMAP );
		return ( 1 * m_nISCUBEMAP ) + 0;
	}
};

#define shaderDynamicTest_debugtextureview_ps51 psh_forgot_to_set_dynamic_ISCUBEMAP

