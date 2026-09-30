#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock shadowmodel_ps51_Block;
class shadowmodel_ps51_Static_Index
{
public:
	shadowmodel_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_shadowmodel_ps51 1


class shadowmodel_ps51_Dynamic_Index
{
public:
	shadowmodel_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< shadowmodel_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_shadowmodel_ps51 1

