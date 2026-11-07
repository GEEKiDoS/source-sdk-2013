// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $SPLINE && ( $ANIMBLEND || $PERPARTICLEOUTLINE || $ORIENTATION == 3 )
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class terror_spritecard_vs20_Static_Index
{
	unsigned int m_nANIMBLEND : 2;
	unsigned int m_nPERPARTICLEOUTLINE : 2;
	unsigned int m_nSPRITECARDVERTEXFOG : 2;
	unsigned int m_nSPLINE : 2;
#ifdef _DEBUG
	bool m_bANIMBLEND : 1;
	bool m_bPERPARTICLEOUTLINE : 1;
	bool m_bSPRITECARDVERTEXFOG : 1;
	bool m_bSPLINE : 1;
#endif	// _DEBUG
public:
	void SetANIMBLEND( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nANIMBLEND = i;
#ifdef _DEBUG
		m_bANIMBLEND = true;
#endif	// _DEBUG
	}

	void SetPERPARTICLEOUTLINE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPERPARTICLEOUTLINE = i;
#ifdef _DEBUG
		m_bPERPARTICLEOUTLINE = true;
#endif	// _DEBUG
	}

	void SetSPRITECARDVERTEXFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSPRITECARDVERTEXFOG = i;
#ifdef _DEBUG
		m_bSPRITECARDVERTEXFOG = true;
#endif	// _DEBUG
	}

	void SetSPLINE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSPLINE = i;
#ifdef _DEBUG
		m_bSPLINE = true;
#endif	// _DEBUG
	}

	terror_spritecard_vs20_Static_Index(  )
	{
		m_nANIMBLEND = 0;
		m_nPERPARTICLEOUTLINE = 0;
		m_nSPRITECARDVERTEXFOG = 0;
		m_nSPLINE = 0;
#ifdef _DEBUG
		m_bANIMBLEND = false;
		m_bPERPARTICLEOUTLINE = false;
		m_bSPRITECARDVERTEXFOG = false;
		m_bSPLINE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bANIMBLEND && m_bPERPARTICLEOUTLINE && m_bSPRITECARDVERTEXFOG && m_bSPLINE );
		return ( 4 * m_nANIMBLEND ) + ( 8 * m_nPERPARTICLEOUTLINE ) + ( 16 * m_nSPRITECARDVERTEXFOG ) + ( 32 * m_nSPLINE ) + 0;
	}
};

#define shaderStaticTest_terror_spritecard_vs20 vsh_forgot_to_set_static_ANIMBLEND + vsh_forgot_to_set_static_PERPARTICLEOUTLINE + vsh_forgot_to_set_static_SPRITECARDVERTEXFOG + vsh_forgot_to_set_static_SPLINE


class terror_spritecard_vs20_Dynamic_Index
{
	unsigned int m_nORIENTATION : 3;
#ifdef _DEBUG
	bool m_bORIENTATION : 1;
#endif	// _DEBUG
public:
	void SetORIENTATION( int i )
	{
		Assert( i >= 0 && i <= 3 );
		m_nORIENTATION = i;
#ifdef _DEBUG
		m_bORIENTATION = true;
#endif	// _DEBUG
	}

	terror_spritecard_vs20_Dynamic_Index(  )
	{
		m_nORIENTATION = 0;
#ifdef _DEBUG
		m_bORIENTATION = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bORIENTATION );
		return ( 1 * m_nORIENTATION ) + 0;
	}
};

#define shaderDynamicTest_terror_spritecard_vs20 vsh_forgot_to_set_dynamic_ORIENTATION

