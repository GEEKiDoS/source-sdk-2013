// Native SM5 port of materialsystem/stdshaders/volume_clouds_helper.h (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef VOLUME_CLOUDS_HELPER_H
#define VOLUME_CLOUDS_HELPER_H
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
struct VolumeCloudsVars_t
{
	VolumeCloudsVars_t() { memset( this, 0xFF, sizeof( VolumeCloudsVars_t ) ); }

	int m_nRefractAmount;
	int m_nTexture1;
	int m_nTexture2;
	int m_nTexture3;
	int m_nTime;
};

// Default values (Arrays should only be vec[4])
static const float kDefaultRefractAmount = 0.1f;

void InitParamsVolumeClouds( CBaseVSShaderDX12 *pShader, IMaterialVar** params, const char *pMaterialName, VolumeCloudsVars_t &info );
void InitVolumeClouds( CBaseVSShaderDX12 *pShader, IMaterialVar** params, VolumeCloudsVars_t &info );
void DrawVolumeClouds( CBaseVSShaderDX12 *pShader, IMaterialVar** params, IShaderDynamicAPI *pShaderAPI,
					  IShaderShadow* pShaderShadow, VolumeCloudsVars_t &info, VertexCompressionType_t vertexCompression );

#endif // VolumeClouds_HELPER_H
