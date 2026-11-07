// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// ( $SHADOWLIT == 0 ) && ( $SHADOWQUALITY != 0 )
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock dx12_detailshadowlit_vs51_Block;
class dx12_detailshadowlit_vs51_Static_Index
{
	unsigned int m_nSHADOWLIT : 2;
#ifdef _DEBUG
	bool m_bSHADOWLIT : 1;
#endif	// _DEBUG
public:
	void SetSHADOWLIT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSHADOWLIT = i;
#ifdef _DEBUG
		m_bSHADOWLIT = true;
#endif	// _DEBUG
	}

	dx12_detailshadowlit_vs51_Static_Index(  )
	{
		m_nSHADOWLIT = 0;
#ifdef _DEBUG
		m_bSHADOWLIT = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSHADOWLIT );
		return ( 8 * m_nSHADOWLIT ) + 0;
	}
};

#define shaderStaticTest_dx12_detailshadowlit_vs51 vsh_forgot_to_set_static_SHADOWLIT


class dx12_detailshadowlit_vs51_Dynamic_Index
{
	unsigned int m_nDOWATERFOG : 2;
	unsigned int m_nSHADOWQUALITY : 3;
#ifdef _DEBUG
	bool m_bDOWATERFOG : 1;
	bool m_bSHADOWQUALITY : 1;
#endif	// _DEBUG
public:
	void SetDOWATERFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDOWATERFOG = i;
#ifdef _DEBUG
		m_bDOWATERFOG = true;
#endif	// _DEBUG
	}

	void SetSHADOWQUALITY( int i )
	{
		Assert( i >= 0 && i <= 3 );
		m_nSHADOWQUALITY = i;
#ifdef _DEBUG
		m_bSHADOWQUALITY = true;
#endif	// _DEBUG
	}

	dx12_detailshadowlit_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< dx12_detailshadowlit_vs51_Block >( dx12native::kStageVertex );
		m_nDOWATERFOG = 0;
		m_nSHADOWQUALITY = 0;
#ifdef _DEBUG
		m_bDOWATERFOG = false;
		m_bSHADOWQUALITY = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bDOWATERFOG && m_bSHADOWQUALITY );
		return ( 1 * m_nDOWATERFOG ) + ( 2 * m_nSHADOWQUALITY ) + 0;
	}
};

#define shaderDynamicTest_dx12_detailshadowlit_vs51 vsh_forgot_to_set_dynamic_DOWATERFOG + vsh_forgot_to_set_dynamic_SHADOWQUALITY

