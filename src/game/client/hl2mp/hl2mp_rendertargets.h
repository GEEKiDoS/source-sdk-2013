//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2MP client render targets.
//
//===========================================================================//
#ifndef HL2MP_RENDERTARGETS_H
#define HL2MP_RENDERTARGETS_H
#ifdef _WIN32
#pragma once
#endif

#include "baseclientrendertargets.h"

class CHL2MPRenderTargets : public CBaseClientRenderTargets
{
	DECLARE_CLASS_GAMEROOT( CHL2MPRenderTargets, CBaseClientRenderTargets );
public:
	virtual void InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
	virtual void ShutdownClientRenderTargets();
};

#endif // HL2MP_RENDERTARGETS_H
