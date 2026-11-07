// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $USE_STATIC_CONTROL_FLOW && ($NUM_LIGHTS > 0)
// $SEPARATE_DETAIL_UVS && $SEAMLESS_DETAIL
// $TREESWAY && ($SEAMLESS_DETAIL || $SEAMLESS_BASE)
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class l4d2_vertexlit_and_unlit_generic_vs20_Static_Index
{
	unsigned int m_nVERTEXCOLOR : 2;
	unsigned int m_nCUBEMAP : 2;
	unsigned int m_nHALFLAMBERT : 2;
	unsigned int m_nFLASHLIGHT : 2;
	unsigned int m_nSEAMLESS_BASE : 2;
	unsigned int m_nSEAMLESS_DETAIL : 2;
	unsigned int m_nSEPARATE_DETAIL_UVS : 2;
	unsigned int m_nUSE_STATIC_CONTROL_FLOW : 2;
	unsigned int m_nTREESWAY : 2;
	unsigned int m_nPIXELFOG : 2;
#ifdef _DEBUG
	bool m_bVERTEXCOLOR : 1;
	bool m_bCUBEMAP : 1;
	bool m_bHALFLAMBERT : 1;
	bool m_bFLASHLIGHT : 1;
	bool m_bSEAMLESS_BASE : 1;
	bool m_bSEAMLESS_DETAIL : 1;
	bool m_bSEPARATE_DETAIL_UVS : 1;
	bool m_bUSE_STATIC_CONTROL_FLOW : 1;
	bool m_bTREESWAY : 1;
	bool m_bPIXELFOG : 1;
#endif	// _DEBUG
public:
	void SetVERTEXCOLOR( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVERTEXCOLOR = i;
#ifdef _DEBUG
		m_bVERTEXCOLOR = true;
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

	void SetHALFLAMBERT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHALFLAMBERT = i;
#ifdef _DEBUG
		m_bHALFLAMBERT = true;
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

	void SetSEPARATE_DETAIL_UVS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEPARATE_DETAIL_UVS = i;
#ifdef _DEBUG
		m_bSEPARATE_DETAIL_UVS = true;
#endif	// _DEBUG
	}

	void SetUSE_STATIC_CONTROL_FLOW( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nUSE_STATIC_CONTROL_FLOW = i;
#ifdef _DEBUG
		m_bUSE_STATIC_CONTROL_FLOW = true;
#endif	// _DEBUG
	}

	void SetTREESWAY( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nTREESWAY = i;
#ifdef _DEBUG
		m_bTREESWAY = true;
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

	l4d2_vertexlit_and_unlit_generic_vs20_Static_Index(  )
	{
		m_nVERTEXCOLOR = 0;
		m_nCUBEMAP = 0;
		m_nHALFLAMBERT = 0;
		m_nFLASHLIGHT = 0;
		m_nSEAMLESS_BASE = 0;
		m_nSEAMLESS_DETAIL = 0;
		m_nSEPARATE_DETAIL_UVS = 0;
		m_nUSE_STATIC_CONTROL_FLOW = 0;
		m_nTREESWAY = 0;
		m_nPIXELFOG = 0;
#ifdef _DEBUG
		m_bVERTEXCOLOR = false;
		m_bCUBEMAP = false;
		m_bHALFLAMBERT = false;
		m_bFLASHLIGHT = false;
		m_bSEAMLESS_BASE = false;
		m_bSEAMLESS_DETAIL = false;
		m_bSEPARATE_DETAIL_UVS = false;
		m_bUSE_STATIC_CONTROL_FLOW = false;
		m_bTREESWAY = false;
		m_bPIXELFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bVERTEXCOLOR && m_bCUBEMAP && m_bHALFLAMBERT && m_bFLASHLIGHT && m_bSEAMLESS_BASE && m_bSEAMLESS_DETAIL && m_bSEPARATE_DETAIL_UVS && m_bUSE_STATIC_CONTROL_FLOW && m_bTREESWAY && m_bPIXELFOG );
		AssertMsg( !( m_nSEPARATE_DETAIL_UVS && m_nSEAMLESS_DETAIL ), "Invalid combo combination ( SEPARATE_DETAIL_UVS && SEAMLESS_DETAIL )" );
		AssertMsg( !( m_nTREESWAY && ( m_nSEAMLESS_DETAIL || m_nSEAMLESS_BASE ) ), "Invalid combo combination ( TREESWAY && ( SEAMLESS_DETAIL || SEAMLESS_BASE ) )" );
		return ( 48 * m_nVERTEXCOLOR ) + ( 96 * m_nCUBEMAP ) + ( 192 * m_nHALFLAMBERT ) + ( 384 * m_nFLASHLIGHT ) + ( 768 * m_nSEAMLESS_BASE ) + ( 1536 * m_nSEAMLESS_DETAIL ) + ( 3072 * m_nSEPARATE_DETAIL_UVS ) + ( 6144 * m_nUSE_STATIC_CONTROL_FLOW ) + ( 12288 * m_nTREESWAY ) + ( 36864 * m_nPIXELFOG ) + 0;
	}
};

