// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "lightmappedreflective_vs20.h"
typedef dx12cb::lightmappedreflective_vs20 lightmappedreflective_highres_vs51_Block;
class lightmappedreflective_highres_vs51_Static_Index
{
	unsigned int m_nBASETEXTURE : 2;
#ifdef _DEBUG
	bool m_bBASETEXTURE : 1;
#endif	// _DEBUG
public:
	void SetBASETEXTURE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBASETEXTURE = i;
#ifdef _DEBUG
		m_bBASETEXTURE = true;
#endif	// _DEBUG
	}

	lightmappedreflective_highres_vs51_Static_Index(  )
	{
		m_nBASETEXTURE = 0;
#ifdef _DEBUG
		m_bBASETEXTURE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bBASETEXTURE );
		return ( 1 * m_nBASETEXTURE ) + 0;
	}
};

#define shaderStaticTest_lightmappedreflective_highres_vs51 vsh_forgot_to_set_static_BASETEXTURE


class lightmappedreflective_highres_vs51_Dynamic_Index
{
public:
	lightmappedreflective_highres_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< lightmappedreflective_highres_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_lightmappedreflective_highres_vs51 1

