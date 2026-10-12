#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock volumetric_vs51_Block;
class volumetric_vs51_Static_Index
{
public:
	volumetric_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_volumetric_vs51 1


class volumetric_vs51_Dynamic_Index
{
public:
	volumetric_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< volumetric_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_volumetric_vs51 1

