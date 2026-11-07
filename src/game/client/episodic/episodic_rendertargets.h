//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Episodic client render targets.
//
//===========================================================================//
#ifndef EPISODIC_RENDERTARGETS_H
#define EPISODIC_RENDERTARGETS_H
#ifdef _WIN32
#pragma once
#endif

#include "baseclientrendertargets.h"

class CEpisodicRenderTargets : public CBaseClientRenderTargets
{
	DECLARE_CLASS_GAMEROOT( CEpisodicRenderTargets, CBaseClientRenderTargets );
public:
	virtual void InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
	virtual void ShutdownClientRenderTargets();
};

#endif // EPISODIC_RENDERTARGETS_H
