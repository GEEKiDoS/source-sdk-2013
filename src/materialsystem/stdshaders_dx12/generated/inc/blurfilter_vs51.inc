// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "BlurFilter_vs20.h"
typedef dx12cb::BlurFilter_vs20 blurfilter_vs51_Block;
class blurfilter_vs51_Static_Index
{
public:
	blurfilter_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_blurfilter_vs51 1


class blurfilter_vs51_Dynamic_Index
{
public:
	blurfilter_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< blurfilter_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_blurfilter_vs51 1

