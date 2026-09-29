// Signature construction adapted from Microsoft D3D9On12, commit
// ee599de26b8e304ff2556a5f79527b742eedfac3, src/9on12Shader.cpp and include/9on12Util.h.
// Copyright (c) Microsoft Corporation. Licensed under the MIT license.
// See thirdparty/dx12_shaderconv/LICENSE-D3D9On12.txt.
// Compiled ONLY in dx12_shaderconv: DDI headers never enter renderer translation units.
#include "../../thirdparty/dx12_shaderconv/ShaderConverter/ShaderConv/pch.h"
#include <d3dcommon.h>
#include <ShaderConv.h>
#include "tracy/Tracy.hpp"
#include <DxbcBuilder.hpp>
#include "shader_translate_dx12.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <cstdio>

namespace shaderapidx12
{
namespace
{
static_assert(sizeof(ShaderVertexExtensionDX12) == sizeof(ShaderConv::VSCBExtension), "VS extension ABI");
static_assert(sizeof(ShaderPixelExtensionDX12) == sizeof(ShaderConv::PSCBExtension), "PS extension ABI");
static_assert(sizeof(ShaderBumpExtensionDX12) == sizeof(ShaderConv::PSCBExtension2), "Bump extension ABI");
static_assert(sizeof(ShaderColorKeyExtensionDX12) == sizeof(ShaderConv::PSCBExtension3), "Color-key extension ABI");

bool Fail(std::string &error, const char *message) { error = message; return false; }
bool CheckResult(HRESULT hr, const char *operation, std::string &error)
{
    if (SUCCEEDED(hr)) return true;
    char text[160]; sprintf_s(text, "%s: HRESULT 0x%08lx", operation, static_cast<unsigned long>(hr));
    return Fail(error, text);
}
uint32_t Token(const void *bytes, size_t index)
{
    uint32_t value; memcpy(&value, static_cast<const uint8_t *>(bytes) + index * 4, 4); return value;
}
int OperandCount(uint32_t opcode, uint32_t version)
{
    const bool oldPixel = (version >> 16) == 0xffff && ((version >> 8) & 255) == 1;
    switch (opcode)
    {
    case D3DSIO_NOP: case D3DSIO_RET: case D3DSIO_ENDLOOP: case D3DSIO_ENDREP:
    case D3DSIO_ELSE: case D3DSIO_ENDIF: case D3DSIO_BREAK: case D3DSIO_PHASE: return 0;
    case D3DSIO_CALL: case D3DSIO_LABEL: case D3DSIO_REP: case D3DSIO_IF:
    case D3DSIO_BREAKP: case D3DSIO_TEXKILL: case D3DSIO_TEXDEPTH: return 1;
    case D3DSIO_MOV: case D3DSIO_RCP: case D3DSIO_RSQ: case D3DSIO_EXP: case D3DSIO_LOG:
    case D3DSIO_EXPP: case D3DSIO_LOGP: case D3DSIO_LIT: case D3DSIO_FRC: case D3DSIO_ABS:
    case D3DSIO_NRM: case D3DSIO_MOVA: case D3DSIO_DSX: case D3DSIO_DSY:
    case D3DSIO_DCL: case D3DSIO_DEFB: case D3DSIO_CALLNZ: case D3DSIO_LOOP:
    case D3DSIO_IFC: case D3DSIO_BREAKC: case D3DSIO_TEXBEM: case D3DSIO_TEXBEML:
    case D3DSIO_TEXREG2AR: case D3DSIO_TEXREG2GB: case D3DSIO_TEXREG2RGB:
    case D3DSIO_TEXM3x2PAD: case D3DSIO_TEXM3x2TEX: case D3DSIO_TEXM3x3PAD:
    case D3DSIO_TEXM3x3TEX: case D3DSIO_TEXM3x3VSPEC: case D3DSIO_TEXDP3TEX:
    case D3DSIO_TEXM3x2DEPTH: case D3DSIO_TEXDP3: case D3DSIO_TEXM3x3: return 2;
    case D3DSIO_ADD: case D3DSIO_SUB: case D3DSIO_MUL: case D3DSIO_DP3: case D3DSIO_DP4:
    case D3DSIO_MIN: case D3DSIO_MAX: case D3DSIO_SLT: case D3DSIO_SGE: case D3DSIO_DST:
    case D3DSIO_M4x4: case D3DSIO_M4x3: case D3DSIO_M3x4: case D3DSIO_M3x3: case D3DSIO_M3x2:
    case D3DSIO_POW: case D3DSIO_CRS: case D3DSIO_BEM: case D3DSIO_SETP:
    case D3DSIO_TEXM3x3SPEC: case D3DSIO_TEXLDL: return 3;
    case D3DSIO_MAD: case D3DSIO_LRP: case D3DSIO_CND: case D3DSIO_CMP:
    case D3DSIO_DP2ADD: case D3DSIO_SGN: return 4;
    case D3DSIO_DEF: case D3DSIO_DEFI: case D3DSIO_TEXLDD: return 5;
    case D3DSIO_SINCOS: return ((version >> 8) & 255) >= 3 ? 2 : 4;
    case D3DSIO_TEXCOORD: return (version & 255) >= 4 ? 2 : 1;
    case D3DSIO_TEX: return oldPixel ? ((version & 255) >= 4 ? 2 : 1) : 3;
    default: return -1;
    }
}
bool ValidateLegacy(const ShaderTranslationRequestDX12 &request, std::string &error)
{
    if (!request.legacyBytes || request.byteCount < 8 || request.byteCount % 4 || request.byteCount > UINT32_MAX)
        return Fail(error, "invalid legacy shader byte span");
    const uint32_t version = Token(request.legacyBytes, 0);
    const uint32_t major = (version >> 8) & 255, minor = version & 255;
    if ((version >> 16) != (request.pixel ? 0xffffu : 0xfffeu) || major < 1 || major > 3 ||
        (major == 1 && minor > (request.pixel ? 4u : 1u)) || (major == 2 && minor > 1) || (major == 3 && minor))
        return Fail(error, "unsupported or mismatched legacy shader profile");
    const size_t count = request.byteCount / 4;
    for (size_t offset = 1; offset < count;)
    {
        const uint32_t instruction = Token(request.legacyBytes, offset), opcode = instruction & D3DSI_OPCODE_MASK;
        if (instruction == D3DSIO_END)
            return offset + 1 == count || Fail(error, "trailing data after legacy shader END");
        size_t length;
        if (opcode == D3DSIO_COMMENT)
            length = 1 + ((instruction & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
        else
        {
            const int operands = OperandCount(opcode, version);
            if (operands < 0) return Fail(error, "unknown legacy shader instruction");
            length = major >= 2 ? 1 + ((instruction & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT) : 1 + operands;
            if (length < 1 + static_cast<size_t>(operands)) return Fail(error, "short legacy shader instruction");
        }
        if (length > count - offset) return Fail(error, "legacy instruction exceeds byte span");
        offset += length;
    }
    return Fail(error, "legacy shader has no terminal END");
}

ShaderConv::RasterStates Raster(const ShaderRasterStateDX12 &state)
{
    ShaderConv::RasterStates result;
    for (size_t i = 0; i < state.textureTypes.size(); ++i)
    {
        result.PSSamplers[i].Value = 0;
        result.PSSamplers[i].TextureType = state.textureTypes[i];
        result.PSSamplers[i].TexCoordWrap = state.texcoordWrap[i];
    }
    result.TCIMapping = state.texcoordMapping;
    result.ProjectedTCsMask = state.projectedTexcoords; result.UserClipPlanes = state.clipPlaneMask;
    result.FillMode = state.fillMode; result.ShadeMode = state.shadeMode; result.PrimitiveType = state.primitiveType;
    result.AlphaFunc = state.alphaFunction; result.AlphaTestEnable = state.alphaTest;
    result.FogEnable = state.fog; result.FogTableMode = state.fogTableMode; result.WFogEnable = state.wFog;
    result.PointSizeEnable = state.pointSize; result.PointSpriteEnable = state.pointSprite;
    result.HasTLVertices = state.transformedVertices;
    result.HardwareShadowMappingRequiredPS = state.comparisonPixelSamplers;
    result.HardwareShadowMappingRequiredVS = state.comparisonVertexSamplers;
    // Channel conversion belongs to SRV component mapping, not a second shader swizzle.
    result.SamplerSwizzleMask = 0;
    return result;
}
bool ImportLinkage(const ShaderLinkageDX12 *source, size_t count, uint32_t centroidMask,
                   ShaderConv::VSOutputDecls &dest, std::string &error)
{
    if ((!source && count) || count > ShaderConv::VSOutputDecls::MAX_SIZE)
        return Fail(error, "invalid predecessor linkage span");
    for (size_t i = 0; i < count; ++i)
    {
        const auto &entry = source[i];
        if (entry.usage > D3DDECLUSAGE_POINTSPRITE || entry.usageIndex >= 16 || entry.registerIndex >= 16 || !entry.writeMask || entry.writeMask > 15)
            return Fail(error, "invalid predecessor linkage declaration");
        const bool centroid = entry.centroid || (entry.usage == D3DDECLUSAGE_TEXCOORD && (centroidMask & (1u << entry.usageIndex)));
        dest.AddDecl(entry.usage, entry.usageIndex, entry.registerIndex, entry.writeMask << 16, centroid);
    }
    return true;
}
void ExportLinkage(const ShaderConv::VSOutputDecls &source, std::vector<ShaderLinkageDX12> &dest)
{
    dest.reserve(source.GetSize());
    for (UINT i = 0; i < source.GetSize(); ++i)
    {
        const auto &entry = source[i];
        dest.push_back({entry.Usage, entry.UsageIndex, entry.RegIndex, entry.WriteMask, (source.CentroidMask & (uint64_t(1) << i)) != 0});
    }
}
const char *Semantic(uint32_t usage, bool vertexInput)
{
    // Usage order is the D3D9 declaration ABI, including converter system semantics.
    static const char *const names[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE", "SV_IsFrontFace", "SV_POSITION", "SV_ClipDistance", "POINTSPRITE"};
    if (!vertexInput && usage == D3DDECLUSAGE_POSITION) return "SV_POSITION";
    return usage < _countof(names) ? names[usage] : nullptr;
}
// Serialized ISG1/OSG1 parameter layout (_D3D11_INTERNALSHADER_PARAMETER_11_1).
struct SignatureParameter
{
    uint32_t stream = 0, semanticName = 0, semanticIndex = 0, systemValue = 0, componentType = 3, reg = 0;
    uint8_t mask = 0, usedMask = 0;
    uint16_t padding = 0;
    uint32_t minPrecision = 0;
};
static_assert(sizeof(SignatureParameter) == 32, "DXBC signature record ABI");
struct NamedParameter { SignatureParameter parameter; const char *name; };
std::vector<uint8_t> PackSignature(std::vector<NamedParameter> &parameters)
{
    std::stable_sort(parameters.begin(), parameters.end(), [](const NamedParameter &a, const NamedParameter &b) { return a.parameter.reg < b.parameter.reg; });
    size_t size = 8 + parameters.size() * sizeof(SignatureParameter);
    for (const auto &entry : parameters) size += strlen(entry.name) + 1;
    std::vector<uint8_t> bytes((size + 3) & ~size_t(3), 0);
    const uint32_t header[2] = {static_cast<uint32_t>(parameters.size()), 8};
    memcpy(bytes.data(), header, sizeof(header));
    size_t stringOffset = 8 + parameters.size() * sizeof(SignatureParameter);
    for (size_t i = 0; i < parameters.size(); ++i)
    {
        auto parameter = parameters[i].parameter;
        parameter.semanticName = static_cast<uint32_t>(stringOffset);
        memcpy(bytes.data() + 8 + i * sizeof(parameter), &parameter, sizeof(parameter));
        const size_t length = strlen(parameters[i].name) + 1;
        memcpy(bytes.data() + stringOffset, parameters[i].name, length); stringOffset += length;
    }
    return bytes;
}
std::vector<uint8_t> LinkageSignature(const ShaderConv::VSOutputDecls &decls)
{
    std::vector<NamedParameter> parameters; parameters.reserve(decls.GetSize());
    for (UINT i = 0; i < decls.GetSize(); ++i)
    {
        const auto &decl = decls[i]; NamedParameter entry{};
        entry.name = Semantic(decl.Usage, false);
        entry.parameter.semanticIndex = decl.UsageIndex; entry.parameter.reg = decl.RegIndex; entry.parameter.mask = decl.WriteMask;
        if (decl.Usage == D3DDECLUSAGE_POSITION || decl.Usage == D3DDECLUSAGE_VPOS) entry.parameter.systemValue = D3D_NAME_POSITION;
        if (decl.Usage == D3DDECLUSAGE_CLIPDISTANCE) entry.parameter.systemValue = D3D_NAME_CLIP_DISTANCE;
        if (decl.Usage == D3DDECLUSAGE_VFACE) { entry.parameter.systemValue = D3D_NAME_IS_FRONT_FACE; entry.parameter.componentType = D3D_REGISTER_COMPONENT_UINT32; }
        parameters.push_back(entry);
    }
    return PackSignature(parameters);
}
struct ConvertedTokens
{
    ShaderConv::ByteCode &code;
    ~ConvertedTokens() { ShaderConv::ShaderConverterAPI::CleanUpConvertedShader(code); }
};
bool Assemble(const ShaderConv::ByteCode &tokens, const std::vector<uint8_t> &input, const std::vector<uint8_t> &output,
              SignDxbcFnDX12 signer, std::vector<uint8_t> &bytecode, std::string &error)
{
    if (!signer || !tokens.m_pByteCode || tokens.m_byteCodeSize < 8 || tokens.m_byteCodeSize > UINT32_MAX)
        return Fail(error, "missing signer or converted shader tokens");
    CDXBCBuilder builder(false);
    if (!CheckResult(builder.AppendBlob(DXBC_InputSignature11_1, static_cast<UINT32>(input.size()), input.data()), "append input signature", error) ||
        !CheckResult(builder.AppendBlob(DXBC_OutputSignature11_1, static_cast<UINT32>(output.size()), output.data()), "append output signature", error) ||
        !CheckResult(builder.AppendBlob(DXBC_GenericShader, static_cast<UINT32>(tokens.m_byteCodeSize), tokens.m_pByteCode), "append shader", error)) return false;
    UINT32 size = 0;
    if (!CheckResult(builder.GetFinalDXBC(nullptr, &size), "size DXBC", error) || !size) return false;
    bytecode.resize(size);
    if (!CheckResult(builder.GetFinalDXBC(bytecode.data(), &size), "assemble DXBC", error) ||
        !CheckResult(signer(bytecode.data(), size), "sign DXBC", error)) { bytecode.clear(); return false; }
    return true;
}
} // namespace

bool CShaderTranslatorDX12::TranslateLegacy(const ShaderTranslationRequestDX12 &request, SignDxbcFnDX12 signer,
                                           ShaderTranslationResultDX12 &result, std::string &error) const
{
    result = {}; error.clear();
    ZoneScopedN("DX12 TranslateLegacy");
    if (!ValidateLegacy(request, error)) return false;
    if ((!request.vertexInputs && request.vertexInputCount) || request.vertexInputCount > ShaderConv::MAX_VS_INPUT_REGS)
        return Fail(error, "invalid vertex declaration span");
    try
    {
        ShaderTranslationResultDX12 converted;
        ShaderConv::VSInputDecls inputs(ShaderConv::MAX_VS_INPUT_REGS);
        for (size_t i = 0; i < request.vertexInputCount; ++i)
        {
            const auto &input = request.vertexInputs[i];
            if (input.usage > D3DDECLUSAGE_SAMPLE || input.usageIndex >= 16 || input.registerIndex >= 16 || input.conversion > 3 || input.componentType < 1 || input.componentType > 3)
                return Fail(error, "invalid vertex declaration element");
            inputs.AddDecl(input.usage, input.usageIndex, input.registerIndex, input.transformedPosition, input.conversion);
        }
        ShaderConv::VSOutputDecls linked, outputs;
        if (!ImportLinkage(request.linkedOutputs, request.linkedOutputCount, request.centroidTexcoordMask, linked, error)) return false;
        const auto raster = Raster(request.raster);
        ShaderConv::ConvertShaderArgs args(9, ShaderConv::AnythingTimes0Equals0, raster);
        args.type = request.pixel ? ShaderConv::ConvertShaderArgs::SHADER_TYPE::SHADER_TYPE_PIXEL : ShaderConv::ConvertShaderArgs::SHADER_TYPE::SHADER_TYPE_VERTEX;
        args.outputRegistersMask = 0;
        args.pVsInputDecl = &inputs; args.pVsOutputDecl = &outputs; args.pPsInputDecl = request.pixel ? &linked : nullptr;
        args.legacyByteCode.m_pByteCode = const_cast<void *>(request.legacyBytes); args.legacyByteCode.m_byteCodeSize = request.byteCount;
        ConvertedTokens cleanup{args.convertedByteCode};
        ShaderConv::ShaderConverterAPI compiler;
        if (!CheckResult(compiler.ConvertShader(args), "convert legacy shader", error)) return false;
        std::vector<uint8_t> inputSignature, outputSignature;
        if (request.pixel)
        {
            for (const auto &semantic : args.AddedSystemSemantics) linked.AddDecl(semantic);
            ExportLinkage(linked, converted.inputLinkage);
            inputSignature = LinkageSignature(linked);
            std::vector<NamedParameter> parameters;
            for (UINT i = 0; i < ShaderConv::MAX_PS_COLOROUT_REGS; ++i)
            {
                if (!(args.outputRegistersMask & (1u << i))) continue;
                NamedParameter entry{}; entry.name = "SV_Target"; entry.parameter.semanticIndex = i;
                entry.parameter.systemValue = D3D_NAME_TARGET; entry.parameter.reg = i; entry.parameter.mask = 15;
                parameters.push_back(entry);
            }
            if (args.outputRegistersMask & ShaderConv::DEPTH_OUTPUT_MASK)
            {
                NamedParameter entry{}; entry.name = "SV_Depth"; entry.parameter.systemValue = D3D_NAME_DEPTH;
                entry.parameter.reg = UINT32_MAX; entry.parameter.mask = 1; parameters.push_back(entry);
            }
            outputSignature = PackSignature(parameters);
        }
        else
        {
            std::vector<NamedParameter> parameters;
            for (size_t i = 0; i < request.vertexInputCount; ++i)
            {
                auto input = request.vertexInputs[i];
                const UINT reg = inputs.FindRegisterIndex(input.usage, input.usageIndex);
                if (reg == ShaderConv::VSInputDecls::INVALID_INDEX) continue;
                input.registerIndex = reg; converted.vertexInputs.push_back(input);
                NamedParameter entry{}; entry.name = Semantic(input.usage, true); entry.parameter.semanticIndex = input.usageIndex;
                entry.parameter.componentType = input.componentType; entry.parameter.reg = reg; entry.parameter.mask = entry.parameter.usedMask = 15;
                parameters.push_back(entry);
            }
            inputSignature = PackSignature(parameters);
            ExportLinkage(outputs, converted.outputLinkage);
            outputSignature = LinkageSignature(outputs);
        }
        if (!Assemble(args.convertedByteCode, inputSignature, outputSignature, signer, converted.bytecode, error)) return false;
        for (size_t file = 0; file < 3; ++file)
        {
            converted.inlineConstants[file].reserve(args.m_inlineConsts[file].size());
            for (const auto &constant : args.m_inlineConsts[file])
            {
                // Float/int offsets count scalar words; the renderer indexes float4/int4 registers.
                ShaderInlineConstantDX12 entry; entry.registerIndex = file == 2 ? constant.RegIndex : constant.RegIndex / 4;
                memcpy(entry.words.data(), constant.Value, file == 2 ? sizeof(uint32_t) : sizeof(constant.Value));
                converted.inlineConstants[file].push_back(entry);
            }
        }
        converted.maxFloatConstants = (args.maxFloatConstsUsed + 3) / 4;
        converted.maxIntConstants = (args.maxIntConstsUsed + 3) / 4;
        converted.maxBoolConstants = args.maxBoolConstsUsed; converted.outputRegistersMask = args.outputRegistersMask;
        converted.usedSamplerMask = args.usedSamplerMask;
        result = std::move(converted); return true;
    }
    catch (const std::exception &exception) { error = std::string("shader reconstruction: ") + exception.what(); return false; }
    catch (...) { return Fail(error, "shader reconstruction raised an exception"); }
}

bool CShaderTranslatorDX12::GenerateGeometry(const ShaderLinkageDX12 *inputs, size_t inputCount,
                                            const ShaderRasterStateDX12 &state, SignDxbcFnDX12 signer,
                                            ShaderTranslationResultDX12 &result, std::string &error) const
{
    ZoneScopedN("DX12 GenerateGeometry");
    result = {}; error.clear();
    try
    {
        ShaderTranslationResultDX12 converted;
        ShaderConv::VSOutputDecls input, output;
        if (!ImportLinkage(inputs, inputCount, 0, input, error)) return false;
        const auto raster = Raster(state);
        ShaderConv::CreateGeometryShaderArgs args(9, 0, input, &output, raster);
        ConvertedTokens cleanup{args.m_GSByteCode}; ShaderConv::ShaderConverterAPI compiler;
        if (!CheckResult(compiler.CreateGeometryShader(args), "generate geometry shader", error)) return false;
        const auto inputSignature = LinkageSignature(input), outputSignature = LinkageSignature(output);
        if (!Assemble(args.m_GSByteCode, inputSignature, outputSignature, signer, converted.bytecode, error)) return false;
        ExportLinkage(input, converted.inputLinkage); ExportLinkage(output, converted.outputLinkage);
        result = std::move(converted); return true;
    }
    catch (const std::exception &exception) { error = std::string("geometry reconstruction: ") + exception.what(); return false; }
    catch (...) { return Fail(error, "geometry reconstruction raised an exception"); }
}
} // namespace shaderapidx12
