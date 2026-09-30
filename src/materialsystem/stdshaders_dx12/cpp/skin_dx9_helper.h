// Native SM5 port of materialsystem/stdshaders/skin_dx9_helper.h (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef SKIN_DX9_HELPER_H
#define SKIN_DX9_HELPER_H

#include <string.h>

#include "vertexlitgeneric_dx9_helper.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CBaseVSShaderDX12;
class IMaterialVar;
class IShaderDynamicAPI;
class IShaderShadow;

void InitParamsSkin_DX9( CBaseVSShaderDX12 *pShader, IMaterialVar** params,
						 const char *pMaterialName, VertexLitGeneric_DX9_Vars_t &info );
void InitSkin_DX9( CBaseVSShaderDX12 *pShader, IMaterialVar** params, 
				   VertexLitGeneric_DX9_Vars_t &info );

void DrawSkin_DX9( CBaseVSShaderDX12 *pShader, IMaterialVar** params, IShaderDynamicAPI *pShaderAPI,
				   IShaderShadow* pShaderShadow,
				   VertexLitGeneric_DX9_Vars_t &info, VertexCompressionType_t vertexCompression,
				   CBasePerMaterialContextData **pContextDataPtr );

				   

#endif // SKIN_DX9_HELPER_H
