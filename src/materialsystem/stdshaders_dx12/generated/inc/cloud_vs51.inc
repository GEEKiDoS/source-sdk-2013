// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "cloud_vs20.h"
typedef dx12cb::cloud_vs20 cloud_vs51_Block;
class cloud_vs51_Static_Index
{
public:
	cloud_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_cloud_vs51 1


class cloud_vs51_Dynamic_Index
{
public:
	cloud_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< cloud_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_cloud_vs51 1

