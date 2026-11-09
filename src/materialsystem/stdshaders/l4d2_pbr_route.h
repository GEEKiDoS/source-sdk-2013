//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once

#include "BaseVSShader.h"
#include "convar.h"
#include "tier1/strtools.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"

inline bool L4D2PbrRoute( IMaterialVar **params, bool model, int detailBlendMode, int lightWarp, int phongWarp,
	int rimLight, int distanceAlpha, int seamlessBase, int seamlessDetail, int seamlessScale, int selfIllumFresnel,
	int vertexFog )
{
	if ( !g_pHardwareConfig || V_stricmp( g_pHardwareConfig->GetShaderDLLName(), "stdshader_dx12" ) )
		return false;
	static ConVarRef matPbrOverride( "mat_pbr_override" );
	if ( !matPbrOverride.IsValid() || !matPbrOverride.GetBool() )
		return false;

	const int flags = params[FLAGS]->GetIntValue();
	if ( flags & MATERIAL_VAR_ENVMAPSPHERE )
		return false;
	if ( detailBlendMode != -1 )
	{
		const int mode = params[detailBlendMode]->GetIntValue();
		if ( mode < 0 || mode > ( model ? 9 : 12 ) )
			return false;
	}

	auto isBool = [params]( int var ) { return var != -1 && params[var]->GetIntValue() != 0; };
	auto isTexture = [params]( int var ) { return var != -1 && params[var]->IsTexture(); };
	if ( isTexture( lightWarp ) || isBool( vertexFog ) )
		return false;
	if ( model )
	{
		if ( isTexture( phongWarp ) || isBool( rimLight ) || isBool( distanceAlpha ) ||
			isBool( seamlessBase ) || isBool( seamlessDetail ) || isBool( selfIllumFresnel ) )
			return false;
	}
	else if ( seamlessScale != -1 && params[seamlessScale]->GetFloatValue() != 0.0f )
	{
		return false;
	}
	return true;
}
