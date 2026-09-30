// Native SM5 port of materialsystem/stdshaders/worldvertextransition_dx8_helper.h (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef WORLDVERTEXTRANSITION_DX8_HELPER_H
#define WORLDVERTEXTRANSITION_DX8_HELPER_H

#include <string.h>


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CBaseVSShaderDX12;
class IMaterialVar;
class IShaderDynamicAPI;
class IShaderShadow;


//-----------------------------------------------------------------------------
// Init params/ init/ draw methods
//-----------------------------------------------------------------------------
struct WorldVertexTransitionEditor_DX8_Vars_t
{
	WorldVertexTransitionEditor_DX8_Vars_t() { memset( this, 0xFF, sizeof(WorldVertexTransitionEditor_DX8_Vars_t) ); }

	int m_nBaseTextureVar;
	int m_nBaseTextureFrameVar;
	int m_nBaseTextureTransformVar;
	int m_nBaseTexture2Var;
	int m_nBaseTexture2FrameVar;
	int m_nBaseTexture2TransformVar;
};

void InitParamsWorldVertexTransitionEditor_DX8( IMaterialVar** params, WorldVertexTransitionEditor_DX8_Vars_t &info );
void InitWorldVertexTransitionEditor_DX8( CBaseVSShaderDX12 *pShader, IMaterialVar** params, WorldVertexTransitionEditor_DX8_Vars_t &info );
void DrawWorldVertexTransitionEditor_DX8( CBaseVSShaderDX12 *pShader, IMaterialVar** params, 
	IShaderDynamicAPI *pShaderAPI, IShaderShadow* pShaderShadow, WorldVertexTransitionEditor_DX8_Vars_t &info );


#endif // WORLDVERTEXTRANSITION_DX8_HELPER_H
