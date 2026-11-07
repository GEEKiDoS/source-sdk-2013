// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "vertexlit_and_unlit_generic_vs20.h"
typedef dx12cb::vertexlit_and_unlit_generic_vs20 vertexlit_and_unlit_generic_shadowmap_vs51_Block;
class vertexlit_and_unlit_generic_shadowmap_vs51_Static_Index
{
	unsigned int m_nSEAMLESS_BASE : 2;
	unsigned int m_nSEAMLESS_DETAIL : 2;
#ifdef _DEBUG
	bool m_bSEAMLESS_BASE : 1;
	bool m_bSEAMLESS_DETAIL : 1;
#endif	// _DEBUG
public:
	void SetSEAMLESS_BASE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEAMLESS_BASE = i;
#ifdef _DEBUG
		m_bSEAMLESS_BASE = true;
#endif	// _DEBUG
	}

	void SetSEAMLESS_DETAIL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSEAMLESS_DETAIL = i;
#ifdef _DEBUG
		m_bSEAMLESS_DETAIL = true;
#endif	// _DEBUG
	}

	vertexlit_and_unlit_generic_shadowmap_vs51_Static_Index(  )
	{
		m_nSEAMLESS_BASE = 0;
		m_nSEAMLESS_DETAIL = 0;
#ifdef _DEBUG
		m_bSEAMLESS_BASE = false;
		m_bSEAMLESS_DETAIL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bSEAMLESS_BASE && m_bSEAMLESS_DETAIL );
		return ( 2 * m_nSEAMLESS_BASE ) + ( 4 * m_nSEAMLESS_DETAIL ) + 0;
	}
};

#define shaderStaticTest_vertexlit_and_unlit_generic_shadowmap_vs51 vsh_forgot_to_set_static_SEAMLESS_BASE + vsh_forgot_to_set_static_SEAMLESS_DETAIL


class vertexlit_and_unlit_generic_shadowmap_vs51_Dynamic_Index
{
	unsigned int m_nCOMPRESSED_VERTS : 2;
#ifdef _DEBUG
	bool m_bCOMPRESSED_VERTS : 1;
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

	vertexlit_and_unlit_generic_shadowmap_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< vertexlit_and_unlit_generic_shadowmap_vs51_Block >( dx12native::kStageVertex );
		m_nCOMPRESSED_VERTS = 0;
#ifdef _DEBUG
		m_bCOMPRESSED_VERTS = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bCOMPRESSED_VERTS );
		return ( 1 * m_nCOMPRESSED_VERTS ) + 0;
	}
};

#define shaderDynamicTest_vertexlit_and_unlit_generic_shadowmap_vs51 vsh_forgot_to_set_dynamic_COMPRESSED_VERTS

