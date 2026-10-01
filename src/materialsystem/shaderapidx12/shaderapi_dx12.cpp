// Source matrix, lighting and standard-constant CPU behavior adapted from Valve's shader API.
// Copyright Valve Corporation, All rights reserved.
#include "shaderapi_dx12.h"
#include "shadershadow_dx12.h"
#include "hardwareconfig_dx12.h"
#include "materialsystem/stdshaders/common_hlsl_cpp_consts.h"
#include "shaderapi/ishaderutil.h"
#include "materialsystem/materialsystem_config.h"
#include "materialsystem/ishadersystem_declarations.h"
#include "renderparm.h"
#include "tier1/keyvalues.h"
#include "tier1/convar.h"
#include "tier1/utlsymbol.h"
#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <climits>

#include "tracy_dx12.h"
#include "shaderapi/commandbuffer.h"
#include <cmath>
#include <emmintrin.h>

using namespace shaderapidx12;
static uint64_t TranslationLayoutKey(const VertexLayoutDX12 &layout)
{
    uint64_t key=1469598103934665603ull;
    const auto mix=[&](uint64_t value){key^=value;key*=1099511628211ull;};
    mix(layout.stride);mix(layout.inputCount);
    for(uint32_t i=0;i<layout.inputCount;++i){for(const char *name=layout.inputs[i].semantic;*name;++name)mix(static_cast<unsigned char>(*name));mix(layout.inputs[i].semanticIndex);mix(layout.inputs[i].format);mix(layout.inputs[i].inputSlot);mix(layout.inputs[i].byteOffset);mix(layout.inputs[i].integerToFloat);}
    return key;
}
// Reflects every register-space-1 cbuffer of a native record once: name, binding, size, member
// table and the canonical FNV-1a layout hash shared with the packer and generated C++ blocks.
static void HashReflectedTypeDX12(ID3D12ShaderReflectionType *type,const D3D12_SHADER_TYPE_DESC &desc,std::string &canonical)
{
    canonical+=std::to_string(static_cast<int>(desc.Class))+","+std::to_string(static_cast<int>(desc.Type))+","+std::to_string(desc.Rows)+","+std::to_string(desc.Columns)+","+std::to_string(desc.Elements);
    if(desc.Class!=D3D_SVC_STRUCT)return;
    canonical+="{";
    for(UINT i=0;i<desc.Members;++i){
        ID3D12ShaderReflectionType *member=type->GetMemberTypeByIndex(i);D3D12_SHADER_TYPE_DESC memberDesc{};
        if(!member||FAILED(member->GetDesc(&memberDesc)))continue;
        canonical+=std::string(type->GetMemberTypeName(i))+":";HashReflectedTypeDX12(member,memberDesc,canonical);
        UINT end=0;
        if(i+1<desc.Members){D3D12_SHADER_TYPE_DESC next{};if(SUCCEEDED(type->GetMemberTypeByIndex(i+1)->GetDesc(&next)))end=next.Offset;}
        else end=desc.Elements?memberDesc.Offset+desc.Elements:memberDesc.Offset;
        canonical+=":"+std::to_string(memberDesc.Offset)+":"+std::to_string(end>=memberDesc.Offset?end-memberDesc.Offset:0)+";";
    }
    canonical+="}";
}
bool shaderapidx12::ReflectNativeCBuffersDX12(ShaderRecordDX12 *record)
{
    if(!record||!record->legacyBytecode.empty()||record->nativeReflectionReady)return true;
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    const auto bytecode=record->Bytecode();
    if(!bytecode.pShaderBytecode||FAILED(D3DReflect(bytecode.pShaderBytecode,bytecode.BytecodeLength,IID_PPV_ARGS(&reflection))))return false;
    D3D12_SHADER_DESC shader{};if(FAILED(reflection->GetDesc(&shader)))return false;
    record->nativeCBuffers.clear();record->nativeAbiHash=dx12native::kFnvOffset;
    for(UINT i=0;i<shader.BoundResources;++i){
        D3D12_SHADER_INPUT_BIND_DESC binding{};if(FAILED(reflection->GetResourceBindingDesc(i,&binding)))return false;
        if(binding.Type!=D3D_SIT_CBUFFER)continue;
        ShaderRecordDX12::NativeCBufferBindingDX12 reflected;reflected.name=binding.Name;reflected.shaderRegister=binding.BindPoint;reflected.registerSpace=binding.Space;
        ID3D12ShaderReflectionConstantBuffer *buffer=reflection->GetConstantBufferByName(binding.Name);D3D12_SHADER_BUFFER_DESC bufferDesc{};
        if(!buffer||FAILED(buffer->GetDesc(&bufferDesc)))return false;
        reflected.byteSize=bufferDesc.Size;
        std::string canonical=reflected.name+"|"+std::to_string(bufferDesc.Size)+";";
        for(UINT v=0;v<bufferDesc.Variables;++v){
            ID3D12ShaderReflectionVariable *variable=buffer->GetVariableByIndex(v);D3D12_SHADER_VARIABLE_DESC variableDesc{};D3D12_SHADER_TYPE_DESC typeDesc{};
            if(!variable||FAILED(variable->GetDesc(&variableDesc))||!variable->GetType()||FAILED(variable->GetType()->GetDesc(&typeDesc)))return false;
            canonical+=std::string(variableDesc.Name)+":";HashReflectedTypeDX12(variable->GetType(),typeDesc,canonical);
            canonical+=":"+std::to_string(variableDesc.StartOffset)+":"+std::to_string(variableDesc.Size)+";";
            reflected.members.push_back({variableDesc.Name,variableDesc.StartOffset,variableDesc.Size});
        }
        reflected.layoutHash=dx12native::HashString(canonical.c_str());
        record->nativeAbiHash=dx12native::HashBytes(reflected.name.c_str(),reflected.name.size(),record->nativeAbiHash);
        record->nativeAbiHash=(record->nativeAbiHash^reflected.layoutHash)*dx12native::kFnvPrime;
        record->nativeCBuffers.push_back(std::move(reflected));
    }
    record->nativeReflectionReady=true;return true;
}
static uint64_t TranslationStateKey(uint64_t key,const ShaderRasterStateDX12 &raster)
{
    ZoneNamedN(stateKey, "DX12 TranslationStateKey", DX12_DRAW_ZONES_ACTIVE);
    const auto mix=[&](uint64_t value){key^=value;key*=1099511628211ull;};
    for(uint8_t type:raster.textureTypes)mix(type);
    for(uint8_t wrap:raster.texcoordWrap)mix(wrap);
    mix(raster.texcoordMapping);mix(raster.projectedTexcoords);mix(raster.comparisonPixelSamplers);mix(raster.comparisonVertexSamplers);mix(raster.clipPlaneMask);mix(raster.alphaTest);mix(raster.alphaFunction);mix(raster.fog);mix(raster.fogTableMode);mix(raster.primitiveType);mix(raster.fillMode);mix(raster.shadeMode);mix(raster.wFog);mix(raster.pointSize);mix(raster.pointSprite);mix(raster.transformedVertices);
    return key;
}
// Makes the stored translation `key` the record's active variant; false when it was never translated.
static bool ActivateTranslatedVariant(ShaderRecordDX12 *record,uint64_t key)
{
    if(record->activeVariantValid && record->activeVariantKey==key) return true;
    for(int index=0;index<record->variants.Count();++index)
    {
        auto &variant=record->variants[index];
        if(variant.key!=key)continue;
        record->translated.Swap(variant.result);
        record->inputSignature.swap(variant.inputSignature);
        std::swap(record->inputSignatureReady,variant.inputSignatureReady);
        record->derived.Swap(variant.derived);
        std::swap(record->activeVariantKey,variant.key);
        record->activeVariantValid=true;record->linkageHashValid=false;record->constantLayoutValid=false;
        return true;
    }
    return false;
}
static bool TranslateVariant(ShaderRecordDX12 *record,bool pixel,const VertexLayoutDX12 &layout,const ShaderLinkageDX12 *linked,size_t linkedCount,const ShaderRasterStateDX12 &raster,uint64_t key,SignDxbcFnDX12 signer);
bool EnsureTranslated(ShaderRecordDX12 *record,bool pixel,const VertexLayoutDX12 &layout,const ShaderLinkageDX12 *linked,size_t linkedCount,uint64_t linkageHash,const ShaderRasterStateDX12 &raster,uint64_t stateKey,SignDxbcFnDX12 signer)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 EnsureTranslated", DX12_DRAW_ZONES_ACTIVE);
    if (!record) return false;
    if (record->legacyBytecode.empty()) return record->bytecode.size()>=4;
    if (!signer) return false;
    uint64_t key=stateKey;
    const auto mix=[&](uint64_t value){key^=value;key*=1099511628211ull;};
    mix(pixel);
    // linkageHash summarizes linked[0..linkedCount); translation still reads the array.
    if(linkedCount)mix(linkageHash);
    if(ActivateTranslatedVariant(record,key)) return true;
    return TranslateVariant(record,pixel,layout,linked,linkedCount,raster,key,signer);
}
// Out of line: translation locals would otherwise enlarge every caller's stack frame (DrawBuffers is per draw).
static __declspec(noinline) bool TranslateVariant(ShaderRecordDX12 *record,bool pixel,const VertexLayoutDX12 &layout,const ShaderLinkageDX12 *linked,size_t linkedCount,const ShaderRasterStateDX12 &raster,uint64_t key,SignDxbcFnDX12 signer)
{
    ZoneNamedN(translationMiss, "DX12 TranslateMiss", DX12_ZONES_ACTIVE);
    std::array<ShaderVertexInputDX12,MAX_VERTEX_INPUTS_DX12> inputs{};
    for(uint32_t i=0;i<layout.inputCount;++i){const char *s=layout.inputs[i].semantic;uint32_t usage=0,index=layout.inputs[i].semanticIndex;if(!std::strcmp(s,"POSITION"))usage=0;else if(!std::strcmp(s,"BLENDWEIGHT"))usage=1;else if(!std::strcmp(s,"BLENDINDICES"))usage=2;else if(!std::strcmp(s,"NORMAL"))usage=3;else if(!std::strcmp(s,"PSIZE"))usage=4;else if(!std::strcmp(s,"TEXCOORD"))usage=5;else if(!std::strcmp(s,"TANGENT"))usage=6;else if(!std::strcmp(s,"BINORMAL"))usage=7;else if(!std::strcmp(s,"COLOR"))usage=10;const bool convert=layout.inputs[i].integerToFloat;inputs[i]={usage,index,i,convert?1u:0u,convert?(layout.inputs[i].format==DXGI_FORMAT_R16G16_SINT?2u:1u):3u,false};}
    ShaderTranslationRequestDX12 request{};
    request.legacyBytes=record->legacyBytecode.data();request.byteCount=record->legacyBytecode.size();request.pixel=pixel;request.vertexInputs=inputs.data();request.vertexInputCount=layout.inputCount;request.linkedOutputs=linked;request.linkedOutputCount=linkedCount;request.raster=raster;
    request.centroidTexcoordMask=record->centroidTexcoordMask;
    CShaderTranslatorDX12 translator;ShaderTranslationResultDX12 translated;std::string error;
    if(!translator.TranslateLegacy(request,signer,translated,error)){Warning("ShaderAPIDX12: deferred shader translation failed: %s\n",error.c_str());return false;}
    if(record->activeVariantValid){
        auto &variant=record->variants[record->variants.AddToTail()];
        variant.key=record->activeVariantKey;
        variant.result=std::move(record->translated);
        variant.inputSignature=std::move(record->inputSignature);
        variant.inputSignatureReady=record->inputSignatureReady;
        variant.derived.Swap(record->derived);
    }
    record->translated=std::move(translated);record->activeVariantKey=key;record->activeVariantValid=true;record->inputSignatureReady=false;record->inputSignature.clear();record->derived={};record->linkageHashValid=false;record->constantLayoutValid=false;
    return !record->translated.bytecode.empty();
}
struct CShaderAPIDX12::OcclusionQueryDX12
{
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    uint64_t fence = 0;
    bool active = false, ended = false, error = false, destroyed = false;
};

std::unique_ptr<CShaderAPIDX12::OcclusionQueryDX12> CShaderAPIDX12::CreateOcclusionQuery()
{
    if (!device_ || !device_->NativeDevice()) return nullptr;
    auto query=std::make_unique<OcclusionQueryDX12>();
    D3D12_QUERY_HEAP_DESC heapDesc{};heapDesc.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION;heapDesc.Count=1;
    if(FAILED(device_->NativeDevice()->CreateQueryHeap(&heapDesc,IID_PPV_ARGS(&query->heap))))return nullptr;
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=sizeof(uint64_t);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->NativeDevice()->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&query->readback))))return nullptr;
    return query;
}


