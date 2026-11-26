#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock pso_fallback_ps51_Block;
class pso_fallback_ps51_Static_Index
{
public:
	pso_fallback_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_pso_fallback_ps51 1


class pso_fallback_ps51_Dynamic_Index
{
public:
	pso_fallback_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< pso_fallback_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_pso_fallback_ps51 1

