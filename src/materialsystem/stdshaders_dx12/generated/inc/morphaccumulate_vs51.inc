// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "morphaccumulate_vs30.h"
typedef dx12cb::morphaccumulate_vs30 morphaccumulate_vs51_Block;
class morphaccumulate_vs51_Static_Index
{
	unsigned int m_nCONSTANTBASEDMORPH : 2;
#ifdef _DEBUG
	bool m_bCONSTANTBASEDMORPH : 1;
#endif	// _DEBUG
public:
	void SetCONSTANTBASEDMORPH( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONSTANTBASEDMORPH = i;
#ifdef _DEBUG
		m_bCONSTANTBASEDMORPH = true;
#endif	// _DEBUG
	}

	morphaccumulate_vs51_Static_Index(  )
	{
		m_nCONSTANTBASEDMORPH = 0;
#ifdef _DEBUG
		m_bCONSTANTBASEDMORPH = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCONSTANTBASEDMORPH );
		return ( 1 * m_nCONSTANTBASEDMORPH ) + 0;
	}
};

#define shaderStaticTest_morphaccumulate_vs51 vsh_forgot_to_set_static_CONSTANTBASEDMORPH


class morphaccumulate_vs51_Dynamic_Index
{
public:
	morphaccumulate_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< morphaccumulate_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_morphaccumulate_vs51 1

