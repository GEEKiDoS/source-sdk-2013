// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "compositor_vs20.h"
typedef dx12cb::compositor_vs20 compositor_vs51_Block;
class compositor_vs51_Static_Index
{
public:
	compositor_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_compositor_vs51 1


class compositor_vs51_Dynamic_Index
{
public:
	compositor_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< compositor_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_compositor_vs51 1

