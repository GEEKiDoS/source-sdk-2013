// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $USE_STATIC_CONTROL_FLOW && ( $NUM_LIGHTS > 0 )
// $DEPTHONLY && ( $BUMP || $HALFLAMBERT || $STATICLIGHT )
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class infected_vs20_Static_Index
{
	unsigned int m_nWOUNDED : 2;
	unsigned int m_nDEPTHONLY : 2;
	unsigned int m_nHALFLAMBERT : 2;
	unsigned int m_nUSE_STATIC_CONTROL_FLOW : 2;
	unsigned int m_nBUMP : 2;
#ifdef _DEBUG
	bool m_bWOUNDED : 1;
	bool m_bDEPTHONLY : 1;
	bool m_bHALFLAMBERT : 1;
	bool m_bUSE_STATIC_CONTROL_FLOW : 1;
	bool m_bBUMP : 1;
#endif	// _DEBUG
public:
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

	void SetHALFLAMBERT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHALFLAMBERT = i;
#ifdef _DEBUG
		m_bHALFLAMBERT = true;
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

	void SetBUMP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMP = i;
#ifdef _DEBUG
		m_bBUMP = true;
#endif	// _DEBUG
	}

	infected_vs20_Static_Index(  )
	{
		m_nWOUNDED = 0;
		m_nDEPTHONLY = 0;
		m_nHALFLAMBERT = 0;
		m_nUSE_STATIC_CONTROL_FLOW = 0;
		m_nBUMP = 0;
#ifdef _DEBUG
		m_bWOUNDED = false;
		m_bDEPTHONLY = false;
		m_bHALFLAMBERT = false;
		m_bUSE_STATIC_CONTROL_FLOW = false;
		m_bBUMP = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bWOUNDED && m_bDEPTHONLY && m_bHALFLAMBERT && m_bUSE_STATIC_CONTROL_FLOW && m_bBUMP );
		return ( 80 * m_nWOUNDED ) + ( 160 * m_nDEPTHONLY ) + ( 320 * m_nHALFLAMBERT ) + ( 640 * m_nUSE_STATIC_CONTROL_FLOW ) + ( 1280 * m_nBUMP ) + 0;
	}
};

#define shaderStaticTest_infected_vs20 vsh_forgot_to_set_static_WOUNDED + vsh_forgot_to_set_static_DEPTHONLY + vsh_forgot_to_set_static_HALFLAMBERT + vsh_forgot_to_set_static_USE_STATIC_CONTROL_FLOW + vsh_forgot_to_set_static_BUMP


class infected_vs20_Dynamic_Index
{
	unsigned int m_nSKINNING : 2;
	unsigned int m_nCOMPRESSED_VERTS : 2;
	unsigned int m_nSTATICLIGHT : 2;
	unsigned int m_nNUM_LIGHTS : 3;
	unsigned int m_nSCENEFOG : 2;
#ifdef _DEBUG
	bool m_bSKINNING : 1;
	bool m_bCOMPRESSED_VERTS : 1;
	bool m_bSTATICLIGHT : 1;
	bool m_bNUM_LIGHTS : 1;
	bool m_bSCENEFOG : 1;
#endif	// _DEBUG
public:
	void SetSKINNING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSKINNING = i;
#ifdef _DEBUG
		m_bSKINNING = true;
#endif	// _DEBUG
	}

	void SetCOMPRESSED_VERTS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCOMPRESSED_VERTS = i;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = true;
#endif	// _DEBUG
	}

	void SetSTATICLIGHT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSTATICLIGHT = i;
#ifdef _DEBUG
		m_bSTATICLIGHT = true;
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

	void SetSCENEFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSCENEFOG = i;
#ifdef _DEBUG
		m_bSCENEFOG = true;
#endif	// _DEBUG
	}

	infected_vs20_Dynamic_Index(  )
	{
		m_nSKINNING = 0;
		m_nCOMPRESSED_VERTS = 0;
		m_nSTATICLIGHT = 0;
		m_nNUM_LIGHTS = 0;
		m_nSCENEFOG = 0;
#ifdef _DEBUG
		m_bSKINNING = false;
		m_bCOMPRESSED_VERTS = false;
		m_bSTATICLIGHT = false;
		m_bNUM_LIGHTS = false;
		m_bSCENEFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSKINNING && m_bCOMPRESSED_VERTS && m_bSTATICLIGHT && m_bNUM_LIGHTS && m_bSCENEFOG );
		return ( 1 * m_nSKINNING ) + ( 2 * m_nCOMPRESSED_VERTS ) + ( 4 * m_nSTATICLIGHT ) + ( 8 * m_nNUM_LIGHTS ) + ( 40 * m_nSCENEFOG ) + 0;
	}
};

#define shaderDynamicTest_infected_vs20 vsh_forgot_to_set_dynamic_SKINNING + vsh_forgot_to_set_dynamic_COMPRESSED_VERTS + vsh_forgot_to_set_dynamic_STATICLIGHT + vsh_forgot_to_set_dynamic_NUM_LIGHTS + vsh_forgot_to_set_dynamic_SCENEFOG

