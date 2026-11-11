//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "baseclientrendertargets.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "filesystem.h"
#include "materialsystem/itexture.h"
#include "tier0/icommandline.h"
#include "motionvectors_dx12.h"
#include "upscaler_dx12.h"
#include "gtao_dx12.h"
#include "pbr_debug_dx12.h"
#include "screenspace_dx12.h"
#include "postprocess_dx12.h"
#include "framegen_dx12.h"
#include "l4d2/effects/terror_glow_render.h"
#include "tier0/memdbgon.h"

// C286160/C286180, base C011B70/C011B10. Engine ABI remains IClientRenderTargets.
class CTerrorRenderTargets : public CBaseClientRenderTargets
{
	DECLARE_CLASS_GAMEROOT( CTerrorRenderTargets, CBaseClientRenderTargets );
public:
	virtual void InitClientRenderTargets( IMaterialSystem *pMaterialSystem,
		IMaterialSystemHardwareConfig *pHardwareConfig ) OVERRIDE
	{
		// This supported client callback precedes EngineVGUI's first scheme load.
		// Borrow the parent filesystem; no shared app-system registration is added.
		CSysModule *pFileSystemModule = Sys_LoadModule( "filesystem_stdio.dll", SYS_NOLOAD );
		CreateInterfaceFn factory = pFileSystemModule ? Sys_GetFactory( pFileSystemModule ) : NULL;
		IFileSystem *pFileSystem = factory ?
			static_cast<IFileSystem *>( factory( FILESYSTEM_INTERFACE_VERSION, NULL ) ) : NULL;
		if ( !pFileSystem || !PrepareTerrorHudBootstrap( pFileSystem ) )
		{
			Error( "Unable to prepare the mounted original L4D2 default scheme\n" );
		}

		int nSize = CommandLine()->ParmValue( "-reflectionTextureSize", 1024 );
		// Native GetRenderTargetFormat(0,BGRA8888,true) is not in the target
		// interface. Keep the target SDK's hardware-selected back-buffer format.
		m_WaterReflectionTexture.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2(
			"_rt_WaterReflection", nSize, nSize, RT_SIZE_PICMIP,
			pMaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, CREATERENDERTARGETFLAGS_HDR ) );
		// SDK refractive glass and the below-DX92 fallback still own this RT.
		// L4D2 Water at DX92+ samples the opaque full-frame copy instead.
		m_WaterRefractionTexture.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2(
			"_rt_WaterRefraction", nSize, nSize, RT_SIZE_PICMIP,
			IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, CREATERENDERTARGETFLAGS_HDR ) );
		// Native deliberately retains this reference, without a stored owner
		// or shutdown decrement. Do not repair that lifetime here.
		ITexture *pEyeGlint = pMaterialSystem->CreateNamedRenderTargetTextureEx2(
			"_rt_eyeglint", 32, 32, RT_SIZE_NO_CHANGE, IMAGE_FORMAT_BGRA8888,
			MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 );
		pEyeGlint->IncrementReferenceCount();
		// Original shadow-manager InitRenderTargets is absent on target;
		// the SDK shadow manager retains its own supported target lifecycle.
		MotionVectorsDX12_CreateRenderTarget( pMaterialSystem, pHardwareConfig );
		UpscalerDX12_Init( pHardwareConfig );
		FrameGenDX12_Init( pHardwareConfig );
		GTAODX12_Init( pHardwareConfig );
		GTAODX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
		PBRDebugDX12_Init( pHardwareConfig );
		PBRDebugDX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
		ScreenSpaceDX12_Init( pHardwareConfig );
		PostProcessDX12_Init( pHardwareConfig );
		PostProcessDX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
		TerrorGlow_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
	}
	virtual void ShutdownClientRenderTargets() OVERRIDE
	{
		TerrorGlow_ShutdownRenderTargets();
		PostProcessDX12_Shutdown();
		ScreenSpaceDX12_Shutdown();
		PBRDebugDX12_Shutdown();
		GTAODX12_Shutdown();
		FrameGenDX12_Shutdown();
		UpscalerDX12_Shutdown();
		MotionVectorsDX12_ShutdownRenderTarget();
		m_WaterReflectionTexture.Shutdown();
		m_WaterRefractionTexture.Shutdown();
		// Native shadow-manager ShutdownRenderTargets is not a target API.
	}
};

static CTerrorRenderTargets g_TerrorRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CTerrorRenderTargets, IClientRenderTargets,
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_TerrorRenderTargets );
