// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "sky_vs20.h"
typedef dx12cb::sky_vs20 sky_vs51_Block;
class sky_vs51_Static_Index
{
public:
	sky_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_sky_vs51 1


class sky_vs51_Dynamic_Index
{
public:
	sky_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< sky_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_sky_vs51 1

