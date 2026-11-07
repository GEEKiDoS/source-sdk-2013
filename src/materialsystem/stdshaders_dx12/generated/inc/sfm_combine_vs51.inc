// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock sfm_combine_vs51_Block;
class sfm_combine_vs51_Static_Index
{
public:
	sfm_combine_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_sfm_combine_vs51 1


class sfm_combine_vs51_Dynamic_Index
{
public:
	sfm_combine_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< sfm_combine_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_sfm_combine_vs51 1

