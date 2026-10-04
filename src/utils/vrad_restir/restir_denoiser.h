//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: OIDN and deterministic bilateral denoising for ReSTIR lightmaps.
//
//=============================================================================//
#ifndef RESTIR_DENOISER_H
#define RESTIR_DENOISER_H
#pragma once

#include "restir_types.h"

class CReSTIRDenoiser
{
public:
	CReSTIRDenoiser();
	~CReSTIRDenoiser();

	bool Init( const ReSTIROptions &options, const ReSTIRDeviceInfo &device );
	void Shutdown();
	bool Denoise( const ReSTIRScene &scene, ReSTIRLightmapResult &result );
	const char *GetModeString() const;

private:
	void *m_pModule;
	void *m_pApi;
	void *m_pDevice;
	ReSTIRDenoiserMode m_requestedMode;
	ReSTIRDenoiserQuality m_quality;
	ReSTIRDenoiserDevice m_devicePreference;
	const char *m_pMode;
	CUtlString m_deviceName;
};

#endif // RESTIR_DENOISER_H