namespace shaderapidx12
{
CShaderAPIDX12 *g_pShaderAPIDX12 = nullptr;
// Snapshot shader names are interned so snapshot copies and comparisons avoid string allocation.
// The pool only grows; returned pointers stay valid for the module lifetime.
InternedNameDX12 InternShaderNameDX12(const char *name)
{
    if(!name||!*name)return {};
    static CUtlSymbolTableMT table(0,64,false);
    return InternedNameDX12{table.String(table.AddString(name))};
}
// The installed x64 material runtime implements these virtuals after the public
// IMaterial prefix. DrawMesh is slot 71 for both CMaterial and CMaterialSubRect;
// no engine function addresses or exported interface headers are changed.
class IMaterialDrawDX12 : public IMaterial
{
public:
 virtual int GetReferenceCount() const=0;
 virtual void SetEnumerationID(int)=0;
 virtual void SetNeedsWhiteLightmap(bool)=0;
 virtual bool GetNeedsWhiteLightmap() const=0;
 virtual void Uncache(bool)=0;
 virtual void Precache()=0;
 virtual bool PrecacheVars(KeyValues *,KeyValues *,void *,int)=0;
 virtual void ReloadTextures()=0;
 virtual void SetMinLightmapPageID(int)=0;
 virtual void SetMaxLightmapPageID(int)=0;
 virtual int GetMinLightmapPageID() const=0;
 virtual int GetMaxLightmapPageID() const=0;
 virtual void *GetShader() const=0;
 virtual bool IsPrecachedVars() const=0;
 virtual void DrawMesh(VertexCompressionType_t)=0;
};

CShaderAPIDX12::CShaderAPIDX12() { for(auto &m:matrices_)m.Identity(); ResetNativeState();g_pShaderAPIDX12=this; }
CShaderAPIDX12::~CShaderAPIDX12()
{
 ShutdownDeviceResources();
 dynamicMeshes_.PurgeAndDeleteElements();
 if(g_pShaderAPIDX12==this)g_pShaderAPIDX12=nullptr;
 FOR_EACH_HASHTABLE(textures_,entry){delete textures_[entry];}
 FOR_EACH_HASHTABLE(namedShaderCombos_,entry){delete namedShaderCombos_[entry];}
 FOR_EACH_HASHTABLE(namedShaderFiles_,entry){delete namedShaderFiles_[entry];}
 for(auto entry=fixedShaders_.FirstInorder();entry!=fixedShaders_.InvalidIndex();entry=fixedShaders_.NextInorder(entry)){delete fixedShaders_[entry];}
 if(debugTextureEntries_)debugTextureEntries_->deleteThis();
}
ShaderRecordDX12 *CShaderAPIDX12::ResolveNamedShader(const char *name,bool pixel,int staticIndex,int dynamicIndex)
{
 ZoneNamedN(___tracy_scoped_zone, "DX12 ResolveNamedShader", DX12_DRAW_ZONES_ACTIVE);
 if(!device_||!g_pShaderDeviceMgrDX12||!g_pShaderDeviceMgrDX12->HostFileSystem()||!*name)return nullptr;
 const NamedShaderKeyView referenceKey{name,staticIndex,-1,pixel};
 auto &reference=namedReferenceHints_[pixel?1:0];
 if(!namedShaderReferences_.IsValidHandle(reference)||!NamedShaderEqual()(namedShaderReferences_.Key(reference),referenceKey))reference=namedShaderReferences_.Insert(referenceKey,true);
 namedShaderReferences_[reference]=true;
 const int dynamic=std::max(0,dynamicIndex);
 const NamedShaderKeyView key{name,staticIndex,dynamic,pixel};
 auto &found=namedComboHints_[pixel?1:0];
 if(!namedShaderCombos_.IsValidHandle(found)||!NamedShaderEqual()(namedShaderCombos_.Key(found),key))found=namedShaderCombos_.Find(key);
 if(found!=namedShaderCombos_.InvalidHandle())return namedShaderCombos_[found];
 found=namedShaderCombos_.Insert(key,nullptr);
 CUtlString fileKey(pixel?"p:":"v:");fileKey.Append(name);
 auto shaderFile=namedShaderFiles_.Find(fileKey.String());
 if(shaderFile==namedShaderFiles_.InvalidHandle()){
  auto file=std::make_unique<ShaderVcsFile>();std::string error;
  if(!file->Open(*g_pShaderDeviceMgrDX12->HostFileSystem(),name,pixel?VcsStage::Pixel:VcsStage::Vertex,error)){
   Warning("ShaderAPIDX12: unable to load named shader %s: %s\n",name,error.c_str());namedShaderFiles_.Insert(fileKey.String(),nullptr);return nullptr;
  }
  // -dx12shaderlog: one line per named shader file, stating which VCS path won (shaders/vsh|psh native DXBC or
  // shaders/fxc legacy). Development diagnostic; files open once, so this never runs per draw.
  static const bool logNamedShaders=CommandLine()&&CommandLine()->CheckParm("-dx12shaderlog");
  if(logNamedShaders)Msg("ShaderAPIDX12: %s shader %s from %s\n",pixel?"pixel":"vertex",name,file->Path().c_str());
  shaderFile=namedShaderFiles_.Insert(fileKey.String(),file.release());
 }
 auto *file=namedShaderFiles_[shaderFile];
 if(!file)return nullptr;
 if(staticIndex<0||static_cast<uint32_t>(dynamic)>=file->DynamicComboCount()){
  Warning("ShaderAPIDX12: shader %s has invalid static %d / dynamic %d combo\n",name,staticIndex,dynamic);return nullptr;
 }
 std::string error;
 if(!file->LoadStaticCombo(static_cast<uint32_t>(staticIndex),error)){
  Warning("ShaderAPIDX12: shader %s combo decode failed: %s\n",name,error.c_str());return nullptr;
 }
 const VcsPayload *payload=file->DynamicPayload(static_cast<uint32_t>(staticIndex),static_cast<uint32_t>(dynamic));
 if(!payload){Warning("ShaderAPIDX12: shader %s missing static %d / dynamic %d combo\n",name,staticIndex,dynamic);return nullptr;}
 class ShaderBufferView final : public IShaderBuffer {
 public: explicit ShaderBufferView(const VcsPayload &value):payload(value){}
  size_t GetSize() const override{return payload.tokens.size();}
  const void *GetBits() const override{return payload.tokens.data();}
  void Release() override{Assert(0);}
 private: const VcsPayload &payload;
 } buffer(*payload);
 auto *record=pixel?reinterpret_cast<ShaderRecordDX12 *>(device_->CreatePixelShader(&buffer)):reinterpret_cast<ShaderRecordDX12 *>(device_->CreateVertexShader(&buffer));
 if(!record){Warning("ShaderAPIDX12: failed to create %s shader %s combo %d/%d\n",pixel?"pixel":"vertex",name,staticIndex,dynamic);return nullptr;}
 record->centroidTexcoordMask=file->CentroidMask();
 found=namedShaderCombos_.Find(key);namedShaderCombos_[found]=record;return record;
}
ShaderRecordDX12 *CShaderAPIDX12::ResolveActiveNamedShader(bool pixel,int dynamicIndex)
{
 const char *name=(pixel?activeSnapshot_.pixelShaderName:activeSnapshot_.vertexShaderName).c_str();
 const int staticIndex=pixel?activeSnapshot_.staticPixelIndex:activeSnapshot_.staticVertexIndex;
 if(activeSnapshotId_<0)return ResolveNamedShader(name,pixel,staticIndex,dynamicIndex);
 auto &cache=namedResolveCache_[pixel?1:0];
 auto &entry=cache[Mix32HashFunctor()(static_cast<uint32_t>(activeSnapshotId_)*0x9E3779B1u^static_cast<uint32_t>(dynamicIndex))&(cache.size()-1)];
 if(entry.record&&entry.epoch==namedResolveEpoch_&&entry.snapshot==activeSnapshotId_&&entry.dynamicIndex==dynamicIndex){
  if(entry.referenceEpoch==namedReferenceEpoch_)return entry.record;
  // Reference marks were cleared: re-mark through the remembered handle (hints can move on rehash, so verify the key).
  const NamedShaderKeyView referenceKey{name,staticIndex,-1,pixel};
  if(namedShaderReferences_.IsValidHandle(entry.reference)&&NamedShaderEqual()(namedShaderReferences_.Key(entry.reference),referenceKey)){
   namedShaderReferences_[entry.reference]=true;entry.referenceEpoch=namedReferenceEpoch_;return entry.record;
  }
 }
 auto *record=ResolveNamedShader(name,pixel,staticIndex,dynamicIndex);
 // Failures are not cached: they may depend on device or filesystem availability.
 entry={activeSnapshotId_,dynamicIndex,namedResolveEpoch_,record,namedReferenceHints_[pixel?1:0],namedReferenceEpoch_};
 return record;
}

void CShaderAPIDX12::DrawMaterialMesh(CMeshDX12 *mesh,int firstIndex,int indexCount)
{
 ZoneNamedN(___tracy_scoped_zone, "DX12 MaterialDraw", DX12_DRAW_ZONES_ACTIVE);
 { ZoneNamedN(materialSync, "DX12 MaterialSyncMatrices", DX12_DRAW_ZONES_ACTIVE); if(shaderUtil_)shaderUtil_->SyncMatrices(); }
 { ZoneNamedN(materialTransforms, "DX12 MaterialTransforms", DX12_DRAW_ZONES_ACTIVE); CommitTransforms(); }
 { ZoneNamedN(materialLighting, "DX12 MaterialLighting", DX12_DRAW_ZONES_ACTIVE); CommitVertexLighting(); }
 if(!mesh||!boundMaterial_){DrawMesh(mesh,firstIndex,indexCount);return;}
 if(shaderUtil_&&shaderUtil_->GetConfig().m_bSuppressRendering)return;
 CMeshDX12 *previousMesh=renderMesh_;const int previousFirst=renderFirstIndex_,previousCount=renderIndexCount_;
 renderMesh_=mesh;renderFirstIndex_=firstIndex;renderIndexCount_=indexCount;
 { ZoneNamedN(materialDispatch, "DX12 MaterialShaderDispatch", DX12_DRAW_ZONES_ACTIVE); reinterpret_cast<IMaterialDrawDX12 *>(boundMaterial_)->DrawMesh(CompressionType(mesh->GetVertexFormat())); }
 renderMesh_=previousMesh;renderFirstIndex_=previousFirst;renderIndexCount_=previousCount;
}
void CShaderAPIDX12::DrawMesh(CMeshDX12 *mesh,int firstIndex,int indexCount)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 DrawMesh", DX12_DRAW_ZONES_ACTIVE);
 if(!mesh)return;
 // Mesh draws use streams 0-2 only; slots 3-15 of the persistent array stay empty.
 auto &bindings=drawMeshBindings_;
 bindings[0]={&mesh->DrawVertices(),0,0,static_cast<uint32_t>(mesh->DrawVertices().WrittenCount()),1,mesh->GetVertexFormat()};
 const auto auxiliary=[&](IMesh *source,int offset,unsigned slot)->bool{
  if(!source){bindings[slot]={};return true;}
  auto &vertices=static_cast<CMeshDX12 *>(source)->Vertices();
  const size_t bytes=static_cast<size_t>(vertices.WrittenCount())*vertices.Stride();
  if(offset<0||!vertices.Stride()||static_cast<size_t>(offset)>bytes)return false;
  bindings[slot]={&vertices,static_cast<uint32_t>(offset),0,static_cast<uint32_t>((bytes-offset)/vertices.Stride()),1,vertices.GetVertexFormat()};
  return true;
 };
 if(!auxiliary(mesh->ColorMesh(),mesh->ColorOffset(),1)||!auxiliary(mesh->FlexMesh(),mesh->FlexOffset(),2))return;
 DrawBuffers(bindings,&mesh->DrawIndices(),0,mesh->PrimitiveType(),firstIndex,indexCount,true);
}
void CShaderAPIDX12::DrawBuffers(const std::array<VertexBindingDX12,16> &bindings,CIndexBufferDX12 *indices,
 size_t indexOffset,MaterialPrimitiveType_t primitive,int firstIndex,int indexCount,bool meshStreams)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 DrawBuffers", DX12_DRAW_ZONES_ACTIVE);
 if(!device_||disallowAccess_||!device_->CommandList()||!device_->NativeDevice()||!bindings[0].buffer||firstIndex<0||indexCount<=0)return;
 if(motionPassState_==MotionPassStateDX12::Suppressed)return;
 ++frameDrawCount_;++drawStats_.draws;
 const bool indexed=primitive!=MATERIAL_POINTS;
 const size_t indexBytes=indices?static_cast<size_t>(indices->WrittenCount())*indices->IndexSize():0;
 // Range checks without division: first+count elements fit in n bytes iff (first+count)*size <= n. Counts are
 // bounded by the (<=UINT_MAX) byte extents before multiplying, so the products cannot overflow.
 const auto fits=[](uint64_t first,uint64_t count,uint64_t size,uint64_t available){const uint64_t end=first+count;return end<=available&&end*size<=available;};
 if(indexed&&(!indices||indexOffset>indexBytes||(indexOffset&(indices->IndexSize()-1))||indexBytes-indexOffset>UINT_MAX||
    !fits(static_cast<uint32_t>(firstIndex),static_cast<uint32_t>(indexCount),indices->IndexSize(),indexBytes-indexOffset)))return;
 for(const auto &binding:bindings)if(binding.buffer){
  const size_t stride=binding.buffer->Stride(),bytes=static_cast<size_t>(binding.buffer->WrittenCount())*stride;
  if(!stride||!binding.vertexCount||binding.byteOffset>bytes||bytes>UINT_MAX||!fits(binding.firstVertex,binding.vertexCount,stride,bytes-binding.byteOffset))return;
 }
 if(!indexed&&(static_cast<uint32_t>(firstIndex)>bindings[0].firstVertex+bindings[0].vertexCount||
    static_cast<uint32_t>(indexCount)>bindings[0].firstVertex+bindings[0].vertexCount-firstIndex))return;
 if(selection_.Enabled()){
  VMatrix modelView,modelToClip;MatrixMultiply(matrices_[MATERIAL_VIEW],matrices_[MATERIAL_MODEL],modelView);MatrixMultiply(matrices_[MATERIAL_PROJECTION],modelView,modelToClip);
  if(indices)TestSelectionDX12(*bindings[0].buffer,*indices,primitive,firstIndex,indexCount,modelToClip,
   activeSnapshot_.culling&&(!rasterOverride_||rasterState_.m_bCullEnable),cullMode_==MATERIAL_CULLMODE_CW,selection_,bindings[0].byteOffset,indexOffset);
  return;
 }
 { ZoneNamedN(drawCommit, "DX12 DrawCommit", DX12_DRAW_ZONES_ACTIVE); ProcessPendingTextureDeletes();CommitTransforms();CommitFogState();CommitVertexLighting(); }
 if(namedVertexShaderDirty_){auto *record=ResolveActiveNamedShader(false,vertexShaderIndex_);boundVS_=reinterpret_cast<VertexShaderHandle_t>(record);boundVertexShaderIsNamed_=record!=nullptr;namedVertexShaderDirty_=false;}
 if(namedPixelShaderDirty_){auto *record=ResolveActiveNamedShader(true,pixelShaderIndex_);boundPS_=reinterpret_cast<PixelShaderHandle_t>(record);boundPixelShaderIsNamed_=record!=nullptr;namedPixelShaderDirty_=false;}
 VertexFormat_t format=bindings[0].format;
 const bool motionActive=MotionPassActive();
 VertexLayoutDX12 explicitLayout;
 // Mesh layouts depend only on (format, stream flags); a small direct-mapped cache covers alternating formats.
 const uint8_t meshLayoutFlags=static_cast<uint8_t>((bindings[1].buffer?1:0)|(bindings[2].buffer?2:0)|((format&VERTEX_WRINKLE)?4:0));
 auto &meshLayout=sourceLayouts_[(static_cast<uint32_t>(format)^static_cast<uint32_t>(format>>29)^static_cast<uint32_t>(format>>41)^meshLayoutFlags*0x9E3779B1u)%sourceLayouts_.size()];
 VertexLayoutDX12 &sourceLayout=meshStreams?meshLayout.layout:explicitLayout;
 { ZoneNamedN(sourceLayoutSetup, "DX12 SourceLayout", DX12_DRAW_ZONES_ACTIVE);
 if(meshStreams){
  if(!meshLayout.valid||meshLayout.format!=format||meshLayout.flags!=meshLayoutFlags){
   const VertexInputStreamsDX12 streams{bindings[1].buffer!=nullptr,bindings[2].buffer!=nullptr,false,(format&VERTEX_WRINKLE)!=0};
   sourceLayout=ComputeVertexLayoutDX12(format,nullptr,nullptr,streams);meshLayout.format=format;meshLayout.flags=meshLayoutFlags;
   meshLayout.translationKey=sourceLayout.valid?TranslationLayoutKey(sourceLayout):0;meshLayout.valid=true;
  }
  sourceLayoutTranslationKey_=meshLayout.translationKey;
 }else{
  sourceLayout={};
  sourceLayout.valid=true;sourceLayout.stride=bindings[0].buffer->Stride();
  for(unsigned slot=0;slot<bindings.size();++slot)if(bindings[slot].buffer){
   const auto &physical=bindings[slot].buffer->Layout();
   VertexLayoutDX12 subset;const VertexLayoutDX12 *usage=&physical;
   if(bindings[slot].format!=bindings[slot].buffer->GetVertexFormat()){subset=ComputeVertexLayoutDX12(bindings[slot].format);usage=&subset;}
   if(!physical.valid||!usage->valid)return;
   format|=bindings[slot].format;
   for(uint32_t i=0;i<usage->inputCount;++i){
    const VertexInputDX12 *element=nullptr;
    for(uint32_t j=0;j<physical.inputCount;++j)if(usage->inputs[i].semanticIndex==physical.inputs[j].semanticIndex&&!std::strcmp(usage->inputs[i].semantic,physical.inputs[j].semantic)){element=&physical.inputs[j];break;}
    if(!element||sourceLayout.inputCount==MAX_VERTEX_INPUTS_DX12)return;
    for(uint32_t j=0;j<sourceLayout.inputCount;++j)if(sourceLayout.inputs[j].semanticIndex==element->semanticIndex&&!std::strcmp(sourceLayout.inputs[j].semantic,element->semantic)){Warning("ShaderAPIDX12: duplicate explicit vertex semantic %s%u\n",element->semantic,element->semanticIndex);return;}
    sourceLayout.inputs[sourceLayout.inputCount]=*element;sourceLayout.inputs[sourceLayout.inputCount++].inputSlot=slot;
   }
  }
 }
 }
 if(!sourceLayout.valid)return;
 auto *vsRecord=motionActive?MotionVertexShader(format):(boundVS_==VERTEX_SHADER_HANDLE_INVALID?nullptr:reinterpret_cast<ShaderRecordDX12 *>(boundVS_));
 auto *psRecord=motionActive?motionPS_:(boundPS_==PIXEL_SHADER_HANDLE_INVALID?nullptr:reinterpret_cast<ShaderRecordDX12 *>(boundPS_));
 // Until legacy analysis exists, retain the complete binding path. Native/fixed-function
 // shaders and explicit geometry shaders also retain it; their resource use is not in this metadata.
 const auto samplerMask=[&](const ShaderRecordDX12 *record,uint32_t all){
  return boundGS_==GEOMETRY_SHADER_HANDLE_INVALID&&record&&!record->legacyBytecode.empty()&&record->activeVariantValid?record->translated.usedSamplerMask&all:all;
 };
 const uint32_t pixelSamplers=samplerMask(psRecord,0xffff),vertexSamplers=samplerMask(vsRecord,0xf);
 const uint32_t sampledMask=pixelSamplers|(vertexSamplers<<16);
 if(preparedSamplerMask_!=sampledMask){preparedSamplerTable_.count=0;preparedSamplerMask_=sampledMask;}
 // Persistent binding input: only slots sampled by the previous draw but not this one need resetting.
 auto &bindingInput=drawBindingInput_;
 if(drawBindingNull_.ptr!=pipeline_.NullShaderResourceView().ptr){bindingInput=CPipelineCacheDX12::BindingInputDX12(pipeline_.NullShaderResourceView());bindingInput.samplerDescs.fill(CPipelineCacheDX12::DefaultSamplerDesc());drawBindingNull_=pipeline_.NullShaderResourceView();drawBindingMask_=0;}
 const auto clearSlot=[&](size_t slot){bindingInput.textures[slot]=nullptr;bindingInput.srvSources[slot]=drawBindingNull_;bindingInput.samplerDescs[slot]=CPipelineCacheDX12::DefaultSamplerDesc();bindingInput.samplerIds[slot]=0;};
 for(uint32_t stale=drawBindingMask_&~sampledMask;stale;stale&=stale-1){unsigned long bit=0;_BitScanForward(&bit,stale);clearSlot(bit);}
 drawBindingMask_=sampledMask;
 // Every constant bank field is assigned below: shader banks by the constants lambda, extension banks after it.
 // Reserve a complete sampler table before recording any transient GPU addresses.
 // A flush after geometry/constant uploads could recycle addresses in this draw.
 const auto prepareTextures=[&]{
  ZoneNamedN(___tracy_scoped_zone, "DX12 PrepareTextures", DX12_DRAW_ZONES_ACTIVE);
  const auto prepareSlot=[&](size_t slot,ShaderAPITextureHandle_t handle,bool srgb,bool comparison){
   auto &cached=preparedTextureSlots_[slot];
   // No texture changed sampled state, sampler parameters, resource or lifetime since this slot was prepared.
   if(cached.valid&&cached.handle==handle&&cached.srgb==srgb&&cached.comparison==comparison&&cached.epoch==textureStateEpoch_){
    bindingInput.textures[slot]=cached.resource;bindingInput.samplerDescs[slot]=cached.sampler;bindingInput.samplerIds[slot]=cached.samplerId;bindingInput.srvSources[slot]=cached.source;
    if(!cached.source.ptr)bindingInput.srvDescs[slot]=cached.srv;
    return;
   }
   TextureRecord *record=cached.valid&&cached.record&&cached.handle==handle?cached.record:(handle>0?FindTexture(handle):nullptr);
   const bool reusable=cached.valid&&cached.handle==handle&&cached.record==record&&cached.srgb==srgb&&cached.comparison==comparison&&(!record||(record->sampledStateValid&&record->resource.Get()==cached.resource&&record->samplerDescriptorValid_[comparison?1:0]));
   if(!reusable){
    ID3D12Resource *resource=nullptr;D3D12_SHADER_RESOURCE_VIEW_DESC srv{};D3D12_SAMPLER_DESC sampler{};D3D12_CPU_DESCRIPTOR_HANDLE source{};
   if(!PrepareSampledTexture(handle,srgb,&resource,srv,sampler,&source,comparison)){cached.valid=false;preparedSamplerTable_.count=0;clearSlot(slot);return;}
     if(sampler.AddressU==0)sampler=CPipelineCacheDX12::DefaultSamplerDesc();
     // Interned ids are process-stable, so an unchanged description keeps its id without a lookup.
     if(!cached.valid||std::memcmp(&cached.sampler,&sampler,sizeof(sampler))){preparedSamplerTable_.count=0;cached.samplerId=pipeline_.InternSampler(sampler);}cached.handle=handle;cached.record=record;cached.resource=resource;cached.srv=srv;cached.sampler=sampler;cached.source=source;cached.srgb=srgb;cached.comparison=comparison;cached.valid=true;
   }
   cached.epoch=textureStateEpoch_;
   bindingInput.textures[slot]=cached.resource;bindingInput.samplerDescs[slot]=cached.sampler;bindingInput.samplerIds[slot]=cached.samplerId;bindingInput.srvSources[slot]=cached.source;
   if(!cached.source.ptr)bindingInput.srvDescs[slot]=cached.srv;
  };
  for(size_t i=0;i<boundTextures_.size();++i)if(pixelSamplers&(1u<<i))prepareSlot(i,boundTextures_[i],(activeSnapshot_.srgbReadMask&(1u<<i))!=0,(activeSnapshot_.comparisonSamplerMask&(1u<<i))!=0);
  for(size_t i=0;i<vertexTextures_.size();++i)if(vertexSamplers&(1u<<i))prepareSlot(16+i,vertexTextures_[i],false,false);
 };
 // Whole-set reuse: identical bound textures, sampled masks and texture state since the previous draw in this
 // recording mean every slot would take its prepared fast path and the sampler table is still reserved.
 // Field-wise compare against the previous prepared set; the key is written only when the set is re-prepared.
 const uint64_t textureFence=device_->NextFenceValue();
 const auto &lastSet=lastTextureSet_;
 bool textureSetReused=textureSetValid_&&preparedSamplerTable_.count==32&&preparedSamplerFence_==textureFence&&lastSet.fence==textureFence&&lastSet.epoch==textureStateEpoch_&&
  lastSet.sampledMask==sampledMask&&lastSet.srgbMask==activeSnapshot_.srgbReadMask&&lastSet.comparisonMask==activeSnapshot_.comparisonSamplerMask&&lastSet.nullView==drawBindingNull_.ptr&&
  lastSet.pixel==boundTextures_&&lastSet.vertex==vertexTextures_;
 if(!textureSetReused)prepareTextures();
  bindingInput.texturesUnchanged=textureSetReused;
 // Completed-fence reclamation runs at submission/frame boundaries; a failed sampler reservation
 // first retries after reclaiming, and only then forces a GPU wait.
 bindingInput.samplerTable=preparedSamplerTable_.count==32&&preparedSamplerFence_==device_->NextFenceValue()?preparedSamplerTable_:pipeline_.PrepareSamplerTable(bindingInput.samplerDescs,bindingInput.samplerIds,device_->NextFenceValue());
 if(bindingInput.samplerTable.count!=32){
  pipeline_.Reclaim(device_->CompletedFenceValue());
  bindingInput.samplerTable=pipeline_.PrepareSamplerTable(bindingInput.samplerDescs,bindingInput.samplerIds,device_->NextFenceValue());
 }
 if(bindingInput.samplerTable.count!=32){
  if(!device_->Submit(true))return;
  pipeline_.Reclaim(device_->CompletedFenceValue());
  prepareTextures();
  bindingInput.samplerTable=pipeline_.PrepareSamplerTable(bindingInput.samplerDescs,bindingInput.samplerIds,device_->NextFenceValue());
  if(bindingInput.samplerTable.count!=32){Warning("ShaderAPIDX12: sampler descriptors unavailable after completion\n");return;}
 }
 // Reuse only within this recording fence: the cache reservation already protects its lifetime.
 preparedSamplerTable_=bindingInput.samplerTable;preparedSamplerFence_=device_->NextFenceValue();
 // Recompute the key: the retry path may have submitted (new fence) and re-prepared slots.
 if(!textureSetReused){auto &key=lastTextureSet_;key.pixel=boundTextures_;key.vertex=vertexTextures_;key.sampledMask=sampledMask;key.srgbMask=activeSnapshot_.srgbReadMask;key.comparisonMask=activeSnapshot_.comparisonSamplerMask;key.nullView=drawBindingNull_.ptr;}
 lastTextureSet_.fence=device_->NextFenceValue();lastTextureSet_.epoch=textureStateEpoch_;textureSetValid_=true;
 bindingInput.retireFence=device_->NextFenceValue();const uint64_t retireFence=bindingInput.retireFence;
 auto *list=device_->CommandList();
 bool pipelineBound=false;
 RenderTargetBindingDX12 target; // PrepareRenderTargets assigns it before any use
 { ZoneNamedN(drawTargets, "DX12 DrawTargets", DX12_DRAW_ZONES_ACTIVE); if(!(motionActive?PrepareMotionBinding(target):PrepareRenderTargets(target))||(!target.colorCount&&!target.depth)){static unsigned invalidTarget=0;if(invalidTarget++<6)Warning("ShaderAPIDX12: draw target unavailable colorCount=%u depth=%p\n",target.colorCount,target.depth);return;}pipeline_.BindRenderTargets(list,target.colorCount,target.rtvs.data(),target.colors.data(),target.depth?&target.dsv:nullptr,target.depth,retireFence); }
 D3D12_VIEWPORT viewport{0,0,static_cast<float>(target.width),static_cast<float>(target.height),0,1};
 if(viewportCount_>0){const auto &v=viewports_[0];viewport.TopLeftX=static_cast<float>(v.m_nTopLeftX);viewport.TopLeftY=static_cast<float>(v.m_nTopLeftY);viewport.Width=static_cast<float>(std::max(0,v.m_nWidth));viewport.Height=static_cast<float>(std::max(0,v.m_nHeight));viewport.MinDepth=v.m_flMinZ;viewport.MaxDepth=v.m_flMaxZ;}
 D3D12_RECT scissor{0,0,target.width,target.height};
 if(fastIntParams_[4]){scissor.left=std::max<LONG>(0,fastIntParams_[0]);scissor.top=std::max<LONG>(0,fastIntParams_[1]);scissor.right=std::max<LONG>(scissor.left,std::min<LONG>(target.width,fastIntParams_[2]));scissor.bottom=std::max<LONG>(scissor.top,std::min<LONG>(target.height,fastIntParams_[3]));}
 pipeline_.BindDrawState(list,viewport,scissor,retireFence);
 std::array<std::array<float,4>,6> drawClipPlanes;std::array<std::array<float,4>,6> *drawClipPlanesOverride=nullptr;uint32_t drawClipMask=clipPlaneMask_;
 const auto addClipPlane=[&](const std::array<float,4> &plane){if(!drawClipPlanesOverride){drawClipPlanes=worldClipPlanes_;drawClipPlanesOverride=&drawClipPlanes;}for(size_t i=0;i<drawClipPlanes.size();++i)if(!(drawClipMask&(1u<<i))){drawClipPlanes[i]=plane;drawClipMask|=1u<<i;return true;}return false;};
 if(heightClipMode_!=MATERIAL_HEIGHTCLIPMODE_DISABLE&&!addClipPlane({0.f,0.f,heightClipMode_==MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT?1.f:-1.f,heightClipMode_==MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT?-heightClipZ_:heightClipZ_})){Warning("ShaderAPIDX12: no free shader user clip plane for height clipping\n");return;}
 if(fastClipEnabled_&&!addClipPlane(fastClipPlane_)){Warning("ShaderAPIDX12: no free shader user clip plane for fast clipping\n");return;}
 const auto &clipPlanes=drawClipPlanesOverride?*drawClipPlanesOverride:worldClipPlanes_;
 // Pipeline-state memo: a mesh draw whose pipeline inputs equal the previous successful mesh draw in
 // this recording reuses its shader records, translated variants, input layout and PSO. The slow path
 // clears the memo before it can switch any record's active variant and stores it only on success.
 const uint8_t meshStreamFlags=static_cast<uint8_t>(meshStreams?((bindings[1].buffer?1:0)|(bindings[2].buffer?2:0)|((format&VERTEX_WRINKLE)?4:0)):0);
 // Texture dimensions (not identities) are the only texture inputs of pipeline selection.
 ShaderRasterStateDX12 raster{};
 uint32_t textureTypesPacked=0;
 for(size_t i=0;i<boundTextures_.size();++i){
  const auto handle=boundTextures_[i];if(!handle)continue;
  // Handles are never reused for a different texture; DeleteTexture drops cached entries.
  if(textureTypeHandles_[i]==handle){raster.textureTypes[i]=textureTypeValues_[i];continue;}
  const auto &slot=preparedTextureSlots_[i];const auto *texture=slot.valid&&slot.record&&slot.handle==handle?slot.record:FindTexture(handle);
  if(texture){raster.textureTypes[i]=(texture->flags&TEXTURE_CREATE_CUBEMAP)?2:(texture->depth>1?3:1);textureTypeHandles_[i]=handle;textureTypeValues_[i]=raster.textureTypes[i];}
 }
 for(unsigned i=0;i<16;++i)textureTypesPacked|=static_cast<uint32_t>(raster.textureTypes[i])<<(2*i);
 PipelineSignatureDX12 signature;std::memset(&signature,0,sizeof(signature));
 const bool memoEligible=meshStreams&&activeSnapshotId_>=0;
 bool memoHit=false;PipelineMemoDX12 *memoSlot=nullptr;
 if(memoEligible){
  signature.resolveEpoch=namedResolveEpoch_;signature.snapshot=activeSnapshotId_;
  signature.vs=reinterpret_cast<uint64_t>(boundVS_);signature.ps=reinterpret_cast<uint64_t>(boundPS_);signature.gs=reinterpret_cast<uint64_t>(boundGS_);
  signature.textureTypes=textureTypesPacked;signature.format=static_cast<uint64_t>(format);signature.layoutKey=sourceLayoutTranslationKey_;
  signature.policy=unusedVertexFields_;for(size_t i=0;i<unusedTextureCoordinates_.size();++i)if(unusedTextureCoordinates_[i])signature.policy|=uint64_t(1)<<(32+i);
  signature.instanceCount=bindings[0].repetitions;signature.primitive=static_cast<uint32_t>(primitive);signature.streamFlags=meshStreamFlags;signature.motionPass=motionActive?1:0;signature.clipMask=drawClipMask;
  signature.colorFormats=target.colorFormats;signature.depthFormat=target.depthFormat;signature.colorCount=target.colorCount;signature.samples=target.sampleCount;signature.quality=target.sampleQuality;signature.hasDepth=target.depth!=nullptr;
  std::memcpy(&signature.rasterState,&rasterState_,sizeof(rasterState_));signature.rasterOverride=rasterOverride_;signature.shadeMode=shadeMode_;signature.fogMode=fogMode_;signature.pixelFog=ShouldUsePixelFog();signature.cullMode=cullMode_;
  signature.stencilEnabled=stencilEnabled_;signature.stencilCompare=stencilCompare_;signature.stencilFail=stencilFailOp_;signature.stencilDepthFail=stencilDepthFailOp_;signature.stencilPass=stencilPassOp_;signature.stencilReadMask=stencilReadMask_;signature.stencilWriteMask=stencilWriteMask_;
  signature.alphaToCoverage=alphaToCoverage_;signature.colorWriteOverride=colorWriteOverride_;signature.colorWriteValue=colorWriteOverrideValue_;signature.alphaWriteOverride=alphaWriteOverride_;signature.alphaWriteValue=alphaWriteOverrideValue_;
  signature.overrideDepthEnable=overrideDepthEnable_;signature.overrideDepthValue=overrideDepthValue_;signature.forceDepthEquals=forceDepthEquals_;
  signature.shadowSlope=fastFloatParams_[0];signature.shadowDepth=fastFloatParams_[1];
  if(shaderUtil_){const auto &config=shaderUtil_->GetConfig();signature.reverseDepth=config.bReverseDepth;signature.slopeDecal=config.m_SlopeScaleDepthBias_Decal;signature.slopeNormal=config.m_SlopeScaleDepthBias_Normal;signature.depthDecal=config.m_DepthBias_Decal;signature.depthNormal=config.m_DepthBias_Normal;}
  memoSlot=&pipelineMemos_[Mix32HashFunctor()(static_cast<uint32_t>(signature.snapshot)*0x9E3779B1u^static_cast<uint32_t>(signature.vs>>4)^static_cast<uint32_t>(signature.ps>>4)*31u^static_cast<uint32_t>(signature.textureTypes)^static_cast<uint32_t>(signature.format))&(pipelineMemos_.size()-1)];
  // Records may have switched translated variant since the entry was stored; require the stored ones.
  // A shader alternating between translated variants (e.g. fog or clip state) still hits: the stored
  // variants are reactivated, which is exactly what the slow path's EnsureTranslated would do.
  memoHit=memoSlot->epoch==pipelineMemoEpoch_&&memoSlot->psoEpoch==pipeline_.PipelineEpoch()&&!std::memcmp(&signature,&memoSlot->signature,sizeof(signature))&&
   ActivateTranslatedVariant(memoSlot->vs,memoSlot->vsVariant)&&(!memoSlot->ps||ActivateTranslatedVariant(memoSlot->ps,memoSlot->psVariant));
 }
 bool depthOnly=false,generatedVS=false,generatedPS=false,zeroInput=false,geometryStage=false;
 D3D12_SHADER_BYTECODE geometryCode{};uint64_t geometryIdentity=0,geometryVariant=0;
 if(memoHit){
  vsRecord=memoSlot->vs;psRecord=memoSlot->ps;geometryStage=memoSlot->geometryStage;depthOnly=memoSlot->depthOnly;generatedVS=memoSlot->generatedVS;generatedPS=memoSlot->generatedPS;zeroInput=memoSlot->zeroInput;
 }else{
 raster.clipPlaneMask=drawClipMask;raster.alphaTest=activeSnapshot_.alphaTest;raster.alphaFunction=static_cast<uint32_t>(activeSnapshot_.alphaFunction)+1;
 raster.shadeMode=shadeMode_==SHADER_FLAT?1:2;
 raster.fog=activeSnapshot_.fogMode!=SHADER_FOGMODE_DISABLED&&fogMode_!=MATERIAL_FOG_NONE&&!ShouldUsePixelFog();raster.fogTableMode=0;raster.comparisonPixelSamplers=activeSnapshot_.comparisonSamplerMask;
 raster.fillMode=rasterOverride_?(rasterState_.m_FillMode==SHADER_FILL_WIREFRAME?2:3):(activeSnapshot_.polyFront==SHADER_POLYMODE_POINT?1:(activeSnapshot_.polyFront==SHADER_POLYMODE_LINE?2:3));
 raster.primitiveType=primitive==MATERIAL_POINTS?1:(primitive==MATERIAL_LINES?2:(primitive==MATERIAL_LINE_STRIP?3:(primitive==MATERIAL_TRIANGLE_STRIP?5:4)));
  depthOnly=!motionActive&&target.depth&&!(colorWriteOverride_?colorWriteOverrideValue_:activeSnapshot_.colorWrites)&&!(alphaWriteOverride_?alphaWriteOverrideValue_:activeSnapshot_.alphaWrites)&&!activeSnapshot_.alphaTest;
 generatedVS=!vsRecord;generatedPS=!psRecord&&!depthOnly;
 auto fixedShader=[&](bool pixel)->ShaderRecordDX12 *{
  uint32_t textureTypes=0;uint64_t linkage=1469598103934665603ull;
  if(pixel){for(unsigned i=0;i<16;++i)textureTypes|=static_cast<uint32_t>(raster.textureTypes[i])<<(2*i);if(vsRecord)for(const auto &output:vsRecord->translated.outputLinkage){linkage^=output.usage|(uint64_t(output.usageIndex)<<8)|(uint64_t(output.registerIndex)<<16)|(uint64_t(output.writeMask)<<24);linkage*=1099511628211ull;}}
  if(pixel){linkage^=static_cast<uint64_t>(shadeMode_);linkage*=1099511628211ull;}
  const FixedShaderKey key{activeSnapshotId_,format,pixel,textureTypes,pixel?linkage:0};
  const auto found=fixedShaders_.Find(key);if(found!=fixedShaders_.InvalidIndex())return fixedShaders_[found];
  FixedFunctionStateDX12 state=activeSnapshot_.fixed;state.format=format;state.flatShade=shadeMode_==SHADER_FLAT;std::copy_n(raster.textureTypes.begin(),16,state.textureTypes.begin());
  std::unique_ptr<ShaderRecordDX12> record(CreateFixedFunctionShaderDX12(device_,state,pixel,vsRecord?&vsRecord->translated.outputLinkage:nullptr));
  if(!record){Warning("ShaderAPIDX12: unable to compile generated %s shader\n",pixel?"pixel":"vertex");return nullptr;}
  auto *result=record.get();fixedShaders_.Insert(key,record.release());return result;
 };
 if(generatedVS){vsRecord=fixedShader(false);if(!vsRecord)return;}
 const bool needsTranslationKey=(vsRecord&&!vsRecord->legacyBytecode.empty())||(psRecord&&!psRecord->legacyBytecode.empty());
 uint64_t translationStateKey=0;
 if(needsTranslationKey){
  const uint64_t layoutTranslationKey=meshStreams?sourceLayoutTranslationKey_:TranslationLayoutKey(sourceLayout);
  // Compare only declared bytes; tail padding is indeterminate and would merely cause a recompute.
  constexpr size_t rasterBytes=offsetof(ShaderRasterStateDX12,transformedVertices)+sizeof(bool);
  if(translationKeyMemoValid_&&translationKeyMemoLayout_==layoutTranslationKey&&!std::memcmp(&translationKeyMemoRaster_,&raster,rasterBytes))translationStateKey=translationKeyMemo_;
  else{translationStateKey=TranslationStateKey(layoutTranslationKey,raster);translationKeyMemoRaster_=raster;translationKeyMemoLayout_=layoutTranslationKey;translationKeyMemo_=translationStateKey;translationKeyMemoValid_=true;}
 }
 if(!EnsureTranslated(vsRecord,false,sourceLayout,nullptr,0,0,raster,translationStateKey,device_->Signer()))return;
 const auto reflect=[](ShaderRecordDX12 *record)->bool{
  if(!record)return true;
  if(!ReflectNativeCBuffersDX12(record))return false;
  if(record->inputSignatureReady)return true;
  const auto bytecode=record->Bytecode();const bool native=record->legacyBytecode.empty();
  if(!ReadShaderInputSignatureDX12(bytecode.pShaderBytecode,bytecode.BytecodeLength,record->inputSignature,
     native?&record->nativeConstantRegisters:nullptr,native&&!record->stagePixel?&record->translated.outputLinkage:nullptr))return false;
  record->inputSignatureReady=true;record->linkageHashValid=false;record->constantLayoutValid=false;return true;
 };
 if(!reflect(vsRecord))return;
 if(boundGS_!=GEOMETRY_SHADER_HANDLE_INVALID){
  const auto *geometry=reinterpret_cast<ShaderRecordDX12 *>(boundGS_);geometryCode=geometry->Bytecode();geometryIdentity=geometry->identity;geometryVariant=geometry->activeVariantKey;
 }else if(raster.fillMode==1&&raster.primitiveType>=4){
  const uint32_t rasterKey=raster.primitiveType|(raster.fillMode<<4)|(raster.clipPlaneMask<<8);
  auto &variants=vsRecord->geometryVariants;int index=0;
  for(;index<variants.Count();++index)if(variants[index].vertexVariant==vsRecord->activeVariantKey&&variants[index].rasterKey==rasterKey)break;
  if(index==variants.Count()){
   ShaderTranslationResultDX12 result;CShaderTranslatorDX12 translator;std::string error;
   const auto &outputs=vsRecord->translated.outputLinkage;
   if(!translator.GenerateGeometry(outputs.data(),outputs.size(),raster,device_->Signer(),result,error)){Warning("ShaderAPIDX12: point-fill geometry conversion failed: %s\n",error.c_str());return;}
   auto &variant=variants[variants.AddToTail()];variant.vertexVariant=vsRecord->activeVariantKey;variant.rasterKey=rasterKey;variant.result=std::move(result);
  }
  const auto &code=variants[index].result.bytecode;geometryCode={code.data(),code.size()};geometryIdentity=vsRecord->identity;geometryVariant=static_cast<uint64_t>(index)+1;
 }
 geometryStage=geometryCode.BytecodeLength!=0;
 if(generatedPS){psRecord=fixedShader(true);if(!psRecord)return;}
 if(psRecord){
  auto &linkage=vsRecord->translated.outputLinkage;
  if(!vsRecord->linkageHashValid||vsRecord->linkageHashVariant!=vsRecord->activeVariantKey){
   uint64_t hash=1469598103934665603ull;const auto mix=[&](uint64_t value){hash^=value;hash*=1099511628211ull;};
   for(const auto &output:linkage){mix(output.usage);mix(output.usageIndex);mix(output.registerIndex);mix(output.writeMask);mix(output.centroid);}
   vsRecord->linkageHash=hash;vsRecord->linkageHashVariant=vsRecord->activeVariantKey;vsRecord->linkageHashValid=true;
  }
  if(!EnsureTranslated(psRecord,true,sourceLayout,linkage.data(),linkage.size(),vsRecord->linkageHash,raster,translationStateKey,device_->Signer()))return;
 }
 if(!reflect(psRecord))return;
 // A native record's space-1 cbuffers must be engine blocks with the backend layout or material blocks
 // written through the bridge with the same layout hash; anything else rejects the draw before PSO creation.
 auto validateNative=[&](ShaderRecordDX12 *record,bool pixel)->bool{
  if(!record||!record->legacyBytecode.empty())return true;
  const char *stageName=pixel?"PS":"VS";
  const char *logical=(pixel?activeSnapshot_.pixelShaderName:activeSnapshot_.vertexShaderName).c_str();
  const int staticIndex=pixel?activeSnapshot_.staticPixelIndex:activeSnapshot_.staticVertexIndex,dynamicIndex=pixel?pixelShaderIndex_:vertexShaderIndex_;
  for(const auto &binding:record->nativeCBuffers){
   // Space 0 is the existing native-source contract (Source register banks at root b0-b5); space 1 is the named-block ABI.
   if(binding.registerSpace!=1)continue;
   const dx12native::EngineCBufferLayoutDX12 *engine=nullptr;
   for(const auto &candidate:dx12native::kEngineCBufferLayouts)if(binding.name==candidate.name){engine=&candidate;break;}
   if(engine){
    bool matches=engine->stage==(pixel?dx12native::kStagePixel:dx12native::kStageVertex)&&engine->shaderRegister==binding.shaderRegister&&engine->byteSize==binding.byteSize&&engine->memberCount==binding.members.size();
    for(uint32_t m=0;matches&&m<engine->memberCount;++m)matches=binding.members[m].name==engine->members[m].name&&binding.members[m].offset==engine->members[m].offset&&binding.members[m].byteSize==engine->members[m].size;
    if(!matches){Warning("ShaderAPIDX12: native %s shader %s (static %d dynamic %d abi %016llx) engine cbuffer %s b%u size %u does not match the backend layout\n",stageName,logical,staticIndex,dynamicIndex,static_cast<unsigned long long>(record->nativeAbiHash),binding.name.c_str(),binding.shaderRegister,binding.byteSize);return false;}
    continue;
   }
   const unsigned firstMaterial=pixel?1u:2u;
   const NativeCBufferSlotDX12 *slot=nullptr;
   if(binding.shaderRegister>=firstMaterial&&binding.shaderRegister<=7u)slot=pixel?&nativePSBlocks_[binding.shaderRegister-1]:&nativeVSBlocks_[binding.shaderRegister-2];
   if(!slot||!slot->written||slot->layoutHash!=binding.layoutHash||slot->byteSize!=binding.byteSize){
    Warning("ShaderAPIDX12: native %s shader %s (static %d dynamic %d abi %016llx) material cbuffer %s b%u,space1 expected layout %016llx size %u, bound %016llx size %u\n",stageName,logical,staticIndex,dynamicIndex,static_cast<unsigned long long>(record->nativeAbiHash),binding.name.c_str(),binding.shaderRegister,static_cast<unsigned long long>(binding.layoutHash),binding.byteSize,
     static_cast<unsigned long long>(slot&&slot->written?slot->layoutHash:0),slot&&slot->written?slot->byteSize:0u);
    return false;
   }
  }
  return true;
 };
 if(!validateNative(vsRecord,false)||!validateNative(psRecord,true))return;
 }
 const auto constants=[&](ShaderRecordDX12 *record,bool pixel,bool generated)->bool{
  ZoneNamedN(derivedConstants, "DX12 DerivedConstants", DX12_DRAW_ZONES_ACTIVE);
  const unsigned first=pixel?3:0,descriptor=pixel?4:0;
  if(!record){for(unsigned bank=0;bank<3;++bank){bindingInput.constantVersions[first+bank]=1;bindingInput.constantShaderIds[first+bank]=0;bindingInput.consumedRegisters[first+bank]=0;bindingInput.constantData[descriptor+bank]=nullptr;bindingInput.constantSizes[descriptor+bank]=0;}return true;}
  // Compact per-variant layout on the record's first cache line: avoids touching translated metadata per draw.
  if(!record->constantLayoutValid||record->constantLayoutVariant!=record->activeVariantKey){
   record->constantCounts=record->legacyBytecode.empty()?record->nativeConstantRegisters:
    std::array<uint32_t,3>{record->translated.maxFloatConstants,record->translated.maxIntConstants,record->translated.maxBoolConstants};
   record->inlineConstantMask=0;for(unsigned bank=0;bank<3;++bank)if(!record->translated.inlineConstants[bank].empty())record->inlineConstantMask|=1u<<bank;
   record->constantLayoutVariant=record->activeVariantKey;record->constantLayoutValid=true;
  }
  const std::array<uint32_t,3> &counts=record->constantCounts;const uint32_t inlineMask=record->inlineConstantMask;
  auto &derived=record->derived;
  for(unsigned bank=0;bank<3;++bank){
   uint64_t version=constantVersions_[first+bank];
   if(generated)version+=pixel?fixedPSVersion_+constantVersions_[0]:fixedVSVersion_;
   if(generated&&!pixel&&bank==1)version=fixedVSVersion_;
   bindingInput.constantVersions[first+bank]=version;
   // Unpatched banks are the same API register file across shaders. Inline definitions
   // belong to the original program, not its raster/linkage translation variant.
   const bool hasInline=(inlineMask>>bank)&1;
   bindingInput.constantShaderIds[first+bank]=(generated||hasInline)?record->identity:0;
   bindingInput.consumedRegisters[first+bank]=counts[bank];
   if(bank==0){
    const auto *source=pixel?psFloat_.data():vsFloat_.data();const size_t available=pixel?psFloat_.size():vsFloat_.size();
    if(counts[bank]>available)return false;
    const bool patch=generated||hasInline;
    if(patch){
     const bool changed=derived.versions[bank]!=version;derived.versions[bank]=version;
     if(changed){
      derived.floats.SetCount(counts[bank]);
      if(counts[bank])std::memcpy(derived.floats.Base(),source,counts[bank]*sizeof(std::array<float,4>));
      for(const auto &constant:record->translated.inlineConstants[bank])if(constant.registerIndex<counts[bank])std::memcpy(derived.floats[constant.registerIndex].data(),constant.words.data(),16);
      if(generated&&!pixel){
       if(derived.floats.Count()>1)derived.floats[1]={ambientLight_.x,ambientLight_.y,ambientLight_.z,0.f};
       for(int face=0;face<static_cast<int>(ambientCube_.size())&&21+face<derived.floats.Count();++face)derived.floats[21+face]=ambientCube_[face];
      }else if(generated){
       const auto set=[&](size_t index,const std::array<float,4> &value){if(index<static_cast<size_t>(derived.floats.Count()))derived.floats[static_cast<int>(index)]=value;};
       set(0,{color_.x,color_.y,color_.z,colorAlpha_});set(1,{0.f,0.f,0.f,activeSnapshot_.alphaReference});
       const auto &projection=matrices_[MATERIAL_PROJECTION];set(2,{projection[2][2],projection[2][3],projection[3][2],projection[3][3]});
       set(28,{fogStart_,fogEnd_,fogEnd_!=fogStart_?1.f/(fogEnd_-fogStart_):1.f,fogMode_==MATERIAL_FOG_NONE?0.f:fogMaxDensity_});
       set(29,{rasterFogColor_[0],rasterFogColor_[1],rasterFogColor_[2],0.f});
      }
     }
     bindingInput.constantData[descriptor+bank]=derived.floats.Base();
    }else bindingInput.constantData[descriptor+bank]=source;
    bindingInput.constantSizes[descriptor+bank]=counts[bank]*16;
   }else if(bank==1){
    const auto *source=pixel?psInt_.data():vsInt_.data();
    if(generated&&!pixel){
     if(counts[bank]>40)return false;
     const bool changed=derived.versions[bank]!=version;derived.versions[bank]=version;
     if(changed){
      derived.integers.SetCount(counts[bank]);
      for(unsigned stage=0;stage<8;++stage){
       for(unsigned row=0;row<4&&stage*4+row<counts[bank];++row)std::memcpy(derived.integers[stage*4+row].data(),matrices_[MATERIAL_TEXTURE0+stage][row],16);
       const float flags[4]={textureTransformEnabled_[stage]?1.f:0.f,textureTransformProjected_[stage]?1.f:0.f,static_cast<float>(textureTransformDimension_[stage]),0.f};
       if(32+stage<counts[bank])std::memcpy(derived.integers[32+stage].data(),flags,16);
      }
     }
     bindingInput.constantData[descriptor+bank]=derived.integers.Base();
    }else{
     if(counts[bank]>16)return false;
     if(!hasInline)bindingInput.constantData[descriptor+bank]=source;
     else{
      const bool changed=derived.versions[bank]!=version;derived.versions[bank]=version;
      if(changed){
       derived.integers.SetCount(counts[bank]);
       if(counts[bank])std::memcpy(derived.integers.Base(),source,counts[bank]*sizeof(std::array<int,4>));
       for(const auto &constant:record->translated.inlineConstants[bank])if(constant.registerIndex<counts[bank])std::memcpy(derived.integers[constant.registerIndex].data(),constant.words.data(),16);
      }
      bindingInput.constantData[descriptor+bank]=derived.integers.Base();
     }
    }
    bindingInput.constantSizes[descriptor+bank]=counts[bank]*16;
   }else{
    if(counts[bank]>16)return false;
    if(!counts[bank]){bindingInput.constantData[descriptor+bank]=nullptr;bindingInput.constantSizes[descriptor+bank]=0;continue;}
    const bool changed=derived.versions[bank]!=version;derived.versions[bank]=version;
    if(changed){derived.booleans.SetCount(counts[bank]);for(unsigned i=0;i<counts[bank];++i)derived.booleans[i]=(pixel?psBool_[i]:vsBool_[i])?1u:0u;for(const auto &constant:record->translated.inlineConstants[bank])if(constant.registerIndex<counts[bank])derived.booleans[constant.registerIndex]=constant.words[0];}
    bindingInput.constantData[descriptor+bank]=derived.booleans.Base();bindingInput.constantSizes[descriptor+bank]=counts[bank]*sizeof(uint32_t);
   }
  }
  return true;
 };
 if(!constants(vsRecord,false,generatedVS)||!constants(psRecord,true,generatedPS)){Warning("ShaderAPIDX12: shader constant bank exceeds its source register range\n");return;}
 // Pixel extension: fog and alpha reference; field compare, rebuilt only when an input changed.
 {
  const float fogInverse=fogEnd_!=fogStart_?1.f/(fogEnd_-fogStart_):0.f;
  auto &pixel=previousPixelExtension_;
  if(!extensionVersions_[1]||pixel.fogColor[0]!=rasterFogColor_[0]||pixel.fogColor[1]!=rasterFogColor_[1]||pixel.fogColor[2]!=rasterFogColor_[2]||pixel.fogStart!=fogStart_||pixel.fogEnd!=fogEnd_||
     pixel.fogDistanceInverse!=fogInverse||pixel.fogDensity!=fogMaxDensity_||pixel.alphaReference!=activeSnapshot_.alphaReference){
   pixel.fogColor[0]=rasterFogColor_[0];pixel.fogColor[1]=rasterFogColor_[1];pixel.fogColor[2]=rasterFogColor_[2];pixel.fogStart=fogStart_;pixel.fogEnd=fogEnd_;
   pixel.fogDistanceInverse=fogInverse;pixel.fogDensity=fogMaxDensity_;pixel.alphaReference=activeSnapshot_.alphaReference;++extensionVersions_[1];
  }
 }
 // Bump matrices change only through SetBumpEnvMatrix; the colour-key buffer is always zero.
 if(bumpExtensionDirty_){
  ShaderBumpExtensionDX12 next=previousBumpExtension_;for(size_t stage=0;stage<bumpMatrices_.size();++stage)std::copy_n(bumpMatrices_[stage].data(),4,next.matrix[stage]);
  if(!extensionVersions_[2]||std::memcmp(&next,&previousBumpExtension_,sizeof(next))){previousBumpExtension_=next;++extensionVersions_[2];}
  bumpExtensionDirty_=false;
 }
 if(!extensionVersions_[3])extensionVersions_[3]=1;
 // Vertex extension: viewport scale, point size and user clip planes transformed to clip space. Without clip
 // planes (now and in the stored buffer) only the viewport can change it.
 if(drawClipMask||vertexExtensionClipMask_||!extensionVersions_[0]||previousVertexExtension_.viewportScale[0]!=(viewport.Width>0?1.f/viewport.Width:1.f)||previousVertexExtension_.viewportScale[1]!=(viewport.Height>0?-1.f/viewport.Height:-1.f)){
  ShaderVertexExtensionDX12 vertexExtension{};vertexExtension.viewportScale[0]=viewport.Width>0?1.f/viewport.Width:1.f;vertexExtension.viewportScale[1]=viewport.Height>0?-1.f/viewport.Height:-1.f;vertexExtension.pointSize[0]=1.f;
  if(drawClipMask){
   ZoneNamedN(clipTransform, "DX12 ClipPlaneTransform", DX12_DRAW_ZONES_ACTIVE);
   VMatrix worldToClip;MatrixMultiply(matrices_[MATERIAL_PROJECTION],userClipViewOverride_?userClipView_:matrices_[MATERIAL_VIEW],worldToClip);
   if(!clipInverseValid_||std::memcmp(worldToClip.Base(),cachedWorldToClip_.Base(),sizeof(float)*16)){
    clipInverseValid_=MatrixInverseGeneral(worldToClip,cachedClipToWorld_);
    if(!clipInverseValid_){Warning("ShaderAPIDX12: cannot transform clip plane through singular projection/view matrix\n");return;}
    cachedWorldToClip_=worldToClip;
   }
   for(size_t i=0;i<clipPlanes.size();++i)if(drawClipMask&(1u<<i))for(int c=0;c<4;++c)for(int r=0;r<4;++r)vertexExtension.clipPlanes[i][c]+=cachedClipToWorld_[r][c]*clipPlanes[i][r];
  }
  if(!extensionVersions_[0]||std::memcmp(&vertexExtension,&previousVertexExtension_,sizeof(vertexExtension))){previousVertexExtension_=vertexExtension;++extensionVersions_[0];}
  vertexExtensionClipMask_=drawClipMask;
 }
 if(motionActive)CommitTransforms();
 // Native records read engine state from space-1 blocks built from the same values the legacy registers hold.
 const bool nativeVS=vsRecord&&vsRecord->legacyBytecode.empty(),nativePS=psRecord&&psRecord->legacyBytecode.empty();
 bindingInput.nativeStage={nativeVS,nativePS};
 bindingInput.nativeData.fill(nullptr);bindingInput.nativeSizes.fill(0);bindingInput.nativeVersions.fill(0);
 if(nativeVS){
  ZoneNamedN(nativeEngine, "DX12 NativeVSEngine", DX12_DRAW_ZONES_ACTIVE);
  // Rebuilt only when a source register bank, the vertex extension (viewport/clip planes) or the clip mask changed.
  const std::array<uint64_t,5> engineInputs{constantVersions_[0],constantVersions_[1],constantVersions_[2],extensionVersions_[0],drawClipMask};
  if(nativeVSEngineInputs_!=engineInputs){
   auto &e=nativeVSEngine_;const auto copy=[&](float *dst,unsigned reg,unsigned count){std::memcpy(dst,vsFloat_[reg].data(),sizeof(float)*4*count);};
   copy(e.cConstants0,VERTEX_SHADER_MATH_CONSTANTS0,1);copy(e.cConstants1,VERTEX_SHADER_MATH_CONSTANTS1,1);copy(e.cEyePosWaterZ,VERTEX_SHADER_CAMERA_POS,1);copy(e.cFlexScale,VERTEX_SHADER_FLEXSCALE,1);
   copy(e.cModelViewProj,VERTEX_SHADER_MODELVIEWPROJ,4);copy(e.cViewProj,VERTEX_SHADER_VIEWPROJ,4);copy(e.cModelViewProjZ,VERTEX_SHADER_MODELVIEWPROJ_THIRD_ROW,1);copy(e.cViewProjZ,VERTEX_SHADER_VIEWPROJ_THIRD_ROW,1);
   copy(e.cFogParams,VERTEX_SHADER_FOG_PARAMS,1);copy(e.cViewModel,VERTEX_SHADER_VIEWMODEL,4);copy(e.cAmbientCube[0],VERTEX_SHADER_AMBIENT_LIGHT,6);copy(e.cLightInfo[0].color,VERTEX_SHADER_LIGHTS,20);
   std::memcpy(e.cLightCount,vsInt_[0].data(),sizeof(e.cLightCount));for(unsigned i=0;i<4;++i)e.cLightEnabled[i]=vsBool_[VERTEX_SHADER_LIGHT_ENABLE_BOOL_CONST+i]?1u:0u;
   std::memcpy(e.cViewportScale,previousVertexExtension_.viewportScale,sizeof(e.cViewportScale));std::memcpy(e.cClipPlanes,previousVertexExtension_.clipPlanes,sizeof(e.cClipPlanes));
   e.cClipMask[0]=drawClipMask;e.cClipMask[1]=e.cClipMask[2]=e.cClipMask[3]=0;
   static_assert(sizeof(nativeVSBones_.cModel)==sizeof(float)*4*3*53,"bone rows");std::memcpy(nativeVSBones_.cModel,vsFloat_[VERTEX_SHADER_MODEL].data(),sizeof(nativeVSBones_.cModel));
   nativeVSEngineInputs_=engineInputs;++nativeVSEngineVersion_;
  }
  bindingInput.nativeData[0]=&nativeVSEngine_;bindingInput.nativeSizes[0]=sizeof(nativeVSEngine_);bindingInput.nativeVersions[0]=nativeVSEngineVersion_;
  bindingInput.nativeData[1]=&nativeVSBones_;bindingInput.nativeSizes[1]=sizeof(nativeVSBones_);bindingInput.nativeVersions[1]=nativeVSEngineVersion_;
  for(unsigned slot=0;slot<nativeVSBlocks_.size();++slot)if(nativeVSBlocks_[slot].written){bindingInput.nativeData[2+slot]=nativeVSBlocks_[slot].bytes.data();bindingInput.nativeSizes[2+slot]=nativeVSBlocks_[slot].byteSize;bindingInput.nativeVersions[2+slot]=nativeVSBlocks_[slot].version;}
  if(motionActive){FillMotionBlock(bindings[0],indices,indexOffset,firstIndex,indexCount);bindingInput.nativeData[7]=&motionBlock_;bindingInput.nativeSizes[7]=sizeof(motionBlock_);bindingInput.nativeVersions[7]=motionBlockVersion_;}
 }
 if(nativePS){
  auto &e=nativePSEngine_;
  const float alphaTest[4]={activeSnapshot_.alphaTest?1.f:0.f,static_cast<float>(activeSnapshot_.alphaFunction)+1.f,previousPixelExtension_.alphaReference,0.f};
  const float rasterFog[4]={previousPixelExtension_.fogColor[0],previousPixelExtension_.fogColor[1],previousPixelExtension_.fogColor[2],raster.fog?1.f:0.f};
  const float rasterFogParams[4]={previousPixelExtension_.fogStart,previousPixelExtension_.fogEnd,previousPixelExtension_.fogDistanceInverse,previousPixelExtension_.fogDensity};
  const float lightScale[4]={toneScale_.x,toneScale_.y,toneScale_.z,toneScaleGamma_};
  const auto update=[&](float *dst,const float *value,size_t bytes){if(std::memcmp(dst,value,bytes)){std::memcpy(dst,value,bytes);++nativePSEngineVersion_;}};
  update(e.cPixelFogParams,fogPixelParams_.data(),sizeof(e.cPixelFogParams));update(e.cLinearFogColor,fogPixelColor_.data(),sizeof(e.cLinearFogColor));update(e.cLightScale,lightScale,sizeof(lightScale));
  update(e.cAlphaTest,alphaTest,sizeof(alphaTest));update(e.cRasterFogColor,rasterFog,sizeof(rasterFog));update(e.cRasterFogParams,rasterFogParams,sizeof(rasterFogParams));
  bindingInput.nativeData[8]=&nativePSEngine_;bindingInput.nativeSizes[8]=sizeof(nativePSEngine_);bindingInput.nativeVersions[8]=nativePSEngineVersion_;
  for(unsigned slot=0;slot<nativePSBlocks_.size();++slot)if(nativePSBlocks_[slot].written){bindingInput.nativeData[9+slot]=nativePSBlocks_[slot].bytes.data();bindingInput.nativeSizes[9+slot]=nativePSBlocks_[slot].byteSize;bindingInput.nativeVersions[9+slot]=nativePSBlocks_[slot].version;}
 }
 bindingInput.constantData[3]=&previousVertexExtension_;bindingInput.constantSizes[3]=sizeof(previousVertexExtension_);
 bindingInput.constantData[7]=&previousPixelExtension_;bindingInput.constantSizes[7]=sizeof(previousPixelExtension_);
 bindingInput.constantData[8]=&previousBumpExtension_;bindingInput.constantSizes[8]=sizeof(previousBumpExtension_);
 static const ShaderColorKeyExtensionDX12 colorKeyExtension{};
 bindingInput.constantData[9]=&colorKeyExtension;bindingInput.constantSizes[9]=sizeof(colorKeyExtension);
 for(unsigned bank=0;bank<4;++bank)bindingInput.constantVersions[6+bank]=extensionVersions_[bank];
 bindingInput.vertexTextures=vertexSamplers!=0;bindingInput.geometryStage=geometryStage;
 { ZoneNamedN(___tracy_scoped_zone, "DX12 PrepareBindings", DX12_DRAW_ZONES_ACTIVE); if(!pipeline_.PrepareBindings(list,bindingInput)){Warning("ShaderAPIDX12: binding descriptors unavailable\n");return;} }
 const D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType=primitive==MATERIAL_POINTS?D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:(primitive==MATERIAL_LINES||primitive==MATERIAL_LINE_STRIP||primitive==MATERIAL_LINE_LOOP?D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
 const D3D12_PRIMITIVE_TOPOLOGY iaTopology=primitive==MATERIAL_POINTS?D3D_PRIMITIVE_TOPOLOGY_POINTLIST:(primitive==MATERIAL_LINES?D3D_PRIMITIVE_TOPOLOGY_LINELIST:(primitive==MATERIAL_LINE_STRIP?D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:(primitive==MATERIAL_TRIANGLE_STRIP?D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)));
 const UINT instanceCount=bindings[0].repetitions;
 if(memoHit){++drawStats_.memoHits;pipeline_.BindPipelineState(list,memoSlot->pso,stencilEnabled_?stencilRef_:activeSnapshot_.stencilReference,retireFence);pipelineBound=true;}
 else if(vsRecord&&(psRecord||depthOnly))
 {
  ZoneNamedN(pipelineSetup, "DX12 PipelineSetup", DX12_DRAW_ZONES_ACTIVE);
  auto *vs=vsRecord;auto *ps=psRecord;
  std::array<D3D12_INPUT_ELEMENT_DESC,MAX_VERTEX_INPUTS_DX12> elements;if(vs->inputSignature.size()>elements.size())return;
  const D3D12_INPUT_ELEMENT_DESC *inputElements=elements.data();
  const UINT signatureCount=static_cast<UINT>(vs->inputSignature.size());const uint8_t streamFlags=meshStreamFlags;uint64_t policy=unusedVertexFields_;for(size_t i=0;i<unusedTextureCoordinates_.size();++i)if(unusedTextureCoordinates_[i])policy|=uint64_t(1)<<(32+i);
  uint64_t layoutKey=0;UINT inputCount=signatureCount;
  auto &layoutCache=inputLayoutCaches_[Mix32HashFunctor()(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vs)>>4)^static_cast<uint32_t>(vs->activeVariantKey)^static_cast<uint32_t>(format)^static_cast<uint32_t>(policy>>32)^static_cast<uint32_t>(policy)^(uint32_t(streamFlags)<<24)^(instanceCount<<16))&(inputLayoutCaches_.size()-1)];
  const bool cachedLayout=meshStreams&&layoutCache.valid&&layoutCache.shader==vs&&layoutCache.variant==vs->activeVariantKey&&layoutCache.policy==policy&&layoutCache.format==format&&layoutCache.instanceCount==instanceCount&&layoutCache.streamFlags==streamFlags&&layoutCache.count==signatureCount;
  if(cachedLayout){inputElements=layoutCache.elements.data();layoutKey=layoutCache.layoutKey;zeroInput=layoutCache.zeroInput;}
  else{
   layoutKey=1469598103934665603ull;
   const auto hashLayout=[&](uint64_t value){layoutKey^=value;layoutKey*=1099511628211ull;};
   for(size_t i=0;i<vs->inputSignature.size();++i){
    const auto &required=vs->inputSignature[i];auto &element=elements[i];element.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;element.InstanceDataStepRate=0;
    element.SemanticName=required.semantic.c_str();element.SemanticIndex=required.semanticIndex;
    const VertexInputDX12 *provided=nullptr;
    for(uint32_t j=0;j<sourceLayout.inputCount;++j)if(required.semanticIndex==sourceLayout.inputs[j].semanticIndex&&!std::strcmp(required.semantic.c_str(),sourceLayout.inputs[j].semantic)){provided=&sourceLayout.inputs[j];break;}
    const unsigned int field=required.semantic=="POSITION"?VERTEX_POSITION:(required.semantic=="NORMAL"?VERTEX_NORMAL:(required.semantic=="COLOR"?(required.semanticIndex==0?VERTEX_COLOR:VERTEX_SPECULAR):(required.semantic=="BLENDINDICES"?VERTEX_BONE_INDEX:0)));
    if((field&unusedVertexFields_)||(required.semantic=="TEXCOORD"&&required.semanticIndex<unusedTextureCoordinates_.size()&&unusedTextureCoordinates_[required.semanticIndex]))provided=nullptr;
    if(provided){element.Format=provided->format;element.InputSlot=provided->inputSlot;element.AlignedByteOffset=provided->byteOffset;
     if(!meshStreams&&element.InputSlot>0&&instanceCount>1){element.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;element.InstanceDataStepRate=bindings[element.InputSlot].repetitions;}}
    else{element.Format=required.format;element.InputSlot=16;element.AlignedByteOffset=0;zeroInput=true;}
    for(const char *name=element.SemanticName;*name;++name)hashLayout(static_cast<unsigned char>(*name));
    hashLayout(element.SemanticIndex);hashLayout(element.Format);hashLayout(element.InputSlot);hashLayout(element.AlignedByteOffset);hashLayout(element.InputSlotClass);hashLayout(element.InstanceDataStepRate);
   }
   if(meshStreams){layoutCache.shader=vs;layoutCache.variant=vs->activeVariantKey;layoutCache.policy=policy;layoutCache.format=format;layoutCache.instanceCount=instanceCount;layoutCache.streamFlags=streamFlags;layoutCache.count=signatureCount;layoutCache.layoutKey=layoutKey;layoutCache.zeroInput=zeroInput;std::copy_n(elements.begin(),signatureCount,layoutCache.elements.begin());layoutCache.valid=true;}
  }
  D3D12_INPUT_LAYOUT_DESC input{inputElements,inputCount};PipelineKeyDX12 key{};
  key.vs=vs->identity;key.ps=ps?ps->identity:0;key.vsVariant=vs->activeVariantKey;key.psVariant=ps?ps->activeVariantKey:0;
  key.gs=geometryIdentity;key.gsVariant=geometryVariant;
  key.input=layoutKey;key.topology=topologyType;key.color=target.colorFormat;key.colorFormats=target.colorFormats;key.colorCount=target.colorCount;key.depth=target.depthFormat;key.samples=target.sampleCount;key.sampleQuality=target.sampleQuality;
  key.blend=activeSnapshot_.translucent||activeSnapshot_.separateAlpha?1:0;key.blendSource=activeSnapshot_.translucent?activeSnapshot_.blendSource:SHADER_BLEND_ONE;key.blendDestination=activeSnapshot_.translucent?activeSnapshot_.blendDestination:SHADER_BLEND_ZERO;key.blendOperation=activeSnapshot_.translucent?activeSnapshot_.blendOperation:SHADER_BLEND_OP_ADD;key.separateAlpha=activeSnapshot_.separateAlpha;key.blendAlphaSource=activeSnapshot_.blendAlphaSource;key.blendAlphaDestination=activeSnapshot_.blendAlphaDestination;key.blendAlphaOperation=activeSnapshot_.blendAlphaOperation;
  key.depthState=activeSnapshot_.depthWrite?1:0;key.depthTest=overrideDepthEnable_?overrideDepthValue_:activeSnapshot_.depthTest;key.depthWrite=activeSnapshot_.depthWrite;key.depthFunction=forceDepthEquals_?SHADER_DEPTHFUNC_EQUAL:activeSnapshot_.depthFunction;
  const bool reverseDepth=shaderUtil_&&shaderUtil_->GetConfig().bReverseDepth;
  if(reverseDepth){switch(key.depthFunction){case SHADER_DEPTHFUNC_NEARER:key.depthFunction=SHADER_DEPTHFUNC_FARTHER;break;case SHADER_DEPTHFUNC_NEAREROREQUAL:key.depthFunction=SHADER_DEPTHFUNC_FARTHEROREQUAL;break;case SHADER_DEPTHFUNC_FARTHER:key.depthFunction=SHADER_DEPTHFUNC_NEARER;break;case SHADER_DEPTHFUNC_FARTHEROREQUAL:key.depthFunction=SHADER_DEPTHFUNC_NEAREROREQUAL;break;default:break;}}
  key.culling=activeSnapshot_.culling&&(!rasterOverride_||rasterState_.m_bCullEnable);key.frontCounterClockwise=cullMode_==MATERIAL_CULLMODE_CW;key.wireframe=raster.fillMode==2;key.scissor=rasterOverride_&&rasterState_.m_bScissorEnable;
  static const MaterialSystem_Config_t defaultConfig;const auto &config=shaderUtil_?shaderUtil_->GetConfig():defaultConfig;
  const PolygonOffsetMode_t offset=activeSnapshot_.polygonOffset!=SHADER_POLYOFFSET_DISABLE?activeSnapshot_.polygonOffset:(rasterOverride_&&rasterState_.m_bDepthBias?SHADER_POLYOFFSET_DECAL:SHADER_POLYOFFSET_DISABLE);
  const float slope=offset==SHADER_POLYOFFSET_SHADOW_BIAS?fastFloatParams_[0]:(offset==SHADER_POLYOFFSET_DECAL?config.m_SlopeScaleDepthBias_Decal:config.m_SlopeScaleDepthBias_Normal);
  const float depth=offset==SHADER_POLYOFFSET_SHADOW_BIAS?fastFloatParams_[1]:(offset==SHADER_POLYOFFSET_DECAL?config.m_DepthBias_Decal:config.m_DepthBias_Normal);
  const float direction=reverseDepth?-1.f:1.f;
  key.slopeScaledDepthBias=slope!=0.f?direction/slope:0.f;
  key.depthBiasValue=depth!=0.f?static_cast<int>(std::clamp(std::round(direction*16777216.f/depth),-16777216.f,16777216.f)):0;
  key.colorWrites=colorWriteOverride_?colorWriteOverrideValue_:activeSnapshot_.colorWrites;key.alphaWrites=alphaWriteOverride_?alphaWriteOverrideValue_:activeSnapshot_.alphaWrites;key.alphaToCoverage=alphaToCoverage_||activeSnapshot_.alphaToCoverage;key.stencil=stencilEnabled_||activeSnapshot_.stencil;key.raster=(key.alphaToCoverage?1:0)|(key.stencil?2:0)|(activeSnapshot_.alphaTest?4:0);
  if(stencilEnabled_){key.stencilFunction=stencilCompare_;key.stencilFail=stencilFailOp_;key.stencilDepthFail=stencilDepthFailOp_;key.stencilPass=stencilPassOp_;key.stencilReadMask=stencilReadMask_;key.stencilWriteMask=stencilWriteMask_;}
  else{key.stencilFunction=static_cast<uint32_t>(activeSnapshot_.stencilFunction)+1;key.stencilFail=static_cast<uint32_t>(activeSnapshot_.stencilFail)+1;key.stencilDepthFail=static_cast<uint32_t>(activeSnapshot_.stencilDepthFail)+1;key.stencilPass=static_cast<uint32_t>(activeSnapshot_.stencilPass)+1;key.stencilReadMask=activeSnapshot_.stencilReadMask;key.stencilWriteMask=activeSnapshot_.stencilWriteMask;}
  if(motionActive){key.depthState=0;key.depthWrite=false;key.depthTest=true;key.depthFunction=reverseDepth?SHADER_DEPTHFUNC_FARTHEROREQUAL:SHADER_DEPTHFUNC_NEAREROREQUAL;
   key.blend=0;key.blendSource=SHADER_BLEND_ONE;key.blendDestination=SHADER_BLEND_ZERO;key.blendOperation=SHADER_BLEND_OP_ADD;key.separateAlpha=false;
   key.colorWrites=true;key.alphaWrites=true;key.alphaToCoverage=false;key.stencil=false;key.raster=0;key.slopeScaledDepthBias=0.f;key.depthBiasValue=reverseDepth?-kMotionDepthBias:kMotionDepthBias;}
  ID3D12PipelineState *pso=nullptr;
 { ZoneNamedN(___tracy_scoped_zone, "DX12 GetOrCreatePSO", DX12_DRAW_ZONES_ACTIVE); pso=pipeline_.GetOrCreate(key,vs->Bytecode(),ps?ps->Bytecode():D3D12_SHADER_BYTECODE{},geometryCode,input,retireFence); }
  if(pso){
   pipeline_.BindPipelineState(list,pso,stencilEnabled_?stencilRef_:activeSnapshot_.stencilReference,retireFence);pipelineBound=true;
   if(memoSlot){memoSlot->signature=signature;memoSlot->vs=vsRecord;memoSlot->ps=psRecord;memoSlot->pso=pso;memoSlot->vsVariant=vsRecord->activeVariantKey;memoSlot->psVariant=psRecord?psRecord->activeVariantKey:0;
    memoSlot->depthOnly=depthOnly;memoSlot->generatedVS=generatedVS;memoSlot->generatedPS=generatedPS;memoSlot->zeroInput=zeroInput;memoSlot->geometryStage=geometryStage;memoSlot->epoch=pipelineMemoEpoch_;memoSlot->psoEpoch=pipeline_.PipelineEpoch();}
  }
 }
 if(!pipelineBound){static unsigned invalidShaders=0;if(invalidShaders++<10)Warning("ShaderAPIDX12: shaders unavailable vs=%p ps=%p materialVS=%s materialPS=%s\n",reinterpret_cast<void *>(boundVS_),reinterpret_cast<void *>(boundPS_),activeSnapshot_.vertexShaderName.c_str(),activeSnapshot_.pixelShaderName.c_str());return;}
 {
  ZoneNamedN(___tracy_scoped_zone, "DX12 GeometryUploads", DX12_DRAW_ZONES_ACTIVE);
 // Only views [0,vertexViewCount) and the zero stream are read; unused slots below the count are zeroed.
 std::array<D3D12_VERTEX_BUFFER_VIEW,17> vertexViews;
 UINT vertexViewCount=0;
 for(unsigned slot=0;slot<bindings.size();++slot)if(bindings[slot].buffer){
  for(UINT gap=vertexViewCount;gap<slot;++gap)vertexViews[gap]={};
  const auto &binding=bindings[slot];auto &vertices=*binding.buffer;
  const size_t bytes=static_cast<size_t>(vertices.WrittenCount())*vertices.Stride();
  size_t swapCount=0;const uint32_t *swaps=vertices.SwapOffsets(swapCount);
  D3D12_GPU_VIRTUAL_ADDRESS address=0;
  if(vertices.IsDynamic()){
   ZoneNamedN(___tracy_scoped_zone, "DX12 DynamicVertexUpload", DX12_DRAW_ZONES_ACTIVE);
   if(!pipeline_.UploadDynamic(vertices,bytes,16,retireFence,address,swaps,swapCount,vertices.Stride()))return;
  }else if(!pipeline_.EnsureGeometryBuffer(list,vertices,bytes,retireFence,address,swaps,swapCount,vertices.Stride()))return;
  // firstVertex is the declared minimum index, not an additional base vertex.
  vertexViews[slot]={address+binding.byteOffset,static_cast<UINT>((binding.firstVertex+binding.vertexCount)*vertices.Stride()),vertices.Stride()};
  vertexViewCount=slot+1;
 }
 if(zeroInput){
  static constexpr uint32_t zero[4]{};D3D12_GPU_VIRTUAL_ADDRESS address=0;
  if(!pipeline_.UploadTransient(zero,sizeof(zero),sizeof(zero),16,retireFence,address))return;
  vertexViews[16]={address,sizeof(zero),0};
 }
 {
  ZoneNamedN(___tracy_scoped_zone, "DX12 VertexBindings", DX12_DRAW_ZONES_ACTIVE);
  // The input layout reads only supplied streams and the optional zero stream.
  pipeline_.BindInputAssembler(list,vertexViews.data(),vertexViewCount,zeroInput?&vertexViews[16]:nullptr,iaTopology,retireFence);
 }
 }
 {
  ZoneNamedN(___tracy_scoped_zone, "DX12 IndexUploadAndDraw", DX12_DRAW_ZONES_ACTIVE);
 if(indexed){
  D3D12_GPU_VIRTUAL_ADDRESS address=0;
  if(indices->IsDynamic()){
   if(!pipeline_.UploadDynamic(*indices,indexBytes,4,retireFence,address))return;
  }else if(!pipeline_.EnsureIndexBuffer(list,*indices,indexBytes,retireFence,address))return;
  const D3D12_INDEX_BUFFER_VIEW view{address+indexOffset,static_cast<UINT>(indexBytes-indexOffset),indices->IndexFormat()==MATERIAL_INDEX_FORMAT_32BIT?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R16_UINT};
  pipeline_.BindIndexBuffer(list,&view,retireFence);list->DrawIndexedInstanced(indexCount,instanceCount,firstIndex,0,0);
 }else{pipeline_.BindIndexBuffer(list,nullptr,retireFence);list->DrawInstanced(indexCount,instanceCount,firstIndex,0);}
 }

}
void CShaderAPIDX12::RetireShaderPipelines(ShaderRecordDX12 *record)
{
 if(!record)return;
 pipeline_.NotifyShaderDestroyed(record->identity);for(auto &layoutCache:inputLayoutCaches_)if(layoutCache.shader==record)layoutCache.valid=false;++pipelineMemoEpoch_;
 if(boundVS_==reinterpret_cast<VertexShaderHandle_t>(record)){boundVS_=VERTEX_SHADER_HANDLE_INVALID;boundVertexShaderIsNamed_=false;}
 if(boundPS_==reinterpret_cast<PixelShaderHandle_t>(record)){boundPS_=PIXEL_SHADER_HANDLE_INVALID;boundPixelShaderIsNamed_=false;}
 if(boundGS_==reinterpret_cast<GeometryShaderHandle_t>(record))boundGS_=GEOMETRY_SHADER_HANDLE_INVALID;
}
void CShaderAPIDX12::SetDefaultState()
{
 // Material shaders call this inside a pass; the snapshot and bindings must survive.
 for(int stage=0;stage<4;++stage){DisableTextureTransform(static_cast<TextureStage_t>(stage));MatrixMode(static_cast<MaterialMatrixMode_t>(MATERIAL_TEXTURE0+stage));LoadIdentity();}
 MatrixMode(MATERIAL_MODEL);Color4ub(255,255,255,255);ShadeMode(SHADER_SMOOTH);
 SetVertexShaderIndex();SetPixelShaderIndex();MarkUnusedVertexFields(0,0,nullptr);
}

