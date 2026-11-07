// Native SM5 port of materialsystem/stdshaders/particlelitgeneric_dx9_helper.h (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef PARTICLELITGENERIC_DX9_HELPER_H
#define PARTICLELITGENERIC_DX9_HELPER_H

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
struct ParticleLitGeneric_DX9_Vars_t
{												
	ParticleLitGeneric_DX9_Vars_t() { memset( this, 0xFF, sizeof(ParticleLitGeneric_DX9_Vars_t) ); }

	int m_nBaseTexture;
	int m_nBaseTextureFrame;
	int m_nBaseTextureTransform;
	int m_nBumpmap;
	int m_nBumpFrame;
	int m_nBumpTransform;
	int m_nAlphaTestReference;
	int m_nFlashlightTexture;
	int m_nFlashlightTextureFrame;
};

void InitParamsParticleLitGeneric_DX9( CBaseVSShaderDX12 *pShader, IMaterialVar** params, const char *pMaterialName, ParticleLitGeneric_DX9_Vars_t &info );
void InitParticleLitGeneric_DX9( CBaseVSShaderDX12 *pShader, IMaterialVar** params, ParticleLitGeneric_DX9_Vars_t &info );
void DrawParticleLitGeneric_DX9( CBaseVSShaderDX12 *pShader, IMaterialVar** params, 
	IShaderDynamicAPI *pShaderAPI, IShaderShadow* pShaderShadow, ParticleLitGeneric_DX9_Vars_t &info );


#endif // PARTICLELITGENERIC_DX9_HELPER_H
