// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "particlesphere_vs20.h"
typedef dx12cb::particlesphere_vs20 particlesphere_vs51_Block;
class particlesphere_vs51_Static_Index
{
public:
	particlesphere_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_particlesphere_vs51 1


class particlesphere_vs51_Dynamic_Index
{
	unsigned int m_nFOGTYPE : 2;
#ifdef _DEBUG
	bool m_bFOGTYPE : 1;
#endif	// _DEBUG
public:
	void SetFOGTYPE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nFOGTYPE = i;
#ifdef _DEBUG
		m_bFOGTYPE = true;
#endif	// _DEBUG
	}

	particlesphere_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< particlesphere_vs51_Block >( dx12native::kStageVertex );
		m_nFOGTYPE = 0;
#ifdef _DEBUG
		m_bFOGTYPE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bFOGTYPE );
		return ( 1 * m_nFOGTYPE ) + 0;
	}
};

#define shaderDynamicTest_particlesphere_vs51 vsh_forgot_to_set_dynamic_FOGTYPE