void CShaderAPIDX12::SetViewports( int nCount, const ShaderViewport_t* pViewports ) { AUTO_LOCK(stateMutex_); viewportCount_=std::max(0,std::min(nCount,16)); if (pViewports && viewportCount_) std::copy(pViewports,pViewports+viewportCount_,viewports_.begin()); }
int CShaderAPIDX12::GetViewports( ShaderViewport_t* pViewports, int nMax ) const { const int count=std::min(viewportCount_,std::max(0,nMax)); if(pViewports && count) std::copy(viewports_.begin(),viewports_.begin()+count,pViewports); return count; }
double CShaderAPIDX12::CurrentTime() const { return Plat_FloatTime(); }
void CShaderAPIDX12::GetLightmapDimensions( int *w, int *h ) { if(shaderUtil_)shaderUtil_->GetLightmapDimensions(w,h);else{if(w)*w=0;if(h)*h=0;} }
MaterialFogMode_t CShaderAPIDX12::GetSceneFogMode( ) { return fogMode_; }
void CShaderAPIDX12::GetSceneFogColor( unsigned char *rgb ) { if(rgb){rgb[0]=fogColor_[0];rgb[1]=fogColor_[1];rgb[2]=fogColor_[2];} }
void CShaderAPIDX12::MatrixChanged()
{
    if(matrixMode_==MATERIAL_VIEW || matrixMode_==MATERIAL_PROJECTION || matrixMode_>=MATERIAL_MODEL) {
        // Fog depends on the view only through the camera position; CommitTransforms flags that change.
        transformsDirty_=true;
        if(matrixMode_>=MATERIAL_MODEL) maxBoneLoaded_=std::max(maxBoneLoaded_,std::min(NUM_MODEL_TRANSFORMS-1,int(matrixMode_)-int(MATERIAL_MODEL)));
    }
    if(matrixMode_>=MATERIAL_TEXTURE0&&matrixMode_<=MATERIAL_TEXTURE7){++fixedVSVersion_;textureMatrixIdentityMask_&=~(1u<<(matrixMode_-MATERIAL_TEXTURE0));}
}
// out = a*b for row-major VMatrix, accumulating each element in VMatrix::MatrixMul's k order.
static inline void MultiplyMatrixSSE(const VMatrix &a,const VMatrix &b,VMatrix &out)
{
    const __m128 b0=_mm_loadu_ps(b[0]),b1=_mm_loadu_ps(b[1]),b2=_mm_loadu_ps(b[2]),b3=_mm_loadu_ps(b[3]);
    for(int row=0;row<4;++row){
        __m128 sum=_mm_mul_ps(_mm_set1_ps(a[row][0]),b0);
        sum=_mm_add_ps(sum,_mm_mul_ps(_mm_set1_ps(a[row][1]),b1));
        sum=_mm_add_ps(sum,_mm_mul_ps(_mm_set1_ps(a[row][2]),b2));
        sum=_mm_add_ps(sum,_mm_mul_ps(_mm_set1_ps(a[row][3]),b3));
        _mm_storeu_ps(out[row],sum);
    }
}
void CShaderAPIDX12::CommitTransforms()
{
    if(!transformsDirty_)return;
    const VMatrix &view=matrices_[MATERIAL_VIEW],&projection=matrices_[MATERIAL_PROJECTION];
    const bool viewChanged=!cachedTransformValid_||std::memcmp(view.Base(),cachedTransformView_.Base(),sizeof(float)*16);
    if(viewChanged||std::memcmp(projection.Base(),cachedTransformProjection_.Base(),sizeof(float)*16)){cachedViewProjection_=projection*view;cachedTransformProjection_=projection;}
    if(viewChanged){
        VMatrix cameraToWorld;
        cachedCameraValid_=MatrixInverseGeneral(view,cameraToWorld);
        if(cachedCameraValid_)cachedCameraPosition_.Init(cameraToWorld[0][3],cameraToWorld[1][3],cameraToWorld[2][3]);
        cachedTransformView_=view;cachedTransformValid_=true;
    }
    const VMatrix &viewProjection=cachedViewProjection_;
    VMatrix modelViewProjection,modelView;
    MultiplyMatrixSSE(viewProjection,matrices_[MATERIAL_MODEL],modelViewProjection);
    MultiplyMatrixSSE(view,matrices_[MATERIAL_MODEL],modelView);
    SetVertexShaderConstant(VERTEX_SHADER_MODELVIEWPROJ,modelViewProjection.Base(),4);
    SetVertexShaderConstant(VERTEX_SHADER_VIEWPROJ,viewProjection.Base(),4);
    SetVertexShaderConstant(VERTEX_SHADER_MODELVIEWPROJ_THIRD_ROW,modelViewProjection[2],1);
    SetVertexShaderConstant(VERTEX_SHADER_VIEWPROJ_THIRD_ROW,viewProjection[2],1);
    SetVertexShaderConstant(VERTEX_SHADER_VIEWMODEL,modelView.Base(),4);
    for(int bone=0;bone<std::max(maxBoneLoaded_+1,motionBoneRows_);++bone)
        SetVertexShaderConstant(VERTEX_SHADER_MODEL+bone*3,matrices_[MATERIAL_MODEL+bone].Base(),3);
    if(cachedCameraValid_) {
        if(cameraPosition_!=cachedCameraPosition_)fogDirty_=true;
        cameraPosition_=cachedCameraPosition_;
        const float camera[4]={cameraPosition_.x,cameraPosition_.y,cameraPosition_.z,fogZ_};
        SetVertexShaderConstant(VERTEX_SHADER_CAMERA_POS,camera,1);
    }
    maxBoneLoaded_=0;transformsDirty_=false;
}
void CShaderAPIDX12::MatrixMode(MaterialMatrixMode_t mode) { if(mode>=MATERIAL_VIEW&&mode<=MATERIAL_MODEL_MAX)matrixMode_=mode; }
void CShaderAPIDX12::PushMatrix() { matrixStacks_[matrixMode_].AddToTail(matrices_[matrixMode_]); }
void CShaderAPIDX12::PopMatrix() { auto &stack=matrixStacks_[matrixMode_];if(stack.Count()){matrices_[matrixMode_]=stack.Tail();stack.RemoveMultipleFromTail(1);MatrixChanged();} }
void CShaderAPIDX12::LoadMatrix(float *m) {
    if(!m)return;
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)matrices_[matrixMode_][row][column]=m[column*4+row];
    MatrixChanged();
}
void CShaderAPIDX12::MultMatrix(float *m) {
    if(!m)return;VMatrix rhs;
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)rhs[row][column]=m[column*4+row];
    matrices_[matrixMode_]=rhs*matrices_[matrixMode_];MatrixChanged();
}
void CShaderAPIDX12::MultMatrixLocal(float *m) {
    if(!m)return;VMatrix rhs;
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)rhs[row][column]=m[column*4+row];
    matrices_[matrixMode_]=matrices_[matrixMode_]*rhs;MatrixChanged();
}
void CShaderAPIDX12::GetMatrix(MaterialMatrixMode_t mode,float *dst) {
    if(!dst||mode<MATERIAL_VIEW||mode>MATERIAL_MODEL_MAX)return;
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)dst[row*4+column]=matrices_[mode][column][row];
}
void CShaderAPIDX12::LoadIdentity() {
    // Texture matrices feed only the fixed-function VS version; SetDefaultState reloads them every pass.
    const bool texture=matrixMode_>=MATERIAL_TEXTURE0&&matrixMode_<=MATERIAL_TEXTURE7;
    const uint32_t bit=texture?1u<<(matrixMode_-MATERIAL_TEXTURE0):0u;
    if(texture&&(textureMatrixIdentityMask_&bit))return;
    matrices_[matrixMode_].Identity();MatrixChanged();textureMatrixIdentityMask_|=bit;
}
void CShaderAPIDX12::LoadCameraToWorld() {
    VMatrix inverse;if(!MatrixInverseGeneral(matrices_[MATERIAL_VIEW],inverse))return;
    inverse[0][3]=inverse[1][3]=inverse[2][3]=0;matrices_[matrixMode_]=inverse;MatrixChanged();
}
void CShaderAPIDX12::Ortho(double left,double top,double right,double bottom,double zNear,double zFar) {
    VMatrix projection;MatrixBuildOrtho(projection,left,top,right,bottom,zNear,zFar);
    matrices_[matrixMode_]=matrices_[matrixMode_]*projection;MatrixChanged();
}
void CShaderAPIDX12::PerspectiveX(double fovx,double aspect,double zNear,double zFar) {
    VMatrix projection;MatrixBuildPerspectiveX(projection,fovx,aspect,zNear,zFar);
    matrices_[matrixMode_]=matrices_[matrixMode_]*projection;MatrixChanged();
}
void CShaderAPIDX12::PickMatrix(int x,int y,int width,int height) {
    if(width<=0||height<=0||viewportCount_<=0)return;
    const auto &viewport=viewports_[0];if(viewport.m_nWidth<=0||viewport.m_nHeight<=0)return;
    VMatrix pick;pick.Identity();pick[0][0]=float(viewport.m_nWidth)/width;pick[1][1]=float(viewport.m_nHeight)/height;
    pick[0][3]=float(viewport.m_nWidth-2*(x-viewport.m_nTopLeftX))/width;
    pick[1][3]=float(viewport.m_nHeight-2*(y-viewport.m_nTopLeftY))/height;
    matrices_[matrixMode_]=matrices_[matrixMode_]*pick;MatrixChanged();
}
void CShaderAPIDX12::Rotate(float angle,float x,float y,float z) { MatrixRotate(matrices_[matrixMode_],Vector(x,y,z),angle);MatrixChanged(); }
void CShaderAPIDX12::Translate(float x,float y,float z) { MatrixTranslate(matrices_[matrixMode_],Vector(x,y,z));MatrixChanged(); }
void CShaderAPIDX12::Scale(float x,float y,float z) { VMatrix scale;MatrixBuildScale(scale,x,y,z);matrices_[matrixMode_]=matrices_[matrixMode_]*scale;MatrixChanged(); }
void CShaderAPIDX12::ScaleXY( float x, float y ) { Scale(x,y,1.f); }
void CShaderAPIDX12::Color3f(float r,float g,float b) { if(color_.x!=r||color_.y!=g||color_.z!=b||colorAlpha_!=1.f){color_.Init(r,g,b);colorAlpha_=1.f;++fixedPSVersion_;} }
void CShaderAPIDX12::Color3fv( float const* pColor ) { if(pColor) Color3f(pColor[0],pColor[1],pColor[2]); }
void CShaderAPIDX12::Color4f(float r,float g,float b,float a) { if(color_.x!=r||color_.y!=g||color_.z!=b||colorAlpha_!=a){color_.Init(r,g,b);colorAlpha_=a;++fixedPSVersion_;} }
void CShaderAPIDX12::Color4fv( float const* pColor ) { if(pColor) Color4f(pColor[0],pColor[1],pColor[2],pColor[3]); }
void CShaderAPIDX12::Color3ub( unsigned char r, unsigned char g, unsigned char b ) { Color3f(r/255.0f,g/255.0f,b/255.0f); }
void CShaderAPIDX12::Color3ubv( unsigned char const* pColor ) { if(pColor) Color3ub(pColor[0],pColor[1],pColor[2]); }
void CShaderAPIDX12::Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a ) { Color4f(r/255.0f,g/255.0f,b/255.0f,a/255.0f); }
void CShaderAPIDX12::Color4ubv( unsigned char const* pColor ) { if(pColor) Color4ub(pColor[0],pColor[1],pColor[2],pColor[3]); }
// Stores a validated bridge write and mirrors it into the legacy register files immediately, so a legacy
// record for the same logical name reads identical values with last-writer-wins ordering against direct
// register writes (exactly as the DX9 material code's own SetPixel/VertexShaderConstant calls would).
void CShaderAPIDX12::ApplyNativeCBufferWrite(const dx12native::NativeCBufferWriteDX12 &write,bool pixel)
{
    auto &slot=pixel?nativePSBlocks_[write.shaderRegister-1]:nativeVSBlocks_[write.shaderRegister-2];
    slot.bytes.resize(write.byteSize);std::memcpy(slot.bytes.data(),write.data,write.byteSize);
    slot.legacyMap=write.legacyMap;slot.layoutHash=write.layoutHash;slot.byteSize=write.byteSize;slot.written=true;
    slot.version=pixel?++nativePSVersion_:++nativeVSVersion_;
    const unsigned first=pixel?3:0;bool floats=false,ints=false,bools=false;
    const auto *bytes=static_cast<const unsigned char *>(write.data);
    for(uint32_t e=0;e<write.legacyMap->entryCount;++e){
        const auto &entry=write.legacyMap->entries[e];
        for(uint32_t n=0;n<entry.elementCount;++n){
            const unsigned char *src=bytes+entry.byteOffset+size_t(n)*entry.srcStride;const size_t reg=size_t(entry.reg)+n;
            if(size_t(entry.byteOffset)+size_t(n)*entry.srcStride+entry.elementBytes>write.byteSize)break;
            if(entry.bank==dx12native::kLegacyFloat&&reg<vsFloat_.size()){
                auto &dst=pixel?psFloat_[reg]:vsFloat_[reg];const size_t count=std::min<size_t>(entry.elementBytes,16u-entry.component*4u);
                if(std::memcmp(dst.data()+entry.component,src,count)){std::memcpy(dst.data()+entry.component,src,count);floats=true;}
            }else if(entry.bank==dx12native::kLegacyInt&&reg<vsInt_.size()){
                auto &dst=pixel?psInt_[reg]:vsInt_[reg];const size_t count=std::min<size_t>(entry.elementBytes,16u-entry.component*4u);
                if(std::memcmp(dst.data()+entry.component,src,count)){std::memcpy(dst.data()+entry.component,src,count);ints=true;}
            }else if(entry.bank==dx12native::kLegacyBool&&reg<vsBool_.size()){
                uint32_t value;std::memcpy(&value,src,4);bool &dst=pixel?psBool_[reg]:vsBool_[reg];
                if(dst!=(value!=0)){dst=value!=0;bools=true;}
            }
        }
    }
    if(floats)++constantVersions_[first];if(ints)++constantVersions_[first+1];if(bools)++constantVersions_[first+2];
}
void CShaderAPIDX12::SetVertexShaderConstant(int var,float const *values,int count,bool) { ZoneNamedN(constants, "DX12 SetVertexConstants sampled", DX12_DRAW_ZONES_ACTIVE && (frameCounter_&63)==0); if(var==dx12native::kDX12NativeCBufferPointerVar){ const auto *write=reinterpret_cast<const dx12native::NativeCBufferWriteDX12 *>(values); if(!write||write->magic!=dx12native::kDX12NativeCBufferWriteMagic||write->version!=dx12native::kDX12NativeCBufferVersion||write->stage!=dx12native::kStageVertex||write->registerSpace!=1||write->shaderRegister<2||write->shaderRegister>7||!write->data||!write->legacyMap||write->byteSize==0||(write->byteSize&15)||write->byteSize>65536||write->legacyMap->layoutHash!=write->layoutHash){Warning("ShaderAPIDX12: rejected native VS cbuffer write (magic/version/stage/space/register/size/data/map validation failed)\n");return;} ApplyNativeCBufferWrite(*write,false);return;} if(!values||var<0||count<=0)return;const int end=std::min(var+count,static_cast<int>(vsFloat_.size()));bool changed=false;for(int r=var;r<end;++r){const __m128i incoming=_mm_loadu_si128(reinterpret_cast<const __m128i *>(values+(r-var)*4));auto *slot=reinterpret_cast<__m128i *>(vsFloat_[r].data());if(_mm_movemask_epi8(_mm_cmpeq_epi32(incoming,_mm_loadu_si128(slot)))!=0xFFFF){_mm_storeu_si128(slot,incoming);changed=true;}}if(changed)++constantVersions_[0]; }
void CShaderAPIDX12::SetPixelShaderConstant(int var,float const *values,int count,bool) { ZoneNamedN(constants, "DX12 SetPixelConstants sampled", DX12_DRAW_ZONES_ACTIVE && (frameCounter_&63)==0); if(var==dx12native::kDX12NativeCBufferPointerVar){ const auto *write=reinterpret_cast<const dx12native::NativeCBufferWriteDX12 *>(values); if(!write||write->magic!=dx12native::kDX12NativeCBufferWriteMagic||write->version!=dx12native::kDX12NativeCBufferVersion||write->stage!=dx12native::kStagePixel||write->registerSpace!=1||write->shaderRegister<1||write->shaderRegister>7||!write->data||!write->legacyMap||write->byteSize==0||(write->byteSize&15)||write->byteSize>65536||write->legacyMap->layoutHash!=write->layoutHash){Warning("ShaderAPIDX12: rejected native PS cbuffer write (magic/version/stage/space/register/size/data/map validation failed)\n");return;} ApplyNativeCBufferWrite(*write,true);return;} if(!values||var<0||count<=0)return;const int end=std::min(var+count,static_cast<int>(psFloat_.size()));bool changed=false;for(int r=var;r<end;++r){const __m128i incoming=_mm_loadu_si128(reinterpret_cast<const __m128i *>(values+(r-var)*4));auto *slot=reinterpret_cast<__m128i *>(psFloat_[r].data());if(_mm_movemask_epi8(_mm_cmpeq_epi32(incoming,_mm_loadu_si128(slot)))!=0xFFFF){_mm_storeu_si128(slot,incoming);changed=true;}}if(changed)++constantVersions_[3]; }
void CShaderAPIDX12::ResetNativeState() {
 shadowState_=Snapshot{};activeSnapshot_=Snapshot{};
 boundVS_=VERTEX_SHADER_HANDLE_INVALID;boundGS_=GEOMETRY_SHADER_HANDLE_INVALID;boundPS_=PIXEL_SHADER_HANDLE_INVALID;
 activeSnapshotId_=static_cast<StateSnapshot_t>(-1);
 forceDepthEquals_=overrideDepthEnable_=false;overrideDepthValue_=true;shadeMode_=SHADER_SMOOTH;
 textureTransformEnabled_.fill(false);textureTransformProjected_.fill(false);textureTransformDimension_.fill(2);
 color_.Init(1.f,1.f,1.f);colorAlpha_=1.f;++fixedPSVersion_;++fixedVSVersion_;
 namedVertexShaderDirty_=namedPixelShaderDirty_=false;
 boundVertexShaderIsNamed_=boundPixelShaderIsNamed_=false;
 boundTextures_.fill(0);vertexTextures_.fill(0);boundMaterial_=nullptr;
 boundVertexBuffers_={};boundIndexBuffer_=nullptr;boundIndexOffset_=0;
 unusedVertexFields_=0;unusedTextureCoordinates_.fill(false);
 vertexShaderIndex_=-1;pixelShaderIndex_=0;
 rasterState_=ShaderRasterState_t{};rasterOverride_=false;cullMode_=MATERIAL_CULLMODE_CCW;
 stencilEnabled_=false;stencilRef_=0;stencilReadMask_=0xff;stencilWriteMask_=0xff;
 stencilCompare_=STENCILCOMPARISONFUNCTION_ALWAYS;stencilPassOp_=STENCILOPERATION_KEEP;stencilFailOp_=STENCILOPERATION_KEEP;stencilDepthFailOp_=STENCILOPERATION_KEEP;
 alphaToCoverage_=false;colorWriteOverride_=false;alphaWriteOverride_=false;fastIntParams_[4]=0;
 vsFloat_[VERTEX_SHADER_MATH_CONSTANTS0]={0.f,1.f,2.f,.5f};
 vsFloat_[VERTEX_SHADER_MATH_CONSTANTS1]={1.f/GAMMA,OVERBRIGHT,1.f/3.f,OO_OVERBRIGHT};
 ++constantVersions_[0];
 transformsDirty_=true;
 fogDirty_=true;pixelFogRegister_=-1;
 SetToneMappingScaleLinear(toneScale_);
 SetDefaultState();
}
void CShaderAPIDX12::GetWorldSpaceCameraPosition( float* pPos ) const { if(pPos) std::memcpy(pPos,&cameraPosition_,sizeof(float)*3); }
int CShaderAPIDX12::GetCurrentNumBones( void ) const { return boneCount_; }
int CShaderAPIDX12::GetCurrentLightCombo() const {
    LightState_t state{};GetDX9LightState(&state);
    if(!state.m_nNumLights&&!state.m_bAmbientLight)return state.m_bStaticLightVertex?1:0;
    if(state.m_nNumLights>2){Warning("ShaderAPIDX12: legacy lighting combos support at most two local lights\n");return 0;}
    std::array<int,4> indices{};SortLocalLights(indices);
    const auto rank=[](LightType_t type){return type==MATERIAL_LIGHT_SPOT?0:(type==MATERIAL_LIGHT_POINT?1:2);};
    const int first=state.m_nNumLights?rank(lights_[indices[0]].m_Type):-1;
    const int second=state.m_nNumLights>1?rank(lights_[indices[1]].m_Type):-1;
    static constexpr int combinations[10][2]={{-1,-1},{0,-1},{1,-1},{2,-1},{0,0},{0,1},{0,2},{1,1},{1,2},{2,2}};
    for(int i=0;i<10;++i)if(combinations[i][0]==first&&combinations[i][1]==second)return i+2+(state.m_bStaticLightVertex?10:0);
    return 0;
}
MaterialFogMode_t CShaderAPIDX12::GetCurrentFogType( void ) const { return fogMode_; }
void CShaderAPIDX12::SetTextureTransformDimension(TextureStage_t stage,int dimension,bool projected) { if(stage<0||stage>=8)return;dimension=std::clamp(dimension,1,4);if(textureTransformEnabled_[stage]&&textureTransformDimension_[stage]==dimension&&textureTransformProjected_[stage]==projected)return;textureTransformEnabled_[stage]=true;textureTransformDimension_[stage]=dimension;textureTransformProjected_[stage]=projected;++fixedVSVersion_; }
void CShaderAPIDX12::DisableTextureTransform(TextureStage_t stage) { if(stage>=0&&stage<8&&textureTransformEnabled_[stage]){textureTransformEnabled_[stage]=false;++fixedVSVersion_;} }
void CShaderAPIDX12::SetBumpEnvMatrix(TextureStage_t stage,float m00,float m01,float m10,float m11) { if(stage>=0&&stage<8){bumpMatrices_[stage]={m00,m01,m10,m11};bumpExtensionDirty_=true;} }
void CShaderAPIDX12::SetVertexShaderIndex(int index) { if(activeSnapshot_.vertexShaderName.empty())namedVertexShaderDirty_=false;else namedVertexShaderDirty_|=vertexShaderIndex_!=index||!boundVertexShaderIsNamed_;vertexShaderIndex_=index; }
void CShaderAPIDX12::SetPixelShaderIndex(int index) { if(activeSnapshot_.pixelShaderName.empty())namedPixelShaderDirty_=false;else namedPixelShaderDirty_|=pixelShaderIndex_!=index||!boundPixelShaderIsNamed_;pixelShaderIndex_=index; }
void CShaderAPIDX12::GetBackBufferDimensions( int& width, int& height ) const { if(device_)device_->GetBackBufferDimensions(width,height);else{width=height=0;} }
int CShaderAPIDX12::GetMaxLights( void ) const { return 4; }
const LightDesc_t& CShaderAPIDX12::GetLight(int lightNum) const {
    static const LightDesc_t disabled{};
    return lightNum>=0&&lightNum<static_cast<int>(lights_.size())?lights_[lightNum]:disabled;
}
bool CShaderAPIDX12::ShouldUsePixelFog() const
{
    return fogMode_==MATERIAL_FOG_LINEAR_BELOW_FOG_Z ||
        (fogMode_==MATERIAL_FOG_LINEAR && (!pixelFogConVar_||pixelFogConVar_->GetBool()));
}

