#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "GlowParams.h"
typedef dx12cb::GlowParams l4d2_glow_composite_ps51_Block;
class l4d2_glow_composite_ps51_Static_Index
{
public:
	l4d2_glow_composite_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_l4d2_glow_composite_ps51 1


class l4d2_glow_composite_ps51_Dynamic_Index
{
public:
	l4d2_glow_composite_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< l4d2_glow_composite_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_l4d2_glow_composite_ps51 1

