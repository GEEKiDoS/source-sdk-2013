#pragma once
#include "materialsystem/shaderapidx12/shaderdevice_dx12.h"
#include "materialsystem/shaderapidx12/vertex_layout_dx12.h"
#include "shaderapi/ishadershadow.h"
#include <array>
#include <string>

namespace shaderapidx12
{
struct FixedFunctionStateDX12
{
    VertexFormat_t format=0; unsigned drawFlags=SHADER_DRAW_POSITION; int texCoordCount=0;
    std::array<bool,16> textureEnabled{},texgen{},textureAlpha{};
    std::array<uint8_t,16> textureTypes{{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}};
    std::array<float,16> overbright{};
    std::array<ShaderTexGenParam_t,16> texgenParam{};
    std::array<ShaderTexOp_t,16> colorOp{},alphaOp{};
    std::array<ShaderTexArg_t,16> colorArg1{},colorArg2{},alphaArg1{},alphaArg2{};
    bool customPipe=false, lighting=false, specular=false, vertexBlend=false, constantColor=false, alphaPipe=false, constantAlpha=false, vertexAlpha=false;
    bool alphaTest=false, flatShade=false;
    ShaderAlphaFunc_t alphaFunction=SHADER_ALPHAFUNC_GEQUAL;
    ShaderMaterialSource_t materialSource=SHADER_MATERIALSOURCE_MATERIAL;
    ShaderFogMode_t fogMode=SHADER_FOGMODE_DISABLED;
};

// Generates real signed SM5 bytecode for a legacy fixed-function snapshot.
// The caller owns the returned shader record and must retain it until all PSOs retire.
ShaderRecordDX12 *CreateFixedFunctionShaderDX12(CShaderDeviceDX12 *device, const FixedFunctionStateDX12 &state, bool pixel,
                                                const std::vector<ShaderLinkageDX12> *linkedInputs=nullptr);
ShaderRecordDX12 *CompileNativeShaderRecordDX12(CShaderDeviceDX12 *device, const std::string &source, bool pixel, const char *profile);
}
