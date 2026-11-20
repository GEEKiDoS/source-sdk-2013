#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock flashlight_bounce_vs51_Block;
class flashlight_bounce_vs51_Static_Index
{
	unsigned int m_nVIEWMODEL : 2;
#ifdef _DEBUG
	bool m_bVIEWMODEL : 1;
#endif	// _DEBUG
public:
	void SetVIEWMODEL( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVIEWMODEL = i;
#ifdef _DEBUG
		m_bVIEWMODEL = true;
#endif	// _DEBUG
	}

	flashlight_bounce_vs51_Static_Index(  )
	{
		m_nVIEWMODEL = 0;
#ifdef _DEBUG
		m_bVIEWMODEL = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bVIEWMODEL );
		return ( 1 * m_nVIEWMODEL ) + 0;
	}
};

#define shaderStaticTest_flashlight_bounce_vs51 vsh_forgot_to_set_static_VIEWMODEL


class flashlight_bounce_vs51_Dynamic_Index
{
public:
	flashlight_bounce_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< flashlight_bounce_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_flashlight_bounce_vs51 1

