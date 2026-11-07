// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "screenspaceeffect_vs20.h"
typedef dx12cb::screenspaceeffect_vs20 screenspaceeffect_vs51_Block;
class screenspaceeffect_vs51_Static_Index
{
	unsigned int m_nX360APPCHOOSER : 2;
public:
	void SetX360APPCHOOSER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nX360APPCHOOSER = i;
	}

	screenspaceeffect_vs51_Static_Index(  )
	{
		m_nX360APPCHOOSER = 0;
	}

	int GetIndex() const
	{
		return ( 1 * m_nX360APPCHOOSER ) + 0;
	}
};

#define shaderStaticTest_screenspaceeffect_vs51 1


class screenspaceeffect_vs51_Dynamic_Index
{
public:
	screenspaceeffect_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< screenspaceeffect_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_screenspaceeffect_vs51 1

