// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $STATIC_LIGHT_VERTEX && $TANGENT
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "PBRMaterialVS.h"
typedef dx12cb::PBRMaterialVS pbr_model_vs51_Block;
class pbr_model_vs51_Static_Index
{
	unsigned int m_nTANGENT : 2;
#ifdef _DEBUG
	bool m_bTANGENT : 1;
#endif	// _DEBUG
public:
	void SetTANGENT( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nTANGENT = i;
#ifdef _DEBUG
		m_bTANGENT = true;
#endif	// _DEBUG
	}

	pbr_model_vs51_Static_Index(  )
	{
		m_nTANGENT = 0;
#ifdef _DEBUG
		m_bTANGENT = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bTANGENT );
		return ( 32 * m_nTANGENT ) + 0;
	}
};

#define shaderStaticTest_pbr_model_vs51 vsh_forgot_to_set_static_TANGENT


class pbr_model_vs51_Dynamic_Index
{
	unsigned int m_nCOMPRESSED_VERTS : 2;
	unsigned int m_nSKINNING : 2;
	unsigned int m_nMORPHING : 2;
	unsigned int m_nDOWATERFOG : 2;
	unsigned int m_nSTATIC_LIGHT_VERTEX : 2;
#ifdef _DEBUG
	bool m_bCOMPRESSED_VERTS : 1;
	bool m_bSKINNING : 1;
	bool m_bMORPHING : 1;
	bool m_bDOWATERFOG : 1;
	bool m_bSTATIC_LIGHT_VERTEX : 1;
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

	void SetSTATIC_LIGHT_VERTEX( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSTATIC_LIGHT_VERTEX = i;
#ifdef _DEBUG
		m_bSTATIC_LIGHT_VERTEX = true;
#endif	// _DEBUG
	}

	pbr_model_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pbr_model_vs51_Block >( dx12native::kStageVertex );
		m_nCOMPRESSED_VERTS = 0;
		m_nSKINNING = 0;
		m_nMORPHING = 0;
		m_nDOWATERFOG = 0;
		m_nSTATIC_LIGHT_VERTEX = 0;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = false;
		m_bSKINNING = false;
		m_bMORPHING = false;
		m_bDOWATERFOG = false;
		m_bSTATIC_LIGHT_VERTEX = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCOMPRESSED_VERTS && m_bSKINNING && m_bMORPHING && m_bDOWATERFOG && m_bSTATIC_LIGHT_VERTEX );
		return ( 1 * m_nCOMPRESSED_VERTS ) + ( 2 * m_nSKINNING ) + ( 4 * m_nMORPHING ) + ( 8 * m_nDOWATERFOG ) + ( 16 * m_nSTATIC_LIGHT_VERTEX ) + 0;
	}
};

#define shaderDynamicTest_pbr_model_vs51 vsh_forgot_to_set_dynamic_COMPRESSED_VERTS + vsh_forgot_to_set_dynamic_SKINNING + vsh_forgot_to_set_dynamic_MORPHING + vsh_forgot_to_set_dynamic_DOWATERFOG + vsh_forgot_to_set_dynamic_STATIC_LIGHT_VERTEX

