// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock morphweight_vs51_Block;
class morphweight_vs51_Static_Index
{
public:
	morphweight_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_morphweight_vs51 1


class morphweight_vs51_Dynamic_Index
{
public:
	morphweight_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< morphweight_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_morphweight_vs51 1

