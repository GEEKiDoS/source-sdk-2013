//========= Copyright Valve Corporation, All rights reserved. ============//
// VRAD ReSTIR DLL interface and host orchestration.
#ifndef VRAD_RESTIR_H
#define VRAD_RESTIR_H
#pragma once

#include "restir_types.h"
#include "ivraddll.h"

extern ReSTIROptions g_ReSTIROptions;
extern dface_t *g_pFaces; // dfaces or dfaces_hdr for the selected mode

class CReSTIRSceneBuilder;
class CReSTIRVulkanDevice;
class CReSTIRDenoiser;
class CReSTIRStaticPropMgr;

class CVRadRestirDLL : public IVRadDLL
{
public:
	CVRadRestirDLL();
	~CVRadRestirDLL();

	int main( int argc, char **argv ) override;
	bool Init( char const *pFilename ) override;
	void Release() override;
	void GetBSPInfo( CBSPInfo *pInfo ) override;
	bool DoIncrementalLight( char const *pVMFFile ) override;
	bool Serialize() override;
	float GetPercentComplete() override;
	void Interrupt() override;

private:
	bool LoadSelectedBSP( const ReSTIROptions &options );
	int BakeSelectedMode( bool &pairedComplete );
	void UnloadSelectedBSP();
	void ClearState();
	CReSTIRVulkanDevice *m_pInterfaceDevice;

	bool m_bFileSystemInitialized;
	bool m_bBSPLoaded;
	bool m_bGpuInitialized;
	bool m_bBakeComplete;
	bool m_bInterrupted;
	float m_flProgress;
};

#endif // VRAD_RESTIR_H