void CShaderAPIDX12::CommitFogState()
{
    const bool pixelFog=ShouldUsePixelFog(),srgbWrite=EffectiveSRGBWrite();
    const HDRType_t hdr=g_pHardwareConfigDX12?g_pHardwareConfigDX12->GetHDRType():HDR_TYPE_NONE;
    if(!fogDirty_&&pixelFog==lastPixelFog_&&srgbWrite==lastFogSRGBWrite_&&hdr==lastFogHDR_)return;
    lastPixelFog_=pixelFog;lastFogSRGBWrite_=srgbWrite;lastFogHDR_=hdr;fogDirty_=false;
    FogInputsDX12 inputs;std::memset(&inputs,0,sizeof(inputs));
    inputs.start=fogStart_;inputs.end=fogEnd_;inputs.z=fogZ_;inputs.density=fogMaxDensity_;inputs.camera[0]=cameraPosition_.x;inputs.camera[1]=cameraPosition_.y;inputs.camera[2]=cameraPosition_.z;inputs.tone=toneScale_.x;
    inputs.mode=fogMode_;inputs.passFog=activeSnapshot_.fogMode;inputs.hdr=hdr;
    std::memcpy(inputs.color,fogColor_,sizeof(inputs.color));inputs.pixelFog=pixelFog;inputs.srgbWrite=srgbWrite;inputs.gammaDisabled=activeSnapshot_.fogGammaDisabled;
    // Unchanged inputs: the shaders already see these values; re-issue the (compare-filtered) register writes
    // in case other callers overwrote them, but keep the fixed-function constant version stable.
    if(!fogOutputsValid_||std::memcmp(&inputs,&lastFogInputs_,sizeof(inputs))){
    ZoneNamedN(fogRecompute, "DX12 FogRecompute", DX12_DRAW_ZONES_ACTIVE);
    lastFogInputs_=inputs;fogOutputsValid_=true;
    ++fixedPSVersion_;
    const float inverseRange=fogEnd_!=fogStart_?1.f/(fogEnd_-fogStart_):1.f;
    const float density=std::clamp(fogMaxDensity_,0.f,1.f);
    fogVertexParams_={fogEnd_*inverseRange,1.f,1.f-density,inverseRange};
    fogCameraParams_={cameraPosition_.x,cameraPosition_.y,cameraPosition_.z,fogZ_};
    fogPixelParams_={0.f,fogZ_,1.f,0.f};
    // Only uploaded to a pixel fog register, so computed as if one is bound; register changes need no recompute.
    if(pixelFog&&activeSnapshot_.fogMode!=SHADER_FOGMODE_DISABLED){
        fogPixelParams_[0]=fogMode_==MATERIAL_FOG_LINEAR_BELOW_FOG_Z?0.f:fogStart_*inverseRange;
        fogPixelParams_[2]=fogMode_==MATERIAL_FOG_LINEAR_BELOW_FOG_Z?1.f:density;
        fogPixelParams_[3]=inverseRange;
    }
    float color[3]={0.f,0.f,0.f};
    const ShaderFogMode_t passFog=activeSnapshot_.fogMode;
    if(passFog==SHADER_FOGMODE_FOGCOLOR){for(int i=0;i<3;++i)color[i]=fogColor_[i]/255.f;}
    else if(passFog==SHADER_FOGMODE_WHITE){for(float &c:color)c=1.f;}
    else if(passFog==SHADER_FOGMODE_GREY||passFog==SHADER_FOGMODE_OO_OVERBRIGHT){for(float &c:color)c=128.f/255.f;}
    const bool correctGamma=!activeSnapshot_.fogGammaDisabled&&passFog!=SHADER_FOGMODE_BLACK&&passFog!=SHADER_FOGMODE_WHITE;
    fogPixelColor_={0.f,0.f,0.f,1.f/(hdr==HDR_TYPE_FLOAT?8192.f:192.f)};
    for(int i=0;i<3;++i){
        float fixed=color[i];
        if(correctGamma){
            if(srgbWrite)fixed=GammaToLinear(fixed);
            if(hdr==HDR_TYPE_INTEGER)fixed*=srgbWrite?toneScale_.x:LinearToGammaFullRange(toneScale_.x);
        }
        rasterFogColor_[i]=std::clamp(fixed,0.f,1.f);
        if(pixelFog&&passFog!=SHADER_FOGMODE_DISABLED){
            fogPixelColor_[i]=(srgbWrite||fogMode_==MATERIAL_FOG_LINEAR_BELOW_FOG_Z)?GammaToLinear_HardwareSpecific(color[i]):color[i];
            if(correctGamma&&hdr==HDR_TYPE_INTEGER)fogPixelColor_[i]*=toneScale_.x;
        }
    }
    }
    SetVertexShaderConstant(VERTEX_SHADER_FOG_PARAMS,fogVertexParams_.data(),1);
    SetVertexShaderConstant(VERTEX_SHADER_CAMERA_POS,fogCameraParams_.data(),1);
    if(pixelFogRegister_>=0)SetPixelShaderConstant(pixelFogRegister_,fogPixelParams_.data(),1);
    SetPixelShaderConstant(LINEAR_FOG_COLOR,fogPixelColor_.data(),1);
}

