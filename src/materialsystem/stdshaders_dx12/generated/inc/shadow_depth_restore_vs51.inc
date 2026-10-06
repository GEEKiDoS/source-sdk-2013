#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock shadow_depth_restore_vs51_Block;
class shadow_depth_restore_vs51_Static_Index
{
public:
	shadow_depth_restore_vs51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_shadow_depth_restore_vs51 1


class shadow_depth_restore_vs51_Dynamic_Index
{
public:
	shadow_depth_restore_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< shadow_depth_restore_vs51_Block >( dx12native::kStageVertex );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_shadow_depth_restore_vs51 1

