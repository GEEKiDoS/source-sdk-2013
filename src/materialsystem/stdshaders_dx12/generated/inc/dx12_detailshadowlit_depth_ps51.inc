#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "DetailShadowDepthMaterial.h"
typedef dx12cb::DetailShadowDepthMaterial dx12_detailshadowlit_depth_ps51_Block;
class dx12_detailshadowlit_depth_ps51_Static_Index
{
public:
	dx12_detailshadowlit_depth_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_dx12_detailshadowlit_depth_ps51 1


class dx12_detailshadowlit_depth_ps51_Dynamic_Index
{
public:
	dx12_detailshadowlit_depth_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< dx12_detailshadowlit_depth_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_dx12_detailshadowlit_depth_ps51 1