void CShaderAPIDX12::SetPixelShaderFogParams(int reg)
{
    if(reg<0||reg>=static_cast<int>(psFloat_.size()))return;
    // Recompute only when an input changed; otherwise restore the cached values, since the
    // material may have written these registers after the previous commit.
    // Fog outputs do not depend on the register; the new register just receives them below.
    pixelFogRegister_=reg;
    const bool recompute=fogDirty_||!fogOutputsValid_;
    CommitFogState();
    if(!recompute&&fogOutputsValid_){
        SetVertexShaderConstant(VERTEX_SHADER_FOG_PARAMS,fogVertexParams_.data(),1);
        SetVertexShaderConstant(VERTEX_SHADER_CAMERA_POS,fogCameraParams_.data(),1);
        SetPixelShaderConstant(pixelFogRegister_,fogPixelParams_.data(),1);
        SetPixelShaderConstant(LINEAR_FOG_COLOR,fogPixelColor_.data(),1);
    }
}
void CShaderAPIDX12::SetVertexShaderStateAmbientLightCube() { SetVertexShaderConstant(VERTEX_SHADER_AMBIENT_LIGHT,ambientCube_[0].data(),6); }
void CShaderAPIDX12::SetPixelShaderStateAmbientLightCube(int reg,bool forceBlack) {
    static constexpr float black[24]{};const float *cube=forceBlack?black:ambientCube_[0].data();
    SetPixelShaderConstant(reg,cube,6);
    // Native pixel shaders read the same values from DX12PSEngine.cAmbientCube.
    if(std::memcmp(nativePSEngine_.cAmbientCube,cube,sizeof(nativePSEngine_.cAmbientCube))){std::memcpy(nativePSEngine_.cAmbientCube,cube,sizeof(nativePSEngine_.cAmbientCube));++nativePSEngineVersion_;}
}
int CShaderAPIDX12::SortLocalLights(std::array<int,4> &indices) const {
    const auto rank=[](LightType_t type){return type==MATERIAL_LIGHT_SPOT?0:(type==MATERIAL_LIGHT_POINT?1:2);};
    int count=0;
    for(int i=0;i<static_cast<int>(lights_.size());++i) {
        const LightType_t type=lights_[i].m_Type;
        if(type!=MATERIAL_LIGHT_POINT&&type!=MATERIAL_LIGHT_SPOT&&type!=MATERIAL_LIGHT_DIRECTIONAL)continue;
        int position=count;
        while(position>0&&rank(lights_[indices[position-1]].m_Type)>rank(type)){indices[position]=indices[position-1];--position;}
        indices[position]=i;++count;
    }
    return count;
}
void CShaderAPIDX12::CommitVertexLighting() {
    if(!lightingDirty_)return;
    std::array<int,4> indices{};const int count=SortLocalLights(indices);
    for(int i=0;i<count;++i) {
        const LightDesc_t &light=lights_[indices[i]];float constants[5][4]{};
        std::copy_n(light.m_Color.Base(),3,constants[0]);constants[0][3]=light.m_Type==MATERIAL_LIGHT_DIRECTIONAL?1.f:0.f;
        if(light.m_Type!=MATERIAL_LIGHT_POINT)std::copy_n(light.m_Direction.Base(),3,constants[1]);
        constants[1][3]=light.m_Type==MATERIAL_LIGHT_SPOT?1.f:0.f;
        if(light.m_Type!=MATERIAL_LIGHT_DIRECTIONAL)std::copy_n(light.m_Position.Base(),3,constants[2]);constants[2][3]=1.f;
        if(light.m_Type==MATERIAL_LIGHT_SPOT) {
            const float phi=std::min(light.m_Phi,float(M_PI));const float theta=std::min(light.m_Theta,phi-.001f);
            constants[3][0]=light.m_Falloff;constants[3][1]=std::cos(theta*.5f);constants[3][2]=std::cos(phi*.5f);
            constants[3][3]=constants[3][1]>constants[3][2]?1.f/(constants[3][1]-constants[3][2]):0.f;
        }else{constants[3][1]=constants[3][2]=constants[3][3]=1.f;}
        constants[4][0]=light.m_Attenuation0;constants[4][1]=light.m_Attenuation1;constants[4][2]=light.m_Attenuation2;
        SetVertexShaderConstant(VERTEX_SHADER_LIGHTS+i*5,constants[0],5);
    }
    const int loop[4]={count,0,1,0};SetIntegerVertexShaderConstant(0,loop,1);
    BOOL enabled[4]={count>0,count>1,count>2,count>3};SetBooleanVertexShaderConstant(VERTEX_SHADER_LIGHT_ENABLE_BOOL_CONST,enabled,4);
    lightingDirty_=false;
}
void CShaderAPIDX12::CommitPixelShaderLighting(int reg) {
    std::array<int,4> indices{};const int count=SortLocalLights(indices);float constants[6][4]{};
    for(int i=0;i<count;++i) {
        const LightDesc_t &light=lights_[indices[i]];
        const Vector position=light.m_Type==MATERIAL_LIGHT_DIRECTIONAL?lightingOrigin_-light.m_Direction*10000.f:light.m_Position;
        if(i<3){std::copy_n(light.m_Color.Base(),3,constants[i*2]);std::copy_n(position.Base(),3,constants[i*2+1]);}
        else for(int component=0;component<3;++component){constants[component][3]=light.m_Color[component];constants[component+3][3]=position[component];}
    }
    SetPixelShaderConstant(reg,constants[0],6);
    // Native pixel shaders read the same values from DX12PSEngine.cLightInfo.
    if(std::memcmp(nativePSEngine_.cLightInfo,constants,sizeof(nativePSEngine_.cLightInfo))){std::memcpy(nativePSEngine_.cLightInfo,constants,sizeof(nativePSEngine_.cLightInfo));++nativePSEngineVersion_;}
}
CMeshBuilder* CShaderAPIDX12::GetVertexModifyBuilder() { return &vertexModifyBuilder_; }
const FlashlightState_t &CShaderAPIDX12::GetFlashlightState( VMatrix &worldToTexture ) const { worldToTexture=flashlightMatrix_; return flashlight_; }
bool CShaderAPIDX12::InFlashlightMode() const { return flashlightMode_; }
bool CShaderAPIDX12::InEditorMode() const { return editorMode_; }
MorphFormat_t CShaderAPIDX12::GetBoundMorphFormat() { return shaderUtil_?shaderUtil_->GetBoundMorphFormat():0; }
void CShaderAPIDX12::BindStandardTexture(Sampler_t sampler,StandardTextureId_t id)
{
    if(id<0||id>=TEXTURE_MAX_STD_TEXTURES||sampler<0||sampler>=static_cast<int>(boundTextures_.size()))return;
    const auto handle=standardTextures_[id];
    if(handle!=INVALID_SHADERAPI_TEXTURE_HANDLE)BindTexture(sampler,handle);
    else if(shaderUtil_)shaderUtil_->BindStandardTexture(sampler,id);
}
void CShaderAPIDX12::SetToneMappingScaleLinear(const Vector &scale)
{
    const HDRType_t hdr=g_pHardwareConfigDX12?g_pHardwareConfigDX12->GetHDRType():HDR_TYPE_NONE;
    const Vector next(hdr==HDR_TYPE_NONE?1.f:scale.x,GetLightMapScaleFactor(),hdr==HDR_TYPE_INTEGER?MAX_HDR_OVERBRIGHT:1.f);
    // Fog output and the gamma-space term depend only on these values; skip recomputation when unchanged.
    if(!toneScaleConstantValid_||next!=toneScale_){toneScale_=next;fogDirty_=true;toneScaleGamma_=LinearToGammaFullRange(toneScale_.x);toneScaleConstantValid_=true;}
    const float constants[4]={toneScale_.x,toneScale_.y,toneScale_.z,toneScaleGamma_};
    SetPixelShaderConstant(TONE_MAPPING_SCALE_PSH_CONSTANT,constants,1);
}
const Vector &CShaderAPIDX12::GetToneMappingScaleLinear( void ) const { return toneScale_; }
float CShaderAPIDX12::GetLightMapScaleFactor() const
{
    const HDRType_t hdr=g_pHardwareConfigDX12?g_pHardwareConfigDX12->GetHDRType():HDR_TYPE_NONE;
    return hdr==HDR_TYPE_FLOAT?1.f:(hdr==HDR_TYPE_INTEGER?MAX_HDR_OVERBRIGHT:GammaToLinearFullRange(2.f));
}
void CShaderAPIDX12::LoadBoneMatrix(int boneIndex,const float *m) {
    if(!m||boneIndex<0||boneIndex>=NUM_MODEL_TRANSFORMS)return;
    VMatrix &bone=matrices_[MATERIAL_MODEL+boneIndex];bone.Identity();std::memcpy(bone.Base(),m,12*sizeof(float));
    maxBoneLoaded_=std::max(maxBoneLoaded_,boneIndex);motionBoneRows_=std::max(motionBoneRows_,boneIndex+1);transformsDirty_=true;
    if(boneIndex==0)matrixMode_=MATERIAL_MODEL;
}
void CShaderAPIDX12::PerspectiveOffCenterX(double fovx,double aspect,double zNear,double zFar,double bottom,double top,double left,double right) {
    VMatrix projection;MatrixBuildPerspectiveOffCenterX(projection,fovx,aspect,zNear,zFar,bottom,top,left,right);
    matrices_[matrixMode_]=matrices_[matrixMode_]*projection;MatrixChanged();
}
void CShaderAPIDX12::SetFloatRenderingParameter(int parm_number, float value) { if(parm_number>=0 && parm_number<(int)renderingFloats_.size()) renderingFloats_[parm_number]=value; }
void CShaderAPIDX12::SetStencilEnable(bool onoff) { stencilEnabled_=onoff; }
void CShaderAPIDX12::SetStencilFailOperation(StencilOperation_t op) { stencilFailOp_=op; }
void CShaderAPIDX12::SetStencilZFailOperation(StencilOperation_t op) { stencilDepthFailOp_=op; }
void CShaderAPIDX12::SetStencilPassOperation(StencilOperation_t op) { stencilPassOp_=op; }
void CShaderAPIDX12::SetStencilCompareFunction(StencilComparisonFunction_t cmpfn) { stencilCompare_=cmpfn; }
void CShaderAPIDX12::SetStencilReferenceValue(int ref) { stencilRef_=ref; }
void CShaderAPIDX12::SetStencilTestMask(uint32 msk) { stencilReadMask_=static_cast<uint8_t>(msk); }
void CShaderAPIDX12::SetStencilWriteMask(uint32 msk) { stencilWriteMask_=static_cast<uint8_t>(msk); }
void CShaderAPIDX12::GetDXLevelDefaults(uint &max_dxlevel,uint &recommended_dxlevel) { max_dxlevel=95; recommended_dxlevel=95; }
const FlashlightState_t &CShaderAPIDX12::GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **pFlashlightDepthTexture ) const { worldToTexture=flashlightMatrix_; if(pFlashlightDepthTexture)*pFlashlightDepthTexture=flashlight_.m_pSpotlightTexture; return flashlight_; }
float CShaderAPIDX12::GetAmbientLightCubeLuminance() {
    float luminance=0.f;for(const auto &face:ambientCube_)luminance+=.3f*face[0]+.59f*face[1]+.11f*face[2];return luminance/6.f;
}
void CShaderAPIDX12::GetDX9LightState(LightState_t *state) const {
    if(!state)return;*state={};std::array<int,4> indices{};state->m_nNumLights=SortLocalLights(indices);
    for(const auto &face:ambientCube_)if(face[0]!=0.f||face[1]!=0.f||face[2]!=0.f){state->m_bAmbientLight=true;break;}
    state->m_bStaticLightVertex=renderMesh_&&renderMesh_->ColorMesh()!=nullptr;
}
int CShaderAPIDX12::GetPixelFogCombo() { return ShouldUsePixelFog()&&fogMode_==MATERIAL_FOG_LINEAR_BELOW_FOG_Z?1:0; }
void CShaderAPIDX12::BindStandardVertexTexture(VertexTextureSampler_t sampler,StandardTextureId_t id)
{
    if(id>=0&&id<TEXTURE_MAX_STD_TEXTURES&&sampler>=0&&sampler<static_cast<int>(vertexTextures_.size())&&shaderUtil_)
        shaderUtil_->BindStandardVertexTexture(sampler,id);
}
bool CShaderAPIDX12::IsHWMorphingEnabled( ) const { return morphing_; }
void CShaderAPIDX12::GetStandardTextureDimensions(int *width,int *height,StandardTextureId_t id)
{
    if(width)*width=0;if(height)*height=0;
    if(shaderUtil_&&id>=0&&id<TEXTURE_MAX_STD_TEXTURES)shaderUtil_->GetStandardTextureDimensions(width,height,id);
}
void CShaderAPIDX12::SetBooleanVertexShaderConstant(int var,BOOL const *values,int count,bool) { if(!values||var<0||count<=0)return;bool changed=false;for(int i=0;i<count&&var+i<(int)vsBool_.size();++i){const bool value=values[i]!=FALSE;if(vsBool_[var+i]!=value){vsBool_[var+i]=value;changed=true;}}if(changed)++constantVersions_[2]; }
void CShaderAPIDX12::SetIntegerVertexShaderConstant(int var,int const *values,int count,bool) { if(!values||var<0||count<=0)return;bool changed=false;for(int i=0;i<count&&var+i<(int)vsInt_.size();++i)if(std::memcmp(values+i*4,vsInt_[var+i].data(),sizeof(int)*4)){std::copy_n(values+i*4,4,vsInt_[var+i].begin());changed=true;}if(changed)++constantVersions_[1]; }
void CShaderAPIDX12::SetBooleanPixelShaderConstant(int var,BOOL const *values,int count,bool) { if(!values||var<0||count<=0)return;bool changed=false;for(int i=0;i<count&&var+i<(int)psBool_.size();++i){const bool value=values[i]!=FALSE;if(psBool_[var+i]!=value){psBool_[var+i]=value;changed=true;}}if(changed)++constantVersions_[5]; }
void CShaderAPIDX12::SetIntegerPixelShaderConstant(int var,int const *values,int count,bool) { if(!values||var<0||count<=0)return;bool changed=false;for(int i=0;i<count&&var+i<(int)psInt_.size();++i)if(std::memcmp(values+i*4,psInt_[var+i].data(),sizeof(int)*4)){std::copy_n(values+i*4,4,psInt_[var+i].begin());changed=true;}if(changed)++constantVersions_[4]; }
bool CShaderAPIDX12::ShouldWriteDepthToDestAlpha() const { return fogMode_!=MATERIAL_FOG_LINEAR_BELOW_FOG_Z&&GetIntRenderingParameter(INT_RENDERPARM_WRITE_DEPTH_TO_DESTALPHA)!=0; }
void CShaderAPIDX12::PushDeformation( DeformationBase_t const *deformation ) { if(deformation&&deformation->m_eType==DEFORMATION_CLAMP_TO_BOX_IN_WORLDSPACE)deformations_.AddToTail(*static_cast<const BoxDeformation_t *>(deformation));else if(deformation)Warning("ShaderAPIDX12: unsupported deformation type %d\n",deformation->m_eType); }
void CShaderAPIDX12::PopDeformation( ) { if(!(deformations_.Count()==0))deformations_.RemoveMultipleFromTail(1); }
int CShaderAPIDX12::GetNumActiveDeformations() const { return static_cast<int>(deformations_.Count()); }
void CShaderAPIDX12::SetStandardTextureHandle(StandardTextureId_t id,ShaderAPITextureHandle_t handle) { if(id>=0 && id<TEXTURE_MAX_STD_TEXTURES)standardTextures_[id]=handle; }
void CShaderAPIDX12::ExecuteCommandBuffer(uint8 *buffer)
{
    ZoneNamedN(commandDispatch, "DX12 ExecuteCommandBuffer sampled", DX12_ZONES_ACTIVE && (frameCounter_&63)==0);
    if(!buffer)return;
    uint8 *returns[20];int depth=0;size_t steps=0;
    auto readInt=[](const uint8 *p){int value;std::memcpy(&value,p,sizeof(value));return value;};
    auto readPointer=[](const uint8 *p){uint8 *value;std::memcpy(&value,p,sizeof(value));return value;};
    for(uint8 *pc=buffer;pc&&steps++<1000000;)
    {
        int opcode=readInt(pc);pc+=sizeof(int);
        switch(opcode)
        {
        case CBCMD_END: if(!depth)return;pc=returns[--depth];break;
        case CBCMD_JUMP: pc=readPointer(pc);break;
        case CBCMD_JSR: if(depth==20){Warning("ShaderAPIDX12: command buffer call stack overflow\n");return;}returns[depth++]=pc+sizeof(void *);pc=readPointer(pc);break;
        case CBCMD_SET_PIXEL_SHADER_FLOAT_CONST:
        case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST:
        {
            int reg=readInt(pc),count=readInt(pc+sizeof(int));pc+=2*sizeof(int);
            if(count<0||count>256){Warning("ShaderAPIDX12: invalid command buffer constant count\n");return;}
            if(opcode==CBCMD_SET_PIXEL_SHADER_FLOAT_CONST)SetPixelShaderConstant(reg,reinterpret_cast<const float *>(pc),count);
            else SetVertexShaderConstant(reg,reinterpret_cast<const float *>(pc),count);
            pc+=static_cast<size_t>(count)*4*sizeof(float);break;
        }
        case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST_REF:
        {
            int reg=readInt(pc),count=readInt(pc+sizeof(int));pc+=2*sizeof(int);
            const float *values=nullptr;std::memcpy(&values,pc,sizeof(values));pc+=sizeof(values);
            if(count<0||count>256)return;SetVertexShaderConstant(reg,values,count);break;
        }
        case CBCMD_SETPIXELSHADERFOGPARAMS: SetPixelShaderFogParams(readInt(pc));pc+=sizeof(int);break;
        case CBCMD_STORE_EYE_POS_IN_PSCONST:
        {
            float eye[4]={cameraPosition_.x,cameraPosition_.y,cameraPosition_.z,1.f};SetPixelShaderConstant(readInt(pc),eye,1);pc+=sizeof(int);break;
        }
        case CBCMD_COMMITPIXELSHADERLIGHTING: CommitPixelShaderLighting(readInt(pc));pc+=sizeof(int);break;
        case CBCMD_SETPIXELSHADERSTATEAMBIENTLIGHTCUBE: SetPixelShaderStateAmbientLightCube(readInt(pc),false);pc+=sizeof(int);break;
        case CBCMD_SETAMBIENTCUBEDYNAMICSTATEVERTEXSHADER: SetVertexShaderStateAmbientLightCube();break;
        case CBCMD_SET_DEPTH_FEATHERING_CONST:
        {
            int reg=readInt(pc);float scale;std::memcpy(&scale,pc+sizeof(int),sizeof(scale));pc+=sizeof(int)+sizeof(scale);SetDepthFeatheringPixelShaderConstant(reg,scale);break;
        }
        case CBCMD_BIND_STANDARD_TEXTURE:
        {
            int sampler=readInt(pc),id=readInt(pc+sizeof(int));pc+=2*sizeof(int);if(id>=0&&id<TEXTURE_MAX_STD_TEXTURES)BindStandardTexture(static_cast<Sampler_t>(sampler),static_cast<StandardTextureId_t>(id));break;
        }
        case CBCMD_BIND_SHADERAPI_TEXTURE_HANDLE:
        {
            int sampler=readInt(pc);pc+=sizeof(int);ShaderAPITextureHandle_t handle;std::memcpy(&handle,pc,sizeof(handle));pc+=sizeof(handle);BindTexture(static_cast<Sampler_t>(sampler),handle);break;
        }
        case CBCMD_SET_PSHINDEX: SetPixelShaderIndex(readInt(pc));pc+=sizeof(int);break;
        case CBCMD_SET_VSHINDEX: SetVertexShaderIndex(readInt(pc));pc+=sizeof(int);break;
        default: Warning("ShaderAPIDX12: unsupported material command opcode %d\n",opcode);return;
        }
    }
    Warning("ShaderAPIDX12: command buffer exceeded execution limit\n");
}
int CShaderAPIDX12::GetPackedDeformationInformation(int mask,float *constants,int byteCount,int maximum,int *combos) const
{
    if(maximum<0||byteCount<0)return 0;
    if(combos)std::fill_n(combos,maximum,0);
    if(!constants||!combos)return 0;
    int found=0;constexpr int floats=16;
    for(auto it=deformations_.begin();it!=deformations_.end()&&found<maximum;++it)
    {
        if(!(static_cast<unsigned int>(mask)&(1u<<static_cast<unsigned int>(it->m_eType)))||byteCount<static_cast<int>(floats*sizeof(float)))continue;
        const float values[floats]={it->m_SourceMins.x,it->m_SourceMins.y,it->m_SourceMins.z,it->m_flPad0,it->m_SourceMaxes.x,it->m_SourceMaxes.y,it->m_SourceMaxes.z,it->m_flPad1,it->m_ClampMins.x,it->m_ClampMins.y,it->m_ClampMins.z,it->m_flPad2,it->m_ClampMaxes.x,it->m_ClampMaxes.y,it->m_ClampMaxes.z,it->m_flPad3};
        std::memcpy(constants,values,sizeof(values));constants+=floats;byteCount-=sizeof(values);combos[found++]=it->m_eType;
    }
    return found;
}
void CShaderAPIDX12::MarkUnusedVertexFields(unsigned int flags,int count,bool *unused)
{
 unusedVertexFields_=flags;unusedTextureCoordinates_.fill(false);
 if(unused&&count>0)std::copy_n(unused,std::min(count,static_cast<int>(unusedTextureCoordinates_.size())),unusedTextureCoordinates_.begin());
}
void CShaderAPIDX12::GetCurrentColorCorrection(ShaderColorCorrectionInfo_t *info) { if(!info)return;if(shaderUtil_)shaderUtil_->GetCurrentColorCorrection(info);else{*info={};info->m_flDefaultWeight=1.f;} }
void CShaderAPIDX12::SetPSNearAndFarZ(int reg)
{
    const VMatrix &projection=matrices_[MATERIAL_PROJECTION];const float z=projection[2][2],w=projection[2][3];
    if(z==0.f)return;
    const float nearDistance=w/z;if(nearDistance+w==0.f)return;
    const float farDistance=w*nearDistance/(nearDistance+w);
    float values[4]={nearDistance,farDistance,0.f,0.f};SetPixelShaderConstant(reg,values,1);
}
// shaderapidx8.cpp:5225-5253 (PC): x = dest-alpha depth range / scale (8192 float HDR, else 192), yzw = 0.
void CShaderAPIDX12::SetDepthFeatheringPixelShaderConstant(int reg,float scale) { const HDRType_t hdr=g_pHardwareConfigDX12?g_pHardwareConfigDX12->GetHDRType():HDR_TYPE_NONE;const float values[4]={(hdr==HDR_TYPE_FLOAT?8192.f:192.f)/scale,0.f,0.f,0.f};SetPixelShaderConstant(reg,values,1); }
int CShaderAPIDX12::GetPixelFogCombo1(bool supportsRadial) { return !ShouldUsePixelFog()?0:(fogMode_==MATERIAL_FOG_LINEAR_BELOW_FOG_Z?1:(supportsRadial&&fogRadial_?2:0)); }
void CShaderAPIDX12::ClearColor3ub( unsigned char r, unsigned char g, unsigned char b ) { ClearColor4ub(r,g,b,255); }
void CShaderAPIDX12::ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a ) { clearColor_[0]=r/255.0f;clearColor_[1]=g/255.0f;clearColor_[2]=b/255.0f;clearColor_[3]=a/255.0f; }
void CShaderAPIDX12::ShutdownDeviceResources()
{
    SetShaderPrecacheAccepting(false);
    ProcessPendingTextureDeletes();
    if(device_&&device_->IsRecordingOwner()&&device_->CommandList())device_->Submit(true);
    ReleaseTextureDeviceResources();
    for(auto *query:occlusionQueries_)delete query;
    occlusionQueries_.RemoveAll();pipeline_.Shutdown();
    for(auto *mesh:dynamicMeshes_){mesh->Vertices().NativeResourceRef().Reset();mesh->Indices().NativeResourceRef().Reset();}
    for(auto &cached:targetDescs_)cached=CachedResourceDescDX12{};
    ++namedResolveEpoch_;++pipelineMemoEpoch_;
    SetDevice(nullptr);
}
bool CShaderAPIDX12::InitializeDeviceResources(CShaderDeviceDX12 *device)
{
    ShutdownDeviceResources();
    SetDevice(device);SetShaderUtil(g_pShaderDeviceMgrDX12?g_pShaderDeviceMgrDX12->HostShaderUtil():nullptr);
    ConVarRef pixelFog("r_pixelfog",true);
    pixelFogConVar_=pixelFog.IsValid()?static_cast<ConVar *>(pixelFog.GetLinkedConVar()):nullptr;
    fogDirty_=true;
    if(!device||!device->NativeDevice()||!pipeline_.Initialize(device->NativeDevice())){Warning("ShaderAPIDX12: pipeline initialization failed\n");ShutdownDeviceResources();return false;}
    SetShaderPrecacheAccepting(true);
    return true;
}
bool CShaderAPIDX12::SetMode(void *hwnd,int adapter,const ShaderDeviceInfo_t &info)
{
    return g_pShaderDeviceMgrDX12&&g_pShaderDeviceMgrDX12->SetMode(hwnd,adapter,info)!=nullptr;
}
void CShaderAPIDX12::BindVertexShader(VertexShaderHandle_t shader) { boundVS_=shader;namedVertexShaderDirty_=false;boundVertexShaderIsNamed_=false; }
void CShaderAPIDX12::BindGeometryShader( GeometryShaderHandle_t hGeometryShader ) { boundGS_=hGeometryShader; }
void CShaderAPIDX12::BindPixelShader(PixelShaderHandle_t shader) { boundPS_=shader;namedPixelShaderDirty_=false;boundPixelShaderIsNamed_=false; }
void CShaderAPIDX12::SetRasterState(const ShaderRasterState_t &state) { rasterState_=state;rasterOverride_=true;cullMode_=state.m_CullMode; }
void CShaderAPIDX12::ChangeVideoMode( const ShaderDeviceInfo_t &info )
{
    if (device_ && !device_->ChangeMode(info)) Warning("ShaderAPIDX12: video mode change failed\n");
}
StateSnapshot_t CShaderAPIDX12::TakeSnapshot( ) {
 if(snapshots_.Count()>=32767)return static_cast<StateSnapshot_t>(-1);
 Snapshot snapshot=shadowState_;
 if(g_pShaderShadowDX12){const auto &shadow=*g_pShaderShadowDX12;
  snapshot.translucent=shadow.Blending();snapshot.alphaTest=shadow.AlphaTest();snapshot.depthWrite=shadow.DepthWrites();snapshot.depthTest=shadow.DepthTest();snapshot.colorWrites=shadow.ColorWrites();snapshot.alphaWrites=shadow.AlphaWrites();snapshot.culling=shadow.CullEnabled();snapshot.stencil=shadow.StencilEnabled();snapshot.alphaToCoverage=shadow.AlphaToCoverage();snapshot.fogMode=shadow.FogModeValue();snapshot.fogGammaDisabled=shadow.FogGammaDisabled();snapshot.srgbWrite=shadow.SRGBWrite();snapshot.srgbReadMask=shadow.SRGBReadMask();snapshot.alphaReference=shadow.AlphaReference();snapshot.alphaFunction=shadow.AlphaFunction();
  snapshot.blendSource=shadow.BlendSource();snapshot.blendDestination=shadow.BlendDestination();snapshot.separateAlpha=shadow.SeparateAlphaBlending();snapshot.blendAlphaSource=shadow.BlendAlphaSource();snapshot.blendAlphaDestination=shadow.BlendAlphaDestination();snapshot.blendOperation=shadow.BlendOperation();snapshot.blendAlphaOperation=shadow.BlendAlphaOperation();
  snapshot.stencilFunction=shadow.StencilFunction();snapshot.stencilFail=shadow.StencilFailOperation();snapshot.stencilDepthFail=shadow.StencilDepthFailOperation();snapshot.stencilPass=shadow.StencilPassOperation();snapshot.stencilReadMask=shadow.StencilTestMask();snapshot.stencilWriteMask=shadow.StencilWriteMask();snapshot.stencilReference=shadow.StencilReferenceValue();
  snapshot.depthFunction=shadow.DepthFunction();snapshot.vertexShaderName=InternShaderNameDX12(shadow.VertexShaderName().c_str());snapshot.pixelShaderName=InternShaderNameDX12(shadow.PixelShaderName().c_str());snapshot.staticVertexIndex=shadow.StaticVertexIndex();snapshot.staticPixelIndex=shadow.StaticPixelIndex();snapshot.shaders=!snapshot.vertexShaderName.empty()&&!snapshot.pixelShaderName.empty();snapshot.format=shadow.VertexFormat();snapshot.usage=snapshot.format;snapshot.morph=shadow.MorphFormat();snapshot.comparisonSamplerMask=shadow.ComparisonSamplerMask();
  snapshot.polygonOffset=shadow.PolyOffset();snapshot.polyFront=shadow.PolyModeFront();snapshot.polyBack=shadow.PolyModeBack();
  auto &ff=snapshot.fixed;ff.drawFlags=shadow.DrawFlagsValue();ff.customPipe=shadow.CustomPixelPipe();ff.lighting=shadow.Lighting();ff.specular=shadow.Specular();ff.vertexBlend=shadow.VertexBlend();ff.constantColor=shadow.ConstantColor();ff.alphaPipe=shadow.AlphaPipe();ff.constantAlpha=shadow.ConstantAlpha();ff.vertexAlpha=shadow.VertexAlpha();ff.materialSource=shadow.DiffuseMaterialSource();ff.fogMode=shadow.FogModeValue();ff.texCoordCount=shadow.CustomTextureStageCount();
  ff.alphaTest=snapshot.alphaTest||(snapshot.translucent&&snapshot.blendSource==SHADER_BLEND_SRC_ALPHA&&snapshot.blendDestination==SHADER_BLEND_ONE_MINUS_SRC_ALPHA);
  ff.alphaFunction=snapshot.alphaTest?snapshot.alphaFunction:SHADER_ALPHAFUNC_GEQUAL;
  if(!snapshot.alphaTest&&ff.alphaTest)snapshot.alphaReference=1.f/255.f;
  if(snapshot.alphaToCoverage&&(snapshot.alphaTest==false||snapshot.translucent))snapshot.alphaToCoverage=false;
  for(int stage=0;stage<16;++stage){ff.textureEnabled[stage]=shadow.TextureEnabled(stage);ff.texgen[stage]=shadow.TexGenEnabled(stage);ff.textureAlpha[stage]=shadow.TextureAlphaEnabled(stage);ff.texgenParam[stage]=shadow.TexGenParam(stage);ff.overbright[stage]=shadow.Overbright(stage);ff.colorOp[stage]=shadow.TextureOperation(stage,SHADER_TEXCHANNEL_COLOR);ff.alphaOp[stage]=shadow.TextureOperation(stage,SHADER_TEXCHANNEL_ALPHA);ff.colorArg1[stage]=shadow.TextureArgument(stage,SHADER_TEXCHANNEL_COLOR,0);ff.colorArg2[stage]=shadow.TextureArgument(stage,SHADER_TEXCHANNEL_COLOR,1);ff.alphaArg1[stage]=shadow.TextureArgument(stage,SHADER_TEXCHANNEL_ALPHA,0);ff.alphaArg2[stage]=shadow.TextureArgument(stage,SHADER_TEXCHANNEL_ALPHA,1);}
  if(snapshot.vertexShaderName.empty()){
   VertexFormat_t format=VERTEX_POSITION;const unsigned flags=ff.drawFlags;
   if((flags&SHADER_DRAW_NORMAL)||ff.lighting||ff.specular)format|=VERTEX_NORMAL;
   if((flags&SHADER_DRAW_COLOR)||ff.lighting)format|=VERTEX_COLOR;
   if(flags&SHADER_DRAW_SPECULAR)format|=VERTEX_SPECULAR;
   if(flags&SHADER_TEXCOORD_MASK)format|=VERTEX_TEXCOORD_SIZE(0,2);
   if(flags&SHADER_LIGHTMAP_TEXCOORD_MASK)format|=VERTEX_TEXCOORD_SIZE(1,2);
   if(flags&SHADER_SECONDARY_TEXCOORD_MASK)format|=VERTEX_TEXCOORD_SIZE(2,2);
   if(ff.vertexBlend)format|=VERTEX_BONEWEIGHT(2)|VERTEX_BONE_INDEX;
   snapshot.format=format;snapshot.usage=format;
  }
  ff.format=snapshot.format;
 }
 if(!snapshot.vertexShaderName.empty())namedShaderReferences_[namedShaderReferences_.Insert(NamedShaderKeyView{snapshot.vertexShaderName.c_str(),snapshot.staticVertexIndex,-1,false},true)]=true;
 if(!snapshot.pixelShaderName.empty())namedShaderReferences_[namedShaderReferences_.Insert(NamedShaderKeyView{snapshot.pixelShaderName.c_str(),snapshot.staticPixelIndex,-1,true},true)]=true;
 snapshots_.AddToTail(snapshot);return static_cast<StateSnapshot_t>(snapshots_.Count()-1);
}
void CShaderAPIDX12::Bind( IMaterial* pMaterial )
{
 if(boundMaterial_==pMaterial)return;
 if(boundMaterial_&&pMaterial&&boundMaterial_->InMaterialPage()&&pMaterial->InMaterialPage()&&boundMaterial_->GetMaterialPage()==pMaterial->GetMaterialPage())return;
 FlushBufferedPrimitives();boundMaterial_=pMaterial;
}
void CShaderAPIDX12::FlushBufferedPrimitives() { if(shaderUtil_)shaderUtil_->OnFlushBufferedPrimitives(); }
IMesh* CShaderAPIDX12::GetDynamicMesh(IMaterial *material,int skinBoneCount,bool buffered,IMesh *vertexOverride,IMesh *indexOverride)
{
 return GetDynamicMeshEx(material,0,skinBoneCount,buffered,vertexOverride,indexOverride);
}
IMesh* CShaderAPIDX12::GetDynamicMeshEx(IMaterial *material,VertexFormat_t requestedFormat,int skinBoneCount,bool buffered,IMesh *vertexOverride,IMesh *indexOverride)
{
 if(material)Bind(material);
 IMaterial *effectiveMaterial=material?material:boundMaterial_;
 if(skinBoneCount<0||skinBoneCount>4){Warning("ShaderAPIDX12: unsupported dynamic mesh skin bone count %d\n",skinBoneCount);return nullptr;}
 VertexFormat_t format=vertexOverride?vertexOverride->GetVertexFormat():(requestedFormat?requestedFormat:(effectiveMaterial?effectiveMaterial->GetVertexFormat()&~VERTEX_FORMAT_COMPRESSED:0));
 if(!vertexOverride){
  skinBoneCount=std::max(skinBoneCount,NumBoneWeights(format));
  format&=~VERTEX_BONE_WEIGHT_MASK;
  if(skinBoneCount>0)format|=VERTEX_BONEWEIGHT(2)|VERTEX_BONE_INDEX;
 }
 if(!VertexFormatSizeDX12(format)){Warning("ShaderAPIDX12: dynamic mesh requires a valid material or explicit vertex format\n");return nullptr;}
 boneCount_=skinBoneCount; motionBoneRows_=std::max(motionBoneRows_,std::max(1,boneCount_));
 (void)buffered;
 auto *vertexSource=vertexOverride?static_cast<CMeshDX12 *>(vertexOverride)->VertexSourceMesh():nullptr;
 auto *indexSource=indexOverride?static_cast<CMeshDX12 *>(indexOverride)->IndexSourceMesh():nullptr;
 CMeshDX12 *selected=nullptr;
 for(auto *candidate:dynamicMeshes_){
  if(candidate==vertexOverride||candidate==indexOverride||candidate==renderMesh_||candidate==vertexSource||candidate==indexSource||candidate->Vertices().GetVertexFormat()!=format)continue;
  if(renderMesh_&&renderMesh_->DependsOn(candidate))continue;
  selected=candidate;break;
 }
 if(!selected){
  selected=new CMeshDX12(format,65536,true,[](void *context,CMeshDX12 *draw,int first,int count){static_cast<CShaderAPIDX12 *>(context)->DrawMaterialMesh(draw,first,count);},this);
  dynamicMeshes_.AddToTail(selected);
 }
 if(!selected->OverrideBuffers(vertexSource,indexSource)){Warning("ShaderAPIDX12: cyclic dynamic mesh override\n");return nullptr;}
 dynamicMesh_=selected;return dynamicMesh_;
}
bool CShaderAPIDX12::IsTranslucent( StateSnapshot_t id ) const { return id>=0 && id<(StateSnapshot_t)snapshots_.Count() ? snapshots_[id].translucent : false; }
bool CShaderAPIDX12::IsAlphaTested( StateSnapshot_t id ) const { return id>=0 && id<(StateSnapshot_t)snapshots_.Count() ? snapshots_[id].alphaTest : false; }
bool CShaderAPIDX12::UsesVertexAndPixelShaders( StateSnapshot_t id ) const { return id>=0 && id<(StateSnapshot_t)snapshots_.Count() ? snapshots_[id].shaders : false; }
bool CShaderAPIDX12::IsDepthWriteEnabled( StateSnapshot_t id ) const { return id>=0 && id<(StateSnapshot_t)snapshots_.Count() ? snapshots_[id].depthWrite : true; }
VertexFormat_t CShaderAPIDX12::ComputeVertexFormat(int count,StateSnapshot_t *ids) const {return ComputeVertexUsage(count,ids);}
VertexFormat_t CShaderAPIDX12::ComputeVertexUsage(int count,StateSnapshot_t *ids) const
{
    if(count<=0||!ids)return 0;
    VertexFormat_t flags=0;int boneWeights=0,userData=0,coordinates[VERTEX_MAX_TEXTURE_COORDINATES]={};bool compressed=false,uncompressed=false;
    for(int i=0;i<count;++i)
    {
        if(ids[i]<0||static_cast<size_t>(ids[i])>=snapshots_.Count())continue;
        VertexFormat_t format=snapshots_[ids[i]].usage;
        flags|=VertexFlags(format);boneWeights=std::max(boneWeights,NumBoneWeights(format));userData=std::max(userData,UserDataSize(format));
        for(int j=0;j<VERTEX_MAX_TEXTURE_COORDINATES;++j)coordinates[j]=std::max(coordinates[j],TexCoordSize(j,format));
        compressed|=(format&VERTEX_FORMAT_COMPRESSED)!=0;uncompressed|=(format&VERTEX_FORMAT_COMPRESSED)==0;
    }
    if(compressed&&uncompressed)flags&=~VERTEX_FORMAT_COMPRESSED;
    VertexFormat_t result=flags|VERTEX_BONEWEIGHT(boneWeights)|VERTEX_USERDATA_SIZE(userData);
    for(int j=0;j<VERTEX_MAX_TEXTURE_COORDINATES;++j)result|=VERTEX_TEXCOORD_SIZE(j,coordinates[j]);
    return result;
}
void CShaderAPIDX12::BeginPass(StateSnapshot_t snapshot) {
    ZoneNamedN(beginPass, "DX12 BeginPass", DX12_DRAW_ZONES_ACTIVE);
    if(snapshot<0||snapshot>=static_cast<StateSnapshot_t>(snapshots_.Count()))return;
    if(activeSnapshotId_!=snapshot){
        const auto &next=snapshots_[snapshot];
        fogDirty_|=activeSnapshot_.fogMode!=next.fogMode||activeSnapshot_.fogGammaDisabled!=next.fogGammaDisabled;
        namedVertexShaderDirty_|=activeSnapshot_.vertexShaderName!=next.vertexShaderName||activeSnapshot_.staticVertexIndex!=next.staticVertexIndex;
        namedPixelShaderDirty_|=activeSnapshot_.pixelShaderName!=next.pixelShaderName||activeSnapshot_.staticPixelIndex!=next.staticPixelIndex;
        activeSnapshot_=next;activeSnapshotId_=snapshot;
    }
    if(activeSnapshot_.vertexShaderName.empty()){boundVS_=VERTEX_SHADER_HANDLE_INVALID;boundVertexShaderIsNamed_=namedVertexShaderDirty_=false;}
    else namedVertexShaderDirty_|=!boundVertexShaderIsNamed_;
    if(activeSnapshot_.pixelShaderName.empty()){boundPS_=PIXEL_SHADER_HANDLE_INVALID;boundPixelShaderIsNamed_=namedPixelShaderDirty_=false;}
    else namedPixelShaderDirty_|=!boundPixelShaderIsNamed_;
}
void CShaderAPIDX12::RenderPass( int nPass, int nPassCount ) {
 if(!renderMesh_||nPass<0||nPass>=nPassCount)return;
 DrawMesh(renderMesh_,renderFirstIndex_,renderIndexCount_);
}
void CShaderAPIDX12::SetNumBoneWeights(int numBones) { boneCount_=std::clamp(numBones,0,NUM_MODEL_TRANSFORMS); motionBoneRows_=std::max(1,boneCount_); }
void CShaderAPIDX12::SetLight(int number,const LightDesc_t &light) { if(number<0||number>=static_cast<int>(lights_.size()))return;lights_[number]=light;lightingDirty_=true; }
void CShaderAPIDX12::SetLightingOrigin(Vector origin) { lightingOrigin_=origin; }
void CShaderAPIDX12::SetAmbientLight(float r,float g,float b) { if(ambientLight_.x!=r||ambientLight_.y!=g||ambientLight_.z!=b){ambientLight_.Init(r,g,b);++fixedVSVersion_;} }
void CShaderAPIDX12::SetAmbientLightCube(Vector4D cube[6]) { if(!cube)return;bool changed=false;for(int i=0;i<6;++i)if(std::memcmp(cube[i].Base(),ambientCube_[i].data(),4*sizeof(float))){std::copy_n(cube[i].Base(),4,ambientCube_[i].begin());changed=true;}if(changed)++fixedVSVersion_; }
void CShaderAPIDX12::ShadeMode(ShaderShadeMode_t mode) { shadeMode_=mode; }
void CShaderAPIDX12::CullMode(MaterialCullMode_t mode) { cullMode_=mode; }
void CShaderAPIDX12::ForceDepthFuncEquals(bool enable) { forceDepthEquals_=enable; }
void CShaderAPIDX12::OverrideDepthEnable(bool enable,bool depthEnable) { overrideDepthEnable_=enable;overrideDepthValue_=depthEnable; }
void CShaderAPIDX12::SetHeightClipZ( float z ) { heightClipZ_=z; }
void CShaderAPIDX12::SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode ) { heightClipMode_=heightClipMode; }
void CShaderAPIDX12::SetClipPlane( int index, const float *pPlane ) { if(index<0||index>=static_cast<int>(worldClipPlanes_.size())||!pPlane)return;worldClipPlanes_[index]={pPlane[0],pPlane[1],pPlane[2],-pPlane[3]}; }
void CShaderAPIDX12::EnableClipPlane( int index, bool bEnable ) { if(index<0||index>=static_cast<int>(worldClipPlanes_.size()))return;const uint32_t bit=1u<<index;if(bEnable)clipPlaneMask_|=bit;else clipPlaneMask_&=~bit; }
void CShaderAPIDX12::SetSkinningMatrices() { maxBoneLoaded_=std::max(maxBoneLoaded_,std::max(0,boneCount_-1)); motionBoneRows_=std::max(motionBoneRows_,std::max(1,boneCount_)); transformsDirty_=true;CommitTransforms(); }
void CShaderAPIDX12::FlushHardware()
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 FlushHardware", DX12_ZONES_ACTIVE);
    ++frameFlushCount_;
    if (device_ && device_->IsRecordingOwner() && device_->CommandList()) { ProcessPendingTextureDeletes();device_->Submit(false); pipeline_.Reclaim(device_->CompletedFenceValue()); }
}
void CShaderAPIDX12::BeginFrame()
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 BeginFrame", DX12_ZONES_ACTIVE);
    ProcessPendingTextureDeletes();
    ProcessShaderPrecacheRequests();
    frameDrawCount_=0;
    frameFlushCount_=0;
    frameSyncCount_=0;
    frameActive_=true;++frameCounter_;
}
void CShaderAPIDX12::EndFrame()
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 EndFrame", DX12_ZONES_ACTIVE);
    if (!frameActive_) return;
