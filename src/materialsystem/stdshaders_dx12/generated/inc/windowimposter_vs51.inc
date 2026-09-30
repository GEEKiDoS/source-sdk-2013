// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "windowimposter_vs20.h"
typedef dx12cb::windowimposter_vs20 windowimposter_vs51_Block;
class windowimposter_vs51_Static_Index
{
public:
	windowimposter_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_windowimposter_vs51 1


class windowimposter_vs51_Dynamic_Index
{
	unsigned int m_nDOWATERFOG : 2;
#ifdef _DEBUG
	bool m_bDOWATERFOG : 1;
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

	windowimposter_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< windowimposter_vs51_Block >( dx12native::kStageVertex );
		m_nDOWATERFOG = 0;
#ifdef _DEBUG
		m_bDOWATERFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bDOWATERFOG );
		return ( 1 * m_nDOWATERFOG ) + 0;
	}
};

#define shaderDynamicTest_windowimposter_vs51 vsh_forgot_to_set_dynamic_DOWATERFOG

