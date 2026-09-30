// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock color_projection_vs51_Block;
class color_projection_vs51_Static_Index
{
public:
	color_projection_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_color_projection_vs51 1


class color_projection_vs51_Dynamic_Index
{
public:
	color_projection_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< color_projection_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_color_projection_vs51 1

