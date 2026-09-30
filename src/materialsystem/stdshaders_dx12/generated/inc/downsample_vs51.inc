// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "Downsample_vs20.h"
typedef dx12cb::Downsample_vs20 downsample_vs51_Block;
class downsample_vs51_Static_Index
{
public:
	downsample_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_downsample_vs51 1


class downsample_vs51_Dynamic_Index
{
public:
	downsample_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< downsample_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_downsample_vs51 1