#define shaderStaticTest_l4d2_vertexlit_and_unlit_generic_vs20 vsh_forgot_to_set_static_VERTEXCOLOR + vsh_forgot_to_set_static_CUBEMAP + vsh_forgot_to_set_static_HALFLAMBERT + vsh_forgot_to_set_static_FLASHLIGHT + vsh_forgot_to_set_static_SEAMLESS_BASE + vsh_forgot_to_set_static_SEAMLESS_DETAIL + vsh_forgot_to_set_static_SEPARATE_DETAIL_UVS + vsh_forgot_to_set_static_USE_STATIC_CONTROL_FLOW + vsh_forgot_to_set_static_TREESWAY + vsh_forgot_to_set_static_PIXELFOG


class l4d2_vertexlit_and_unlit_generic_vs20_Dynamic_Index
{
	unsigned int m_nCOMPRESSED_VERTS : 2;
	unsigned int m_nDYNAMIC_LIGHT : 2;
	unsigned int m_nSTATIC_LIGHT_VERTEX : 2;
	unsigned int m_nSKINNING : 2;
	unsigned int m_nNUM_LIGHTS : 2;
#ifdef _DEBUG
	bool m_bCOMPRESSED_VERTS : 1;
	bool m_bDYNAMIC_LIGHT : 1;
	bool m_bSTATIC_LIGHT_VERTEX : 1;
	bool m_bSKINNING : 1;
	bool m_bNUM_LIGHTS : 1;
#endif	// _DEBUG
public:
	void SetCOMPRESSED_VERTS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCOMPRESSED_VERTS = i;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = true;
#endif	// _DEBUG
	}

	void SetDYNAMIC_LIGHT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDYNAMIC_LIGHT = i;
#ifdef _DEBUG
		m_bDYNAMIC_LIGHT = true;
#endif	// _DEBUG
	}

	void SetSTATIC_LIGHT_VERTEX( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSTATIC_LIGHT_VERTEX = i;
#ifdef _DEBUG
		m_bSTATIC_LIGHT_VERTEX = true;
#endif	// _DEBUG
	}

	void SetSKINNING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSKINNING = i;
#ifdef _DEBUG
		m_bSKINNING = true;
#endif	// _DEBUG
	}

	void SetNUM_LIGHTS( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nNUM_LIGHTS = i;
#ifdef _DEBUG
		m_bNUM_LIGHTS = true;
#endif	// _DEBUG
	}

	l4d2_vertexlit_and_unlit_generic_vs20_Dynamic_Index(  )
	{
		m_nCOMPRESSED_VERTS = 0;
		m_nDYNAMIC_LIGHT = 0;
		m_nSTATIC_LIGHT_VERTEX = 0;
		m_nSKINNING = 0;
		m_nNUM_LIGHTS = 0;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = false;
		m_bDYNAMIC_LIGHT = false;
		m_bSTATIC_LIGHT_VERTEX = false;
		m_bSKINNING = false;
		m_bNUM_LIGHTS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCOMPRESSED_VERTS && m_bDYNAMIC_LIGHT && m_bSTATIC_LIGHT_VERTEX && m_bSKINNING && m_bNUM_LIGHTS );
		return ( 1 * m_nCOMPRESSED_VERTS ) + ( 2 * m_nDYNAMIC_LIGHT ) + ( 4 * m_nSTATIC_LIGHT_VERTEX ) + ( 8 * m_nSKINNING ) + ( 16 * m_nNUM_LIGHTS ) + 0;
	}
};

#define shaderDynamicTest_l4d2_vertexlit_and_unlit_generic_vs20 vsh_forgot_to_set_dynamic_COMPRESSED_VERTS + vsh_forgot_to_set_dynamic_DYNAMIC_LIGHT + vsh_forgot_to_set_dynamic_STATIC_LIGHT_VERTEX + vsh_forgot_to_set_dynamic_SKINNING + vsh_forgot_to_set_dynamic_NUM_LIGHTS

