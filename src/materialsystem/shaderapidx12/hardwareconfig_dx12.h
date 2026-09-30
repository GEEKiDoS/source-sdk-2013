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
    void SetDXSupportLevels(int recommended, int maximum);
    void SetAdapter(const MaterialAdapterInfo_t &adapter, uint64_t dedicatedVideoMemory, bool aaEnabled, int samples);
    void SetSupportCaps(bool fastClipping, bool centroidHack, bool disableShaderOptimizations);
    void SetDXLevel(int level);
    bool DisableShaderOptimizations() const { return disableShaderOptimizations_; }
    const MaterialAdapterInfo_t &Adapter() const { return adapter_; }
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
    bool UseFastClipping() const override { return fastClipping_; }
    int GetDXSupportLevel() const override { return dxLevel_; }
    const char *GetShaderDLLName() const override { return "stdshader_dx12"; }
    bool ReadPixelsFromFrontBuffer() const override { return false; }
    bool PreferDynamicTextures() const override { return false; }
    bool SupportsHDR() const override { return true; }
    bool HasProjectedBumpEnv() const override { return true; }
    bool SupportsSpheremapping() const override { return true; }
    bool NeedsAAClamp() const override { return false; }
    bool NeedsATICentroidHack() const override { return centroidHack_; }
    bool SupportsColorOnSecondStream() const override { return true; }
    bool SupportsStaticPlusDynamicLighting() const override { return true; }
    bool PreferReducedFillrate() const override { return false; }
    int GetMaxDXSupportLevel() const override { return maxDXLevel_; }
    bool SpecifiesFogColorInLinearSpace() const override { return true; }
    bool SupportsSRGB() const override { return true; }
    bool FakeSRGBWrite() const override { return false; }
    bool CanDoSRGBReadFromRTs() const override { return true; }
    bool SupportsGLMixedSizeTargets() const override { return false; }
    bool IsAAEnabled() const override { return aaEnabled_; }
    int GetVertexTextureCount() const override { return 4; }
    int GetMaxVertexTextureDimension() const override { return 16384; }
    int MaxTextureDepth() const override { return 2048; }
    HDRType_t GetHDRType() const override { return hdrEnabled_ && GetDXSupportLevel() >= 90 ? hdrType_ : HDR_TYPE_NONE; }
    HDRType_t GetHardwareHDRType() const override { return hdrType_; }
    bool SupportsPixelShaders_2_b() const override { return true; }
    bool SupportsStreamOffset() const override { return !streamOffsetOverride_ || streamOffsetSupport_; }
    int StencilBufferBits() const override { return 8; }
    int MaxViewports() const override { return 16; }
    void OverrideStreamOffsetSupport(bool enabled, bool support) override { streamOffsetOverride_ = enabled; streamOffsetSupport_ = support; }
    int GetShadowFilterMode() const override { return 0; }
    int NeedsShaderSRGBConversion() const override { return 0; }
    bool UsesSRGBCorrectBlending() const override { return true; }
    bool SupportsShaderModel_3_0() const override { return true; }
    bool HasFastVertexTextures() const override { return true; }
    int MaxHWMorphBatchCount() const override { return  morphBatchCount_; }
    bool ActuallySupportsPixelShaders_2_b() const override { return true; }
    bool SupportsHDRMode(HDRType_t mode) const override { return mode == HDR_TYPE_NONE || mode == HDR_TYPE_INTEGER || mode == HDR_TYPE_FLOAT; }
    bool GetHDREnabled() const override { return hdrEnabled_; }
    void SetHDREnabled(bool enabled) override { hdrEnabled_ = enabled; }
    bool SupportsBorderColor() const override { return true; }
    bool SupportsFetch4() const override { return false; }
    bool CanStretchRectFromTextures() const override { return true; }
private:
    MaterialAdapterInfo_t adapter_{};
    uint64_t dedicatedVideoMemory_ = 0;
    int dxLevel_ = 95;
    int samples_ = 1;
    bool aaEnabled_ = false;
    bool hdrEnabled_ = false;
    HDRType_t hdrType_ = HDR_TYPE_INTEGER;
    bool streamOffsetOverride_ = false;
    bool streamOffsetSupport_ = true;
    int morphBatchCount_ = 60;
    int maxDXLevel_ = 95;
    bool fastClipping_ = false;
    bool centroidHack_ = false;
    bool disableShaderOptimizations_ = false;
};

extern CHardwareConfigDX12 *g_pHardwareConfigDX12;

} // namespace shaderapidx12