#ifdef TRACY_ENABLE
    if(TracyIsStarted) {
    TracyPlot("DX12 draws/frame", static_cast<int64_t>(frameDrawCount_));
    TracyPlot("DX12 flushes/frame", static_cast<int64_t>(frameFlushCount_));
    TracyPlot("DX12 forced syncs/frame", static_cast<int64_t>(frameSyncCount_));
    }
#endif
    // -dx12stats: per-frame averages of draw-path cache behavior over 1000 frames (diagnostic; counters are plain increments).
    {
        static const bool statsEnabled=CommandLine()&&CommandLine()->CheckParm("-dx12stats");
        if(statsEnabled&&++drawStatsFrames_>=1000){
            const auto &p=pipeline_.Stats();const double n=static_cast<double>(drawStatsFrames_);
            Msg("ShaderAPIDX12 stats/frame: draws %.1f memoHits %.1f srvTableHits %.1f srvTableCopies %.1f constHits %.1f constUploads %.1f transientConst %.1f rootCbvSets %.1f rootTableSets %.1f\n",
                drawStats_.draws/n,drawStats_.memoHits/n,p.srvTableHits/n,p.srvTableCopies/n,p.constantHits/n,p.constantUploads/n,p.transientConstants/n,p.rootCbvSets/n,p.rootTableSets/n);
            double gpuMs=0.0;uint32_t gpuFrames=0;
            if(device_&&device_->ConsumeGpuTime(gpuMs,gpuFrames))Msg("ShaderAPIDX12 GPU frame time: %.3f ms (%u frames)\n",gpuMs,gpuFrames);
            drawStats_={};pipeline_.ResetStats();drawStatsFrames_=0;
        }
    }
    ProcessPendingTextureDeletes();
    if (device_) pipeline_.Reclaim(device_->CompletedFenceValue());
    frameActive_ = false;
}
int CShaderAPIDX12::SelectionMode(bool enabled) { return selection_.SetMode(enabled); }
void CShaderAPIDX12::SelectionBuffer(unsigned int *buffer,int words) { selection_.SetBuffer(buffer,words); }
void CShaderAPIDX12::ClearSelectionNames() { selection_.ClearNames(); }
void CShaderAPIDX12::LoadSelectionName(int name) { selection_.LoadName(static_cast<unsigned int>(name)); }
void CShaderAPIDX12::PushSelectionName(int name) { selection_.PushName(static_cast<unsigned int>(name)); }
void CShaderAPIDX12::PopSelectionName() { selection_.PopName(); }
void CShaderAPIDX12::ForceHardwareSync() { ZoneNamedN(___tracy_scoped_zone, "DX12 ForceHardwareSync", DX12_ZONES_ACTIVE); ++frameSyncCount_; if(device_ && device_->IsRecordingOwner() && device_->CommandList()) {ProcessPendingTextureDeletes();device_->SubmitFrameSync();pipeline_.Reclaim(device_->CompletedFenceValue());} }
void CShaderAPIDX12::ClearSnapshots() { for(auto entry=fixedShaders_.FirstInorder();entry!=fixedShaders_.InvalidIndex();entry=fixedShaders_.NextInorder(entry)){RetireShaderPipelines(fixedShaders_[entry]);delete fixedShaders_[entry];}fixedShaders_.RemoveAll();snapshots_.RemoveAll();activeSnapshot_=Snapshot{};activeSnapshotId_=static_cast<StateSnapshot_t>(-1);fogDirty_=true;pixelFogRegister_=-1;++namedResolveEpoch_;++pipelineMemoEpoch_; }
void CShaderAPIDX12::FogStart(float start) { if(fogStart_!=start){fogStart_=start;fogDirty_=true;} }
void CShaderAPIDX12::FogEnd(float end) { if(fogEnd_!=end){fogEnd_=end;fogDirty_=true;} }
void CShaderAPIDX12::SetFogZ(float height) { if(fogZ_!=height){fogZ_=height;fogDirty_=true;} }
void CShaderAPIDX12::SceneFogColor3ub(unsigned char r,unsigned char g,unsigned char b) { if(fogColor_[0]!=r||fogColor_[1]!=g||fogColor_[2]!=b){fogColor_[0]=r;fogColor_[1]=g;fogColor_[2]=b;fogDirty_=true;} }
void CShaderAPIDX12::SceneFogMode(MaterialFogMode_t mode) { if(fogMode_!=mode){fogMode_=mode;fogDirty_=true;} }
bool CShaderAPIDX12::CanDownloadTextures() const { return true; }
void CShaderAPIDX12::ResetRenderState( bool bFullReset) { if(bFullReset) ResetNativeState(); }
int CShaderAPIDX12::GetCurrentDynamicVBSize( void ) { return DYNAMIC_VERTEX_BUFFER_MEMORY; }
void CShaderAPIDX12::DestroyVertexBuffers(bool) { boundVertexBuffers_={};boundIndexBuffer_=nullptr;boundIndexOffset_=0; }
void CShaderAPIDX12::SetAnisotropicLevel( int nAnisotropyLevel ) { const int level=std::max(1,std::min(16,nAnisotropyLevel)); if(level!=anisotropy_){anisotropy_=level;for(auto &slot:preparedTextureSlots_)slot.valid=false;++textureStateEpoch_;textureSetValid_=false;} }
void CShaderAPIDX12::SyncToken( const char *pToken ) { lastToken_=pToken?pToken:""; }
void CShaderAPIDX12::SetStandardVertexShaderConstants(float overbright)
{
 const float math[4]={0.f,1.f,2.f,.5f};
 const float gamma[4]={1.f/GAMMA,overbright,1.f/3.f,1.f/overbright};
 const float flex[4]={0.f,0.f,0.f,0.f};
 SetVertexShaderConstant(VERTEX_SHADER_MATH_CONSTANTS0,math,1);
 SetVertexShaderConstant(VERTEX_SHADER_MATH_CONSTANTS1,gamma,1);
 SetVertexShaderConstant(VERTEX_SHADER_FLEXSCALE,flex,1);
}
ShaderAPIOcclusionQuery_t CShaderAPIDX12::CreateOcclusionQueryObject()
{
    const uint64_t completed=device_?device_->CompletedFenceValue():0;
    for(int i=0;i<occlusionQueries_.Count();){
        auto *query=occlusionQueries_[i];
        if(query->destroyed&&(!query->ended||completed>=query->fence)){delete query;occlusionQueries_.Remove(i);}
        else ++i;
    }
    auto query=CreateOcclusionQuery();if(!query)return INVALID_SHADERAPI_OCCLUSION_QUERY_HANDLE;
    OcclusionQueryDX12 *raw=query.get();occlusionQueries_.AddToTail(query.release());return reinterpret_cast<ShaderAPIOcclusionQuery_t>(raw);
}
void CShaderAPIDX12::DestroyOcclusionQueryObject(ShaderAPIOcclusionQuery_t handle)
{
    auto *query=reinterpret_cast<OcclusionQueryDX12 *>(handle);if(!query)return;for(auto *entry:occlusionQueries_)if(entry==query){query->destroyed=true;if(query->active)query->error=true;return;}
}
void CShaderAPIDX12::BeginOcclusionQueryDrawing(ShaderAPIOcclusionQuery_t handle)
{
    auto *query=reinterpret_cast<OcclusionQueryDX12 *>(handle);if(!query||query->destroyed||!device_||!device_->CommandList()||query->active){if(query)query->error=true;return;}device_->CommandList()->BeginQuery(query->heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);query->active=true;query->ended=false;query->error=false;
}
void CShaderAPIDX12::EndOcclusionQueryDrawing(ShaderAPIOcclusionQuery_t handle)
{
    auto *query=reinterpret_cast<OcclusionQueryDX12 *>(handle);if(!query||!query->active||!device_||!device_->CommandList()){if(query)query->error=true;return;}auto *list=device_->CommandList();list->EndQuery(query->heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);list->ResolveQueryData(query->heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,0,1,query->readback.Get(),0);query->active=false;query->ended=true;query->fence=device_->NextFenceValue();device_->RetainResource(query->readback.Get());
}
int CShaderAPIDX12::OcclusionQuery_GetNumPixelsRendered(ShaderAPIOcclusionQuery_t handle,bool flush)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 OcclusionQueryResult", DX12_ZONES_ACTIVE);
    auto *query=reinterpret_cast<OcclusionQueryDX12 *>(handle);if(!query||query->destroyed||query->error)return OCCLUSION_QUERY_RESULT_ERROR;if(query->active||!query->ended)return OCCLUSION_QUERY_RESULT_PENDING;if(device_->CompletedFenceValue()<query->fence){if(flush){TracyPlot("DX12 occlusion flush",static_cast<int64_t>(1));if(device_->IsRecordingOwner() && device_->CommandList()){ProcessPendingTextureDeletes();device_->Submit(true);pipeline_.Reclaim(device_->CompletedFenceValue());}}if(device_->CompletedFenceValue()<query->fence)return OCCLUSION_QUERY_RESULT_PENDING;}uint64_t value=0;void *mapped=nullptr;D3D12_RANGE range{0,sizeof(value)};if(FAILED(query->readback->Map(0,&range,&mapped))||!mapped){query->error=true;return OCCLUSION_QUERY_RESULT_ERROR;}std::memcpy(&value,mapped,sizeof(value));query->readback->Unmap(0,nullptr);return value>INT_MAX?INT_MAX:static_cast<int>(value);
}
void CShaderAPIDX12::SetFlashlightState( const FlashlightState_t &state, const VMatrix &worldToTexture ) { flashlight_=state; flashlightMatrix_=worldToTexture; flashlightMode_=state.m_bEnableShadows || state.m_pSpotlightTexture!=nullptr; }
void CShaderAPIDX12::ClearVertexAndPixelShaderRefCounts() {
    FOR_EACH_HASHTABLE(namedShaderReferences_,entry)namedShaderReferences_[entry]=false;
    ++namedReferenceEpoch_;
    // The next use must re-mark cached named bindings before an unused-shader purge.
    namedVertexShaderDirty_|=boundVertexShaderIsNamed_&&!activeSnapshot_.vertexShaderName.empty();
    namedPixelShaderDirty_|=boundPixelShaderIsNamed_&&!activeSnapshot_.pixelShaderName.empty();
}
void CShaderAPIDX12::PurgeUnusedVertexAndPixelShaders()
{
 ++namedResolveEpoch_;
 for(auto it=namedShaderCombos_.FirstHandle();it!=namedShaderCombos_.InvalidHandle();){
  const auto &key=namedShaderCombos_.Key(it);
  const auto reference=namedShaderReferences_.Find(NamedShaderKeyView{key.name.String(),key.staticIndex,-1,key.pixel});
  if(reference!=namedShaderReferences_.InvalidHandle()&&namedShaderReferences_[reference]){it=namedShaderCombos_.NextHandle(it);continue;}
  auto *record=namedShaderCombos_[it];const bool vertexBound=record&&reinterpret_cast<ShaderRecordDX12 *>(boundVS_)==record;
  const bool pixelBound=record&&reinterpret_cast<ShaderRecordDX12 *>(boundPS_)==record;
  RetireShaderPipelines(record);delete record;it=namedShaderCombos_.RemoveAndAdvance(it);
  if(vertexBound)namedVertexShaderDirty_=!activeSnapshot_.vertexShaderName.empty();
  if(pixelBound)namedPixelShaderDirty_=!activeSnapshot_.pixelShaderName.empty();
 }
 for(auto it=namedShaderFiles_.FirstHandle();it!=namedShaderFiles_.InvalidHandle();){
  bool referenced=false;const char *fileKey=namedShaderFiles_.Key(it).String();
  FOR_EACH_HASHTABLE(namedShaderReferences_,entry){
   const auto &key=namedShaderReferences_.Key(entry);
   if(namedShaderReferences_[entry]&&key.pixel==(fileKey[0]=='p')&&!std::strcmp(fileKey+2,key.name.String())){referenced=true;break;}
  }
  if(referenced)it=namedShaderFiles_.NextHandle(it);
  else{delete namedShaderFiles_[it];it=namedShaderFiles_.RemoveAndAdvance(it);}
 }
 for(auto it=namedShaderReferences_.FirstHandle();it!=namedShaderReferences_.InvalidHandle();)
  if(!namedShaderReferences_[it])it=namedShaderReferences_.RemoveAndAdvance(it);else it=namedShaderReferences_.NextHandle(it);
}
void CShaderAPIDX12::DXSupportLevelChanged() { ClearSnapshots(); }
void CShaderAPIDX12::EnableUserClipTransformOverride( bool bEnable ) { userClipViewOverride_=bEnable; }
void CShaderAPIDX12::UserClipTransform( const VMatrix &worldToView ) { userClipView_=worldToView; }
MorphFormat_t CShaderAPIDX12::ComputeMorphFormat(int count,StateSnapshot_t *ids) const { MorphFormat_t format=0;if(ids)for(int i=0;i<count;++i)if(ids[i]>=0&&static_cast<size_t>(ids[i])<snapshots_.Count())format|=snapshots_[ids[i]].morph;return format; }
void CShaderAPIDX12::HandleDeviceLost() { if(device_ && device_->NativeDevice()) Warning("ShaderAPIDX12: device removal reason 0x%08x\n",static_cast<unsigned>(device_->NativeDevice()->GetDeviceRemovedReason())); }
void CShaderAPIDX12::EnableLinearColorSpaceFrameBuffer( bool bEnable ) { if(linearColorSpaceFramebuffer_!=bEnable){FlushBufferedPrimitives();linearColorSpaceFramebuffer_=bEnable;} }
void CShaderAPIDX12::SetFullScreenTextureHandle( ShaderAPITextureHandle_t h ) { fullScreenTexture_=h; }
void CShaderAPIDX12::SetIntRenderingParameter(int parm_number, int value) { if(parm_number==INT_RENDERPARM_DX12_MOTION_STATUS)return; if(parm_number>=0 && parm_number<(int)renderingInts_.size()) renderingInts_[parm_number]=value; if(parm_number==INT_RENDERPARM_DX12_MOTION_PASS)SetMotionPass(value);else if(parm_number==INT_RENDERPARM_DX12_MOTION_OBJECT)motionObjectKey_=value; }
void CShaderAPIDX12::SetVectorRenderingParameter(int parm_number, Vector const &value) { if(parm_number>=0 && parm_number<(int)renderingVectors_.size()) renderingVectors_[parm_number]=value; }
float CShaderAPIDX12::GetFloatRenderingParameter(int parm_number) const { return parm_number>=0 && parm_number<(int)renderingFloats_.size()?renderingFloats_[parm_number]:0.0f; }
int CShaderAPIDX12::GetIntRenderingParameter(int parm_number) const { return parm_number>=0 && parm_number<(int)renderingInts_.size()?renderingInts_[parm_number]:0; }
Vector CShaderAPIDX12::GetVectorRenderingParameter(int parm_number) const { return parm_number>=0 && parm_number<(int)renderingVectors_.size()?renderingVectors_[parm_number]:Vector(0,0,0); }
void CShaderAPIDX12::SetFastClipPlane( const float *pPlane ) { if(pPlane)fastClipPlane_={pPlane[0],pPlane[1],pPlane[2],-pPlane[3]}; }
void CShaderAPIDX12::EnableFastClip( bool bEnable ) { fastClipEnabled_=bEnable; }
void CShaderAPIDX12::GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices ) { if(pMaxVerts)*pMaxVerts=65536; if(pMaxIndices)*pMaxIndices=INDEX_BUFFER_SIZE; }
int CShaderAPIDX12::GetMaxVerticesToRender( IMaterial *pMaterial ) { return 65536; }
int CShaderAPIDX12::GetMaxIndicesToRender( ) { return INDEX_BUFFER_SIZE; }
void CShaderAPIDX12::DisableAllLocalLights() { for(auto &light:lights_)light.m_Type=MATERIAL_LIGHT_DISABLE;lightingDirty_=true; }
int CShaderAPIDX12::CompareSnapshots( StateSnapshot_t snapshot0, StateSnapshot_t snapshot1 ) { if(snapshot0==snapshot1)return 0; if(snapshot0<0||snapshot1<0||snapshot0>=(StateSnapshot_t)snapshots_.Count()||snapshot1>=(StateSnapshot_t)snapshots_.Count())return snapshot0<snapshot1?-1:1; const Snapshot&a=snapshots_[snapshot0],&b=snapshots_[snapshot1]; if(a.alphaTest!=b.alphaTest)return a.alphaTest?1:-1; if(a.translucent!=b.translucent)return a.translucent?1:-1; return snapshot0<snapshot1?-1:1; }
IMesh *CShaderAPIDX12::GetFlexMesh() { return dynamicMesh_ ? dynamicMesh_ : (dynamicMesh_=new CMeshDX12(0,65536,true,[](void *context,CMeshDX12*m,int f,int n){static_cast<CShaderAPIDX12 *>(context)->DrawMaterialMesh(m,f,n);},this)); }
void CShaderAPIDX12::SetFlashlightStateEx( const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pFlashlightDepthTexture ) { flashlight_=state; flashlightMatrix_=worldToTexture; flashlight_.m_pSpotlightTexture=pFlashlightDepthTexture; flashlightMode_=state.m_bEnableShadows||state.m_pSpotlightTexture!=nullptr||pFlashlightDepthTexture!=nullptr; }
bool CShaderAPIDX12::SupportsMSAAMode( int nMSAAMode ) { return device_ && device_->SupportsMSAA(nMSAAMode); }
bool CShaderAPIDX12::OwnGPUResources( bool bEnable )
{
    if (!device_ || !device_->IsRecordingOwner()) return false;
    if (bEnable) device_->ReacquireResources(); else device_->ReleaseResources();
    return device_->IsUsingGraphics();
}
void CShaderAPIDX12::GetFogDistances( float *fStart, float *fEnd, float *fFogZ ) { if(fStart)*fStart=fogStart_;if(fEnd)*fEnd=fogEnd_;if(fFogZ)*fFogZ=fogZ_; }
void CShaderAPIDX12::BeginPIXEvent( unsigned long color, const char *szName ) {}
void CShaderAPIDX12::EndPIXEvent() {}
void CShaderAPIDX12::SetPIXMarker( unsigned long color, const char *szName ) {}
void CShaderAPIDX12::EnableAlphaToCoverage() { alphaToCoverage_=true; }
void CShaderAPIDX12::DisableAlphaToCoverage() { alphaToCoverage_=false; }
void CShaderAPIDX12::ComputeVertexDescription( unsigned char* pBuffer, VertexFormat_t vertexFormat, MeshDesc_t& desc ) const { ComputeVertexLayoutDX12(vertexFormat,pBuffer,&desc); }
bool CShaderAPIDX12::SupportsShadowDepthTextures( void ) { return true; }
void CShaderAPIDX12::SetDisallowAccess( bool bDisallow ) { disallowAccess_=bDisallow; }
void CShaderAPIDX12::EnableShaderShaderMutex( bool bEnable ) { mutexEnabled_=bEnable; }
void CShaderAPIDX12::ShaderLock() { if(mutexEnabled_) shaderMutex_.Lock(); }
void CShaderAPIDX12::ShaderUnlock() { if(mutexEnabled_) shaderMutex_.Unlock(); }
ImageFormat CShaderAPIDX12::GetShadowDepthTextureFormat( void ) { return IMAGE_FORMAT_NV_INTZ; }
bool CShaderAPIDX12::SupportsFetch4( void ) { return false; }
void CShaderAPIDX12::SetShadowDepthBiasFactors( float fShadowSlopeScaleDepthBias, float fShadowDepthBias ) { fastFloatParams_[0]=fShadowSlopeScaleDepthBias;fastFloatParams_[1]=fShadowDepthBias; }
void CShaderAPIDX12::BindVertexBuffer(int stream,IVertexBuffer *buffer,int offset,int firstVertex,int vertexCount,VertexFormat_t format,int repetitions)
{
 if(stream<0||stream>=static_cast<int>(boundVertexBuffers_.size()))return;
 auto &binding=boundVertexBuffers_[stream];binding={};
 if(!buffer||offset<0||firstVertex<0||vertexCount<=0||repetitions<=0)return;
 binding={static_cast<CVertexBufferDX12 *>(buffer),static_cast<uint32_t>(offset),static_cast<uint32_t>(firstVertex),static_cast<uint32_t>(vertexCount),static_cast<uint32_t>(repetitions),format?format:buffer->GetVertexFormat()};
}
void CShaderAPIDX12::BindIndexBuffer(IIndexBuffer *buffer,int offset)
{
 boundIndexBuffer_=offset>=0?static_cast<CIndexBufferDX12 *>(buffer):nullptr;boundIndexOffset_=offset>=0?static_cast<size_t>(offset):0;
}
void CShaderAPIDX12::Draw(MaterialPrimitiveType_t primitive,int firstIndex,int indexCount)
{
 DrawBuffers(boundVertexBuffers_,boundIndexBuffer_,boundIndexOffset_,primitive,firstIndex,indexCount);
}
void CShaderAPIDX12::PerformFullScreenStencilOperation() { FlushBufferedPrimitives();DrawMaskedClear(false,false,false,nullptr,true); }
void CShaderAPIDX12::SetScissorRect( const int nLeft, const int nTop, const int nRight, const int nBottom, const bool bEnableScissor ) { fastIntParams_[0]=nLeft;fastIntParams_[1]=nTop;fastIntParams_[2]=nRight;fastIntParams_[3]=nBottom;fastIntParams_[4]=bEnableScissor?1:0; }
bool CShaderAPIDX12::SupportsCSAAMode( int nNumSamples, int nQualityLevel ) { (void)nNumSamples;(void)nQualityLevel;return false; }
void CShaderAPIDX12::InvalidateDelayedShaderConstants() { pixelFogRegister_=-1; }
float CShaderAPIDX12::GammaToLinear_HardwareSpecific(float gamma) const { return gamma<=0.04045f?gamma/12.92f:std::pow((gamma+0.055f)/1.055f,2.4f); }
float CShaderAPIDX12::LinearToGamma_HardwareSpecific(float linear) const { return linear<=0.0031308f?linear*12.92f:1.055f*std::pow(linear,1.f/2.4f)-0.055f; }
void CShaderAPIDX12::SetLinearToGammaConversionTextures( ShaderAPITextureHandle_t hSRGBWriteEnabledTexture, ShaderAPITextureHandle_t hIdentityTexture ) { gammaConversionTexture_=hSRGBWriteEnabledTexture;gammaIdentityTexture_=hIdentityTexture; }
ImageFormat CShaderAPIDX12::GetNullTextureFormat( void ) { return IMAGE_FORMAT_RGBA8888; }
void CShaderAPIDX12::BindVertexTexture( VertexTextureSampler_t nSampler, ShaderAPITextureHandle_t textureHandle ) { if(nSampler>=0 && nSampler<(int)vertexTextures_.size()) vertexTextures_[nSampler]=textureHandle; }
void CShaderAPIDX12::EnableHWMorphing( bool bEnable ) { morphing_=bEnable; }
void CShaderAPIDX12::SetFlexWeights( int nFirstWeight, int nCount, const MorphWeight_t* pWeights ) { (void)nFirstWeight;(void)nCount;(void)pWeights; }
void CShaderAPIDX12::FogMaxDensity(float density) { density=std::clamp(density,0.f,1.f);if(fogMaxDensity_!=density){fogMaxDensity_=density;fogDirty_=true;} }
void CShaderAPIDX12::CreateTextures( ShaderAPITextureHandle_t *pHandles, int count, int width, int height, int depth, ImageFormat dstImageFormat, int numMipLevels, int numCopies, int flags, const char *pDebugName, const char *pTextureGroupName ) { if(!pHandles)return;for(int i=0;i<count;++i)pHandles[i]=CreateTexture(width,height,depth,dstImageFormat,numMipLevels,numCopies,flags,pDebugName,pTextureGroupName); }
void CShaderAPIDX12::AcquireThreadOwnership()
{
    if(device_ && !device_->AcquireRecordingOwnership()) Warning("ShaderAPIDX12: AcquireThreadOwnership failed\n");
}
void CShaderAPIDX12::ReleaseThreadOwnership()
{
    if(device_)device_->ReleaseRecordingOwnership();
}
void CShaderAPIDX12::EnableBuffer2FramesAhead( bool bEnable ) { (void)bEnable; }
void CShaderAPIDX12::PrintfVA( char *fmt, va_list vargs ) { if(fmt)vprintf(fmt,vargs); }
void CShaderAPIDX12::Printf( const char *fmt, ... ) { if(fmt){va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);} }
float CShaderAPIDX12::Knob(char *name,float *value) { if(!name)return 0.f;if(value){knobs_[knobs_.Insert(name,*value)]=*value;return *value;}const auto found=knobs_.Find(name);return found==knobs_.InvalidHandle()?0.f:knobs_[found]; }
void CShaderAPIDX12::OverrideAlphaWriteEnable( bool bEnable, bool bAlphaWriteEnable ) { alphaWriteOverride_=bEnable;alphaWriteOverrideValue_=bAlphaWriteEnable; }
void CShaderAPIDX12::OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable ) { colorWriteOverride_=bOverrideEnable;colorWriteOverrideValue_=bColorWriteEnable; }
int CShaderAPIDX12::VertexFormatSize( VertexFormat_t vertexFormat ) const { return VertexFormatSizeDX12(vertexFormat); }
void CShaderAPIDX12::SceneFogRadial( bool bRadial ) { fogRadial_=bRadial; }
bool CShaderAPIDX12::GetSceneFogRadial() { return fogRadial_; }

} // namespace shaderapidx12
