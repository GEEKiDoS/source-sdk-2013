#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
typedef DX12NoNativeBlock eyeglint_ps51_Block;
class eyeglint_ps51_Static_Index
{
public:
	eyeglint_ps51_Static_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_eyeglint_ps51 1


class eyeglint_ps51_Dynamic_Index
{
public:
	eyeglint_ps51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< eyeglint_ps51_Block >( dx12native::kStagePixel );
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_eyeglint_ps51 1

