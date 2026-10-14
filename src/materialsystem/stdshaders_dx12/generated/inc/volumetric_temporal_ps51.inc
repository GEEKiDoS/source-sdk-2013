#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock volumetric_temporal_ps51_Block;
class volumetric_temporal_ps51_Static_Index
{
public:
	volumetric_temporal_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_volumetric_temporal_ps51 1


class volumetric_temporal_ps51_Dynamic_Index
{
public:
	volumetric_temporal_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< volumetric_temporal_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_volumetric_temporal_ps51 1

