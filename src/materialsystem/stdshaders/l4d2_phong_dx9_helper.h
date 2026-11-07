//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef L4D2_PHONG_DX9_HELPER_H
#define L4D2_PHONG_DX9_HELPER_H

#include <string.h>

#include "l4d2_vertexlitgeneric_dx9_helper.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CBaseVSShader;
class IMaterialVar;
class IShaderDynamicAPI;
class IShaderShadow;

void InitParamsL4D2Phong_DX9( CBaseVSShader *pShader, IMaterialVar** params,
						 const char *pMaterialName, L4D2VertexLitGeneric_DX9_Vars_t &info );
void InitL4D2Phong_DX9( CBaseVSShader *pShader, IMaterialVar** params, 
				   L4D2VertexLitGeneric_DX9_Vars_t &info );

void DrawL4D2Phong_DX9( CBaseVSShader *pShader, IMaterialVar** params, IShaderDynamicAPI *pShaderAPI,
				   IShaderShadow* pShaderShadow,
				   L4D2VertexLitGeneric_DX9_Vars_t &info, VertexCompressionType_t vertexCompression,
				   CBasePerMaterialContextData **pContextDataPtr );

				   

#endif // SKIN_DX9_HELPER_H
