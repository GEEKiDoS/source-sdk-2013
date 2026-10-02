//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 implementation of IMaterialSystemHardwareConfig
//
//=============================================================================//

#ifndef HARDWARECONFIG_DX12_H
#define HARDWARECONFIG_DX12_H
#pragma once

#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterialsystem.h"
#include "bitmap/imageformat.h"

namespace shaderapidx12
{

class IHardwareConfigInternal : public IMaterialSystemHardwareConfig
{
public:
	virtual const char *GetHWSpecificShaderDLLName() const = 0;
};

class CHardwareConfigDX12 final : public IHardwareConfigInternal
{
public:
	CHardwareConfigDX12();
	void SetDXSupportLevels( int nRecommended, int nMaximum );
	void SetAdapter( const MaterialAdapterInfo_t &adapter, uint64_t nDedicatedVideoMemory, bool bAaEnabled );
	void SetSupportCaps( bool bFastClipping, bool bCentroidHack, bool bDisableShaderOptimizations );
	void SetDXLevel( int nLevel );

	bool DisableShaderOptimizations() const { return m_bDisableShaderOptimizations; }

	const MaterialAdapterInfo_t &Adapter() const { return m_Adapter; }

	const char *GetHWSpecificShaderDLLName() const override { return "stdshader_dx12.dll"; }

	bool HasDestAlphaBuffer() const override { return true; }

	bool HasStencilBuffer() const override { return true; }

	int GetFrameBufferColorDepth() const override { return 32; }

	int GetSamplerCount() const override { return 16; }

	bool HasSetDeviceGammaRamp() const override { return true; }

	bool SupportsCompressedTextures() const override { return true; }

	VertexCompressionType_t SupportsCompressedVertices() const override { return VERTEX_COMPRESSION_ON; }

	bool SupportsNormalMapCompression() const override { return true; }

	bool SupportsVertexAndPixelShaders() const override { return true; }

	bool SupportsPixelShaders_1_4() const override { return true; }

	bool SupportsStaticControlFlow() const override { return true; }

	bool SupportsPixelShaders_2_0() const override { return true; }

	bool SupportsVertexShaders_2_0() const override { return true; }

	int MaximumAnisotropicLevel() const override { return 16; }

	int MaxTextureWidth() const override { return 16384; }

	int MaxTextureHeight() const override { return 16384; }

	int TextureMemorySize() const override;

	bool SupportsOverbright() const override { return true; }

	bool SupportsCubeMaps() const override { return true; }

	bool SupportsMipmappedCubemaps() const override { return true; }

	bool SupportsNonPow2Textures() const override { return true; }

	int GetTextureStageCount() const override { return 16; }

	int NumVertexShaderConstants() const override { return 256; }

	int NumPixelShaderConstants() const override { return 224; }

	int MaxNumLights() const override { return 4; }

	bool SupportsHardwareLighting() const override { return true; }

	int MaxBlendMatrices() const override { return 53; }

	int MaxBlendMatrixIndices() const override { return 53; }

	int MaxTextureAspectRatio() const override { return 16384; }

	int MaxVertexShaderBlendMatrices() const override { return 53; }

	int MaxUserClipPlanes() const override { return 6; }

	bool UseFastClipping() const override { return m_bFastClipping; }

	int GetDXSupportLevel() const override { return m_nDxLevel; }

	const char *GetShaderDLLName() const override { return "stdshader_dx12"; }

	bool ReadPixelsFromFrontBuffer() const override { return false; }

	bool PreferDynamicTextures() const override { return false; }

	bool SupportsHDR() const override { return true; }

	bool HasProjectedBumpEnv() const override { return true; }

	bool SupportsSpheremapping() const override { return true; }

	bool NeedsAAClamp() const override { return false; }

	bool NeedsATICentroidHack() const override { return m_bCentroidHack; }

	bool SupportsColorOnSecondStream() const override { return true; }

	bool SupportsStaticPlusDynamicLighting() const override { return true; }

	bool PreferReducedFillrate() const override { return false; }

	int GetMaxDXSupportLevel() const override { return m_nMaxDXLevel; }

	bool SpecifiesFogColorInLinearSpace() const override { return true; }

	bool SupportsSRGB() const override { return true; }

	bool FakeSRGBWrite() const override { return false; }

	bool CanDoSRGBReadFromRTs() const override { return true; }

	bool SupportsGLMixedSizeTargets() const override { return false; }

	bool IsAAEnabled() const override { return m_bAaEnabled; }

	int GetVertexTextureCount() const override { return 4; }

	int GetMaxVertexTextureDimension() const override { return 16384; }

	int MaxTextureDepth() const override { return 2048; }

	HDRType_t GetHDRType() const override { return m_bHdrEnabled && GetDXSupportLevel() >= 90 ? m_HdrType : HDR_TYPE_NONE; }

	HDRType_t GetHardwareHDRType() const override { return m_HdrType; }

	bool SupportsPixelShaders_2_b() const override { return true; }

	bool SupportsStreamOffset() const override { return !m_bStreamOffsetOverride || m_bStreamOffsetSupport; }

	int StencilBufferBits() const override { return 8; }

	int MaxViewports() const override { return 16; }

	void OverrideStreamOffsetSupport( bool bOverrideEnabled, bool bEnableSupport ) override
	{
		m_bStreamOffsetOverride = bOverrideEnabled;
		m_bStreamOffsetSupport = bEnableSupport;
	}

	int GetShadowFilterMode() const override { return 0; }

	int NeedsShaderSRGBConversion() const override { return 0; }

	bool UsesSRGBCorrectBlending() const override { return true; }

	bool SupportsShaderModel_3_0() const override { return true; }

	bool HasFastVertexTextures() const override { return true; }

	int MaxHWMorphBatchCount() const override { return 60; }

	bool ActuallySupportsPixelShaders_2_b() const override { return true; }

	bool SupportsHDRMode( HDRType_t nHDRMode ) const override { return nHDRMode == HDR_TYPE_NONE || nHDRMode == HDR_TYPE_INTEGER || nHDRMode == HDR_TYPE_FLOAT; }

	bool GetHDREnabled() const override { return m_bHdrEnabled; }

	void SetHDREnabled( bool bEnable ) override { m_bHdrEnabled = bEnable; }

	bool SupportsBorderColor() const override { return true; }

	bool SupportsFetch4() const override { return false; }

	bool CanStretchRectFromTextures() const override { return true; }

private:
	MaterialAdapterInfo_t m_Adapter{};
	uint64_t m_nDedicatedVideoMemory = 0;
	int m_nDxLevel = 95;
	bool m_bAaEnabled = false;
	bool m_bHdrEnabled = false;
	HDRType_t m_HdrType = HDR_TYPE_INTEGER;
	bool m_bStreamOffsetOverride = false;
	bool m_bStreamOffsetSupport = true;
	int m_nMaxDXLevel = 95;
	bool m_bFastClipping = false;
	bool m_bCentroidHack = false;
	bool m_bDisableShaderOptimizations = false;
};

extern CHardwareConfigDX12 *g_pHardwareConfigDX12;

} // namespace shaderapidx12

#endif // HARDWARECONFIG_DX12_H
