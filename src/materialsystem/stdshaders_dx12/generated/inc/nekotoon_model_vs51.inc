// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "PBRMaterialVS.h"
typedef dx12cb::PBRMaterialVS nekotoon_model_vs51_Block;
class nekotoon_model_vs51_Static_Index
{
	unsigned int m_nBUMP : 2;
	unsigned int m_nHULL : 2;
#ifdef _DEBUG
	bool m_bBUMP : 1;
	bool m_bHULL : 1;
#endif	// _DEBUG
public:
	void SetBUMP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMP = i;
#ifdef _DEBUG
		m_bBUMP = true;
#endif	// _DEBUG
	}

	void SetHULL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nHULL = i;
#ifdef _DEBUG
		m_bHULL = true;
#endif	// _DEBUG
	}

	nekotoon_model_vs51_Static_Index(  )
	{
		m_nBUMP = 0;
		m_nHULL = 0;
#ifdef _DEBUG
		m_bBUMP = false;
		m_bHULL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bBUMP && m_bHULL );
		return ( 16 * m_nBUMP ) + ( 32 * m_nHULL ) + 0;
	}
};

#define shaderStaticTest_nekotoon_model_vs51 vsh_forgot_to_set_static_BUMP + vsh_forgot_to_set_static_HULL


class nekotoon_model_vs51_Dynamic_Index
{
	unsigned int m_nCOMPRESSED_VERTS : 2;
	unsigned int m_nSKINNING : 2;
	unsigned int m_nMORPHING : 2;
	unsigned int m_nDOWATERFOG : 2;
#ifdef _DEBUG
	bool m_bCOMPRESSED_VERTS : 1;
	bool m_bSKINNING : 1;
	bool m_bMORPHING : 1;
	bool m_bDOWATERFOG : 1;
#endif	// _DEBUG
public:
	void SetCOMPRESSED_VERTS( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCOMPRESSED_VERTS = i;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = true;
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

	void SetMORPHING( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMORPHING = i;
#ifdef _DEBUG
		m_bMORPHING = true;
#endif	// _DEBUG
	}

	void SetDOWATERFOG( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDOWATERFOG = i;
#ifdef _DEBUG
		m_bDOWATERFOG = true;
#endif	// _DEBUG
	}

	nekotoon_model_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< nekotoon_model_vs51_Block >( dx12native::kStageVertex );
		m_nCOMPRESSED_VERTS = 0;
		m_nSKINNING = 0;
		m_nMORPHING = 0;
		m_nDOWATERFOG = 0;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = false;
		m_bSKINNING = false;
		m_bMORPHING = false;
		m_bDOWATERFOG = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCOMPRESSED_VERTS && m_bSKINNING && m_bMORPHING && m_bDOWATERFOG );
		return ( 1 * m_nCOMPRESSED_VERTS ) + ( 2 * m_nSKINNING ) + ( 4 * m_nMORPHING ) + ( 8 * m_nDOWATERFOG ) + 0;
	}
};

#define shaderDynamicTest_nekotoon_model_vs51 vsh_forgot_to_set_dynamic_COMPRESSED_VERTS + vsh_forgot_to_set_dynamic_SKINNING + vsh_forgot_to_set_dynamic_MORPHING + vsh_forgot_to_set_dynamic_DOWATERFOG

