// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock portalstaticoverlay_vs51_Block;
class portalstaticoverlay_vs51_Static_Index
{
	unsigned int m_nMODEL : 2;
#ifdef _DEBUG
	bool m_bMODEL : 1;
#endif	// _DEBUG
public:
	void SetMODEL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMODEL = i;
#ifdef _DEBUG
		m_bMODEL = true;
#endif	// _DEBUG
	}

	portalstaticoverlay_vs51_Static_Index(  )
	{
		m_nMODEL = 0;
#ifdef _DEBUG
		m_bMODEL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bMODEL );
		return ( 2 * m_nMODEL ) + 0;
	}
};

#define shaderStaticTest_portalstaticoverlay_vs51 vsh_forgot_to_set_static_MODEL


class portalstaticoverlay_vs51_Dynamic_Index
{
	unsigned int m_nSKINNING : 2;
#ifdef _DEBUG
	bool m_bSKINNING : 1;
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

	portalstaticoverlay_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< portalstaticoverlay_vs51_Block >( dx12native::kStageVertex );
		m_nSKINNING = 0;
#ifdef _DEBUG
		m_bSKINNING = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSKINNING );
		return ( 1 * m_nSKINNING ) + 0;
	}
};

#define shaderDynamicTest_portalstaticoverlay_vs51 vsh_forgot_to_set_dynamic_SKINNING

