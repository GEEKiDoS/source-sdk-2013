// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "filmgrain_vs20.h"
typedef dx12cb::filmgrain_vs20 filmgrain_vs51_Block;
class filmgrain_vs51_Static_Index
{
public:
	filmgrain_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_filmgrain_vs51 1


class filmgrain_vs51_Dynamic_Index
{
public:
	filmgrain_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< filmgrain_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_filmgrain_vs51 1

