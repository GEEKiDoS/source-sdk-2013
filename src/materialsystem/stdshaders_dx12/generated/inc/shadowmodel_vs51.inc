// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "shadowmodel_vs20.h"
typedef dx12cb::shadowmodel_vs20 shadowmodel_vs51_Block;
class shadowmodel_vs51_Static_Index
{
public:
	shadowmodel_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_shadowmodel_vs51 1


class shadowmodel_vs51_Dynamic_Index
{
	unsigned int m_nDOWATERFOG : 2;
	unsigned int m_nSKINNING : 2;
#ifdef _DEBUG
	bool m_bDOWATERFOG : 1;
	bool m_bSKINNING : 1;
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

	void SetSKINNING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSKINNING = i;
#ifdef _DEBUG
		m_bSKINNING = true;
#endif	// _DEBUG
	}

	shadowmodel_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< shadowmodel_vs51_Block >( dx12native::kStageVertex );
		m_nDOWATERFOG = 0;
		m_nSKINNING = 0;
#ifdef _DEBUG
		m_bDOWATERFOG = false;
		m_bSKINNING = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bDOWATERFOG && m_bSKINNING );
		return ( 1 * m_nDOWATERFOG ) + ( 2 * m_nSKINNING ) + 0;
	}
};

#define shaderDynamicTest_shadowmodel_vs51 vsh_forgot_to_set_dynamic_DOWATERFOG + vsh_forgot_to_set_dynamic_SKINNING

