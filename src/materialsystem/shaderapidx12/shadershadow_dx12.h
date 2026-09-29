#pragma once
#include "shaderapi/ishadershadow.h"
#include <array>
#include <string>

namespace shaderapidx12
{
class CShaderShadowDX12 final : public IShaderShadow
{
public:
    void SetDefaultState() override;
    void DepthFunc(ShaderDepthFunc_t depthFunc) override;
    void EnableDepthWrites(bool bEnable) override;
    void EnableDepthTest(bool bEnable) override;
    void EnablePolyOffset(PolygonOffsetMode_t nOffsetMode) override;
    void EnableStencil(bool bEnable) override;
    void StencilFunc(ShaderStencilFunc_t stencilFunc) override;
    void StencilPassOp(ShaderStencilOp_t stencilOp) override;
    void StencilFailOp(ShaderStencilOp_t stencilOp) override;
    void StencilDepthFailOp(ShaderStencilOp_t stencilOp) override;
    void StencilReference(int nReference) override;
    void StencilMask(int nMask) override;
    void StencilWriteMask(int nMask) override;
    void EnableColorWrites(bool bEnable) override;
    void EnableAlphaWrites(bool bEnable) override;
    void EnableBlending(bool bEnable) override;
    void BlendFunc(ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor) override;
    void EnableAlphaTest(bool bEnable) override;
    void AlphaFunc(ShaderAlphaFunc_t alphaFunc, float alphaRef) override;
    void PolyMode(ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode) override;
    void EnableCulling(bool bEnable) override;
    void EnableConstantColor(bool bEnable) override;
    void VertexShaderVertexFormat(unsigned int nFlags,int nTexCoordCount,int *pTexCoordDimensions,int nUserDataSize) override;
    void SetVertexShader(const char *pFileName,int nStaticVshIndex) override;
    void SetPixelShader(const char *pFileName,int nStaticPshIndex = 0) override;
    void EnableLighting(bool bEnable) override;
    void EnableSpecular(bool bEnable) override;
    void EnableSRGBWrite(bool bEnable) override;
    void EnableSRGBRead(Sampler_t sampler,bool bEnable) override;
    void EnableVertexBlend(bool bEnable) override;
    void OverbrightValue(TextureStage_t stage,float value) override;
    void EnableTexture(Sampler_t sampler,bool bEnable) override;
    void EnableTexGen(TextureStage_t stage,bool bEnable) override;
    void TexGen(TextureStage_t stage,ShaderTexGenParam_t param) override;
    void EnableCustomPixelPipe(bool bEnable) override;
    void CustomTextureStages(int stageCount) override;
    void CustomTextureOperation(TextureStage_t stage,ShaderTexChannel_t channel,ShaderTexOp_t op,ShaderTexArg_t arg1,ShaderTexArg_t arg2) override;
    void DrawFlags(unsigned int drawFlags) override;
    void EnableAlphaPipe(bool bEnable) override;
    void EnableConstantAlpha(bool bEnable) override;
    void EnableVertexAlpha(bool bEnable) override;
    void EnableTextureAlpha(TextureStage_t stage,bool bEnable) override;
    void EnableBlendingSeparateAlpha(bool bEnable) override;
    void BlendFuncSeparateAlpha(ShaderBlendFactor_t srcFactor,ShaderBlendFactor_t dstFactor) override;
    void FogMode(ShaderFogMode_t fogMode) override;
    void SetDiffuseMaterialSource(ShaderMaterialSource_t materialSource) override;
    void SetMorphFormat(MorphFormat_t flags) override;
    void DisableFogGammaCorrection(bool bDisable) override;
    void EnableAlphaToCoverage(bool bEnable) override;
    void SetShadowDepthFiltering(Sampler_t stage) override;
    void BlendOp(ShaderBlendOp_t blendOp) override;
    void BlendOpSeparateAlpha(ShaderBlendOp_t blendOp) override;
    const std::string &VertexShaderName() const { return vertexShader_; }
    const std::string &PixelShaderName() const { return pixelShader_; }
    int StaticVertexIndex() const { return staticVertexIndex_; }
    int StaticPixelIndex() const { return staticPixelIndex_; }
    unsigned int VertexFlags() const { return vertexFlags_; }
    VertexFormat_t VertexFormat() const { VertexFormat_t format=vertexFlags_|VERTEX_USERDATA_SIZE(userDataSize_);for(int i=0;i<texCoordCount_;++i)format|=VERTEX_TEXCOORD_SIZE(i,texCoordDimensions_[i]);return format; }
    MorphFormat_t MorphFormat() const { return morphFormat_; }
    bool DepthWrites() const { return depthWrites_; }
    bool DepthTest() const { return depthTest_; }
    bool Blending() const { return blending_; }
    bool AlphaTest() const { return alphaTest_; }
    float AlphaReference() const { return alphaRef_; }
    ShaderAlphaFunc_t AlphaFunction() const { return alphaFunc_; }
    bool CullEnabled() const { return culling_; }
    bool ColorWrites() const { return colorWrites_; }
    bool AlphaWrites() const { return alphaWrites_; }
    bool AlphaToCoverage() const { return alphaToCoverage_; }
    ShaderBlendFactor_t BlendSource() const { return blendSrc_; }
    ShaderBlendFactor_t BlendDestination() const { return blendDst_; }
    ShaderDepthFunc_t DepthFunction() const { return depthFunc_; }
    PolygonOffsetMode_t PolyOffset() const { return polyOffset_; }
    ShaderPolyMode_t PolyModeFront() const { return polyFront_; }
    ShaderPolyMode_t PolyModeBack() const { return polyBack_; }
    bool SeparateAlphaBlending() const { return separateAlpha_; }
    ShaderBlendFactor_t BlendAlphaSource() const { return blendAlphaSrc_; }
    ShaderBlendFactor_t BlendAlphaDestination() const { return blendAlphaDst_; }
    ShaderBlendOp_t BlendOperation() const { return blendOp_; }
    ShaderBlendOp_t BlendAlphaOperation() const { return blendAlphaOp_; }
    ShaderStencilFunc_t StencilFunction() const { return stencilFunc_; }
    ShaderStencilOp_t StencilPassOperation() const { return stencilPass_; }
    ShaderStencilOp_t StencilFailOperation() const { return stencilFail_; }
    ShaderStencilOp_t StencilDepthFailOperation() const { return stencilDepthFail_; }
    uint8_t StencilTestMask() const { return stencilMask_; }
    uint8_t StencilWriteMask() const { return stencilWriteMask_; }
    uint8_t StencilReferenceValue() const { return stencilReference_; }
    bool StencilEnabled() const { return stencil_; }
    ShaderFogMode_t FogModeValue() const { return fogMode_; }
    bool FogGammaDisabled() const { return fogGammaDisabled_; }
    bool SRGBWrite() const { return srgbWrite_; }
    uint16_t SRGBReadMask() const { uint16_t mask=0;for(size_t i=0;i<srgbRead_.size();++i)if(srgbRead_[i])mask|=static_cast<uint16_t>(1u<<i);return mask; }
    uint16_t ComparisonSamplerMask() const { return comparisonSamplerMask_; }
    bool Lighting() const { return lighting_; }
    bool Specular() const { return specular_; }
    bool VertexBlend() const { return vertexBlend_; }
    bool ConstantColor() const { return constantColor_; }
    bool CustomPixelPipe() const { return customPipe_; }
    int CustomTextureStageCount() const { return customStages_; }
    unsigned int DrawFlagsValue() const { return drawFlags_; }
    bool AlphaPipe() const { return alphaPipe_; }
    bool ConstantAlpha() const { return constantAlpha_; }
    bool VertexAlpha() const { return vertexAlpha_; }
    ShaderMaterialSource_t DiffuseMaterialSource() const { return materialSource_; }
    bool TextureEnabled(int stage) const { return stage>=0&&stage<(int)textures_.size()&&textures_[stage]; }
    float Overbright(int stage) const { return stage>=0&&stage<(int)overbright_.size()?overbright_[stage]:1.f; }
    bool TexGenEnabled(int stage) const { return stage>=0&&stage<(int)texgen_.size()&&texgen_[stage]; }
    ShaderTexGenParam_t TexGenParam(int stage) const { return stage>=0&&stage<(int)texgenParam_.size()?texgenParam_[stage]:SHADER_TEXGENPARAM_OBJECT_LINEAR; }
    bool TextureAlphaEnabled(int stage) const { return stage>=0&&stage<(int)textureAlpha_.size()&&textureAlpha_[stage]; }
    ShaderTexOp_t TextureOperation(int stage,ShaderTexChannel_t channel) const { return stage>=0&&stage<(int)texOps_.size()?texOps_[stage][channel]:SHADER_TEXOP_DISABLE; }
    ShaderTexArg_t TextureArgument(int stage,ShaderTexChannel_t channel,int arg) const { return stage>=0&&stage<(int)texArgs_.size()&&channel<2&&arg<2?texArgs_[stage][channel][arg]:SHADER_TEXARG_NONE; }
    bool EnableBlendingSeparateAlphaValue() const { return separateAlpha_; }
private:
    struct TextureStageStateDX12 { float overbright=1.f; bool texgen=false,textureAlpha=false; ShaderTexGenParam_t texgenParam=SHADER_TEXGENPARAM_OBJECT_LINEAR; ShaderTexOp_t op[2]={SHADER_TEXOP_DISABLE,SHADER_TEXOP_DISABLE}; ShaderTexArg_t arg[2][2]={{SHADER_TEXARG_TEXTURE,SHADER_TEXARG_PREVIOUSSTAGE},{SHADER_TEXARG_TEXTURE,SHADER_TEXARG_PREVIOUSSTAGE}}; };
    std::array<float,16> overbright_{};
    std::array<bool,16> texgen_{}, textureAlpha_{};
    std::array<ShaderTexGenParam_t,16> texgenParam_{};
    std::array<std::array<ShaderTexOp_t,2>,16> texOps_{};
    std::array<std::array<std::array<ShaderTexArg_t,2>,2>,16> texArgs_{};
private:
    ShaderDepthFunc_t depthFunc_ = SHADER_DEPTHFUNC_NEAREROREQUAL;
    ShaderPolyMode_t polyFront_=SHADER_POLYMODE_FILL, polyBack_=SHADER_POLYMODE_FILL;
    PolygonOffsetMode_t polyOffset_ = SHADER_POLYOFFSET_DISABLE;
    ShaderStencilFunc_t stencilFunc_ = SHADER_STENCILFUNC_ALWAYS;
    ShaderStencilOp_t stencilPass_ = SHADER_STENCILOP_KEEP, stencilFail_ = SHADER_STENCILOP_KEEP, stencilDepthFail_ = SHADER_STENCILOP_KEEP;
    ShaderBlendFactor_t blendSrc_ = SHADER_BLEND_ONE, blendDst_ = SHADER_BLEND_ZERO, blendAlphaSrc_ = SHADER_BLEND_ONE, blendAlphaDst_ = SHADER_BLEND_ZERO;
    ShaderBlendOp_t blendOp_ = SHADER_BLEND_OP_ADD, blendAlphaOp_ = SHADER_BLEND_OP_ADD;
    ShaderAlphaFunc_t alphaFunc_ = SHADER_ALPHAFUNC_ALWAYS;
    ShaderFogMode_t fogMode_ = SHADER_FOGMODE_DISABLED;
    ShaderMaterialSource_t materialSource_ = SHADER_MATERIALSOURCE_MATERIAL;
    int staticVertexIndex_=0, staticPixelIndex_=0;
    unsigned int vertexFlags_ = 0, drawFlags_ = 0;
    uint16_t comparisonSamplerMask_=0;
    uint8_t stencilMask_=0xff, stencilWriteMask_=0xff, stencilReference_=0;
    int texCoordCount_ = 0, userDataSize_ = 0, customStages_ = 0;
    MorphFormat_t morphFormat_ = 0;
    float alphaRef_ = 0.0f;
    std::array<int,8> texCoordDimensions_{};
    std::array<bool,16> textures_{}, srgbRead_{};
    bool depthWrites_=true, depthTest_=true, stencil_=false, colorWrites_=true, alphaWrites_=true, blending_=false, alphaTest_=false, culling_=true, constantColor_=false, lighting_=false, specular_=false, srgbWrite_=false, vertexBlend_=false, customPipe_=false, alphaPipe_=false, constantAlpha_=false, vertexAlpha_=false, separateAlpha_=false, fogGammaDisabled_=false, alphaToCoverage_=false;
    std::string vertexShader_, pixelShader_;
};
extern CShaderShadowDX12 *g_pShaderShadowDX12;
} // namespace shaderapidx12
