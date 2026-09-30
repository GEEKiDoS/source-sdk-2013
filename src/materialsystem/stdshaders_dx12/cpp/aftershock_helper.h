// Native SM5 port of materialsystem/stdshaders/aftershock_helper.h (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef AFTERSHOCK_HELPER_H
#define AFTERSHOCK_HELPER_H
#ifdef _WIN32
#pragma once
#endif

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
struct AftershockVars_t
{
	AftershockVars_t() { memset( this, 0xFF, sizeof(AftershockVars_t) ); }

	int m_nColorTint;
	int m_nRefractAmount;

	int m_nBumpmap;
	int m_nBumpFrame;
	int m_nBumpTransform;

	int m_nSilhouetteThickness;
	int m_nSilhouetteColor;
	int m_nGroundMin;
	int m_nGroundMax;
	int m_nBlurAmount;

	int m_nTime;
};

// Default values (Arrays should only be vec[4])
static const float kDefaultColorTint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kDefaultRefractAmount = 0.1f;
static const float kDefaultSilhouetteThickness = 0.2f;
static const float kDefaultSilhouetteColor[4] = { 0.3f, 0.3f, 0.5f, 1.0f };
static const float kDefaultGroundMin = -0.3f;
static const float kDefaultGroundMax = -0.1f;
static const float kDefaultBlurAmount = 0.01f;

void InitParamsAftershock( CBaseVSShaderDX12 *pShader, IMaterialVar** params, const char *pMaterialName, AftershockVars_t &info );
void InitAftershock( CBaseVSShaderDX12 *pShader, IMaterialVar** params, AftershockVars_t &info );
void DrawAftershock( CBaseVSShaderDX12 *pShader, IMaterialVar** params, IShaderDynamicAPI *pShaderAPI,
					IShaderShadow* pShaderShadow, AftershockVars_t &info, VertexCompressionType_t vertexCompression );

#endif // AFTERSHOCK_HELPER_H
