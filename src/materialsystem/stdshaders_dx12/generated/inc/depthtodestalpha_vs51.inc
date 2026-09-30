// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock depthtodestalpha_vs51_Block;
class depthtodestalpha_vs51_Static_Index
{
public:
	depthtodestalpha_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_depthtodestalpha_vs51 1


class depthtodestalpha_vs51_Dynamic_Index
{
public:
	depthtodestalpha_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< depthtodestalpha_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_depthtodestalpha_vs51 1

