// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "PBRMaterialVS.h"
typedef dx12cb::PBRMaterialVS pbr_world_highres_vs51_Block;
class pbr_world_highres_vs51_Static_Index
{
	unsigned int m_nLAYERED : 2;
#ifdef _DEBUG
	bool m_bLAYERED : 1;
#endif	// _DEBUG
public:
	void SetLAYERED( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLAYERED = i;
#ifdef _DEBUG
		m_bLAYERED = true;
#endif	// _DEBUG
	}

	pbr_world_highres_vs51_Static_Index(  )
	{
		m_nLAYERED = 0;
#ifdef _DEBUG
		m_bLAYERED = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLAYERED );
		return ( 2 * m_nLAYERED ) + 0;
	}
};

#define shaderStaticTest_pbr_world_highres_vs51 vsh_forgot_to_set_static_LAYERED


class pbr_world_highres_vs51_Dynamic_Index
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

	pbr_world_highres_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pbr_world_highres_vs51_Block >( dx12native::kStageVertex );
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

#define shaderDynamicTest_pbr_world_highres_vs51 vsh_forgot_to_set_dynamic_DOWATERFOG

