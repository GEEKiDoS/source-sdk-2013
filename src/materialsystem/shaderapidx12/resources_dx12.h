#pragma once

#include "materialsystem/imesh.h"
#include "materialsystem/shaderapidx12/vertex_layout_dx12.h"
#include "shaderapi/IShaderDevice.h"
#include <d3d12.h>
#include <wrl/client.h>
#include "tier1/utlmemory.h"
#include <cstddef>
#include <cstdint>

namespace shaderapidx12
{

template <typename T> struct ByteSpanDX12
{
    T *bytes;
    size_t count;
    T *data() const { return bytes; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
};

class CShaderBufferDX12 final : public IShaderBuffer
{
public:
    CShaderBufferDX12(const void *data, size_t size);
    size_t GetSize() const override { return byteSize_; }
    const void *GetBits() const override { return byteSize_ ? bytes_.Base() : nullptr; }
    void Release() override { delete this; }
    ByteSpanDX12<const unsigned char> Bytes() const { return {bytes_.Base(), byteSize_}; }
private:
    CUtlMemoryConservative<unsigned char> bytes_;
    size_t byteSize_ = 0;
};

class CVertexBufferDX12 : public IVertexBuffer
{
public:
    CVertexBufferDX12(VertexFormat_t format, int count, bool dynamic);
    int VertexCount() const override { return vertexCount_; }
    int WrittenCount() const { return written_; }
    VertexFormat_t GetVertexFormat() const override { return format_; }
    bool IsDynamic() const override { return dynamic_; }
    void BeginCastBuffer(VertexFormat_t format) override;
    void EndCastBuffer() override;
    int GetRoomRemaining() const override;
    bool Lock(int nVertexCount, bool bAppend, VertexDesc_t &desc) override;
    void Unlock(int nVertexCount, VertexDesc_t &desc) override;
    void Spew(int nVertexCount, const VertexDesc_t &desc) override;
    void ValidateData(int nVertexCount, const VertexDesc_t &desc) override;
    ByteSpanDX12<const unsigned char> Bytes() const { return {bytes_.Base(), byteSize_}; }
    ByteSpanDX12<unsigned char> Bytes() { MarkModified(); return {bytes_.Base(), byteSize_}; }
    ByteSpanDX12<const unsigned char> Data() const { return {bytes_.Base(), byteSize_}; }
    uint32_t Stride() const { return stride_; }
    const VertexLayoutDX12 &Layout() const { return layout_; }
    // Byte offsets of colors needing red/blue swap, derived from the fixed layout on first use.
    const uint32_t *SwapOffsets(size_t &count) const {
        if(swapLayoutStride_!=layout_.stride+1){swapCount_=0;for(uint32_t i=0;i<layout_.inputCount;++i)if(layout_.inputs[i].swapRedBlue)swapOffsets_[swapCount_++]=layout_.inputs[i].byteOffset;swapLayoutStride_=layout_.stride+1;}
        count=swapCount_;return swapOffsets_.data();
    }
    ID3D12Resource *NativeResource() const { return resource_.Get(); }
    bool EnsureCapacity(int count);
    Microsoft::WRL::ComPtr<ID3D12Resource> &NativeResourceRef() { return resource_; }
    uint64_t ContentVersion() const { return contentVersion_; }
    uint64_t NativeResourceVersion() const { return nativeResourceVersion_; }
    size_t NativeResourceBytes() const { return nativeResourceBytes_; }
    ID3D12Device *NativeDevice() const { return nativeDevice_; }
    void SetNativeResourceVersion(uint64_t version, size_t bytes, ID3D12Device *device) { nativeResourceVersion_=version; nativeResourceBytes_=bytes; nativeDevice_=device; }
    // Last transient upload of this buffer's contents: reusable while version, fence and epoch match.
    struct TransientUploadDX12 { uint64_t version=0,fence=0,epoch=0; size_t bytes=0; D3D12_GPU_VIRTUAL_ADDRESS address=0; };
    TransientUploadDX12 &TransientUpload() { return transientUpload_; }
    // The pipeline cache already holds a fence-owned reference for this resource and recording fence.
    bool IsRetainedFor(uint64_t fence, uint64_t epoch) const { return retainedFence_==fence&&retainedEpoch_==epoch&&retainedResource_==resource_.Get(); }
    void MarkRetained(uint64_t fence, uint64_t epoch) { retainedFence_=fence; retainedEpoch_=epoch; retainedResource_=resource_.Get(); retainedAddress_=resource_->GetGPUVirtualAddress(); }
    // GPU address of the retained resource; valid while IsRetainedFor holds.
    D3D12_GPU_VIRTUAL_ADDRESS RetainedAddress() const { return retainedAddress_; }
    void MarkModified();
protected:
    VertexFormat_t format_ = 0;
    VertexLayoutDX12 layout_{};
    int vertexCount_ = 0;
    int written_ = 0;
    bool dynamic_ = false;
    uint32_t stride_ = 0;
    CUtlMemoryConservative<unsigned char> bytes_;
    size_t byteSize_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    uint64_t contentVersion_ = 1, nativeResourceVersion_ = 0;
    size_t nativeResourceBytes_ = 0;
    ID3D12Device *nativeDevice_ = nullptr;
    ID3D12Resource *retainedResource_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS retainedAddress_ = 0;
    uint64_t retainedFence_ = 0, retainedEpoch_ = 0;
    TransientUploadDX12 transientUpload_{};
    mutable std::array<uint32_t,MAX_VERTEX_INPUTS_DX12> swapOffsets_{};
    mutable size_t swapCount_ = 0;
    mutable uint32_t swapLayoutStride_ = 0;
};

class CIndexBufferDX12 : public IIndexBuffer
{
public:
    CIndexBufferDX12(MaterialIndexFormat_t format, int count, bool dynamic);
    int IndexCount() const override { return indexCount_; }
    int WrittenCount() const { return written_; }
    MaterialIndexFormat_t IndexFormat() const override { return format_; }
    bool IsDynamic() const override { return dynamic_; }
    void BeginCastBuffer(MaterialIndexFormat_t format) override;
    void EndCastBuffer() override;
    int GetRoomRemaining() const override;
    bool Lock(int nMaxIndexCount, bool bAppend, IndexDesc_t &desc) override;
    void Unlock(int nWrittenIndexCount, IndexDesc_t &desc) override;
    void ModifyBegin(bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc) override;
    void ModifyEnd(IndexDesc_t &desc) override;
    void Spew(int nIndexCount, const IndexDesc_t &desc) override;
    void ValidateData(int nIndexCount, const IndexDesc_t &desc) override;
    ByteSpanDX12<const unsigned char> Bytes() const { return {bytes_.Base(), byteSize_}; }
    ByteSpanDX12<unsigned char> Bytes() { MarkModified(); return {bytes_.Base(), byteSize_}; }
    ByteSpanDX12<const unsigned char> Data() const { return {bytes_.Base(), byteSize_}; }
    uint32_t IndexSize() const { return indexSize_; }
    ID3D12Resource *NativeResource() const { return resource_.Get(); }
    bool EnsureCapacity(int count);
    Microsoft::WRL::ComPtr<ID3D12Resource> &NativeResourceRef() { return resource_; }
    uint64_t ContentVersion() const { return contentVersion_; }
    uint64_t NativeResourceVersion() const { return nativeResourceVersion_; }
    size_t NativeResourceBytes() const { return nativeResourceBytes_; }
    ID3D12Device *NativeDevice() const { return nativeDevice_; }
    void SetNativeResourceVersion(uint64_t version, size_t bytes, ID3D12Device *device) { nativeResourceVersion_=version; nativeResourceBytes_=bytes; nativeDevice_=device; }
    bool IsRetainedFor(uint64_t fence, uint64_t epoch) const { return retainedFence_==fence&&retainedEpoch_==epoch&&retainedResource_==resource_.Get(); }
    void MarkRetained(uint64_t fence, uint64_t epoch) { retainedFence_=fence; retainedEpoch_=epoch; retainedResource_=resource_.Get(); retainedAddress_=resource_->GetGPUVirtualAddress(); }
    // GPU address of the retained resource; valid while IsRetainedFor holds.
    D3D12_GPU_VIRTUAL_ADDRESS RetainedAddress() const { return retainedAddress_; }
    struct TransientUploadDX12 { uint64_t version=0,fence=0,epoch=0; size_t bytes=0; D3D12_GPU_VIRTUAL_ADDRESS address=0; };
    TransientUploadDX12 &TransientUpload() { return transientUpload_; }
    void MarkModified();
private:
    MaterialIndexFormat_t format_ = MATERIAL_INDEX_FORMAT_16BIT;
    int indexCount_ = 0;
    int written_ = 0;
    bool dynamic_ = false;
    uint32_t indexSize_ = 2;
    CUtlMemoryConservative<unsigned char> bytes_;
    size_t byteSize_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    uint64_t contentVersion_ = 1, nativeResourceVersion_ = 0;
    size_t nativeResourceBytes_ = 0;
    ID3D12Device *nativeDevice_ = nullptr;
    ID3D12Resource *retainedResource_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS retainedAddress_ = 0;
    uint64_t retainedFence_ = 0, retainedEpoch_ = 0;
    TransientUploadDX12 transientUpload_{};
    bool modifyingWritable_ = false;

};
class CMeshDX12 final : public IMesh
{
public:
    using DrawCallback = void (*)(void *, CMeshDX12 *, int, int);
    CMeshDX12(VertexFormat_t format, int vertexCount, bool dynamic, DrawCallback draw, void *drawContext);
    int VertexCount() const override { return DrawVertices().VertexCount(); }
    VertexFormat_t GetVertexFormat() const override { return DrawVertices().GetVertexFormat(); }
    bool IsDynamic() const override;
    void BeginCastBuffer(VertexFormat_t format) override { vertices_.BeginCastBuffer(format); }
    void EndCastBuffer() override;
    int GetRoomRemaining() const override { return vertices_.GetRoomRemaining(); }
    bool Lock(int nVertexCount, bool bAppend, VertexDesc_t &desc) override { return vertices_.Lock(nVertexCount,bAppend,desc); }
    void Unlock(int nVertexCount, VertexDesc_t &desc) override { vertices_.Unlock(nVertexCount,desc); }
    void Spew(int nVertexCount, const VertexDesc_t &desc) override { vertices_.Spew(nVertexCount,desc); }
    void ValidateData(int nVertexCount, const VertexDesc_t &desc) override { vertices_.ValidateData(nVertexCount,desc); }
    int IndexCount() const override { return DrawIndices().IndexCount(); }
    MaterialIndexFormat_t IndexFormat() const override { return DrawIndices().IndexFormat(); }
    bool IsDynamicIndex() const { return indices_.IsDynamic(); }
    void BeginCastBuffer(MaterialIndexFormat_t format) override { indices_.BeginCastBuffer(format); }
    int GetRoomRemainingIndex() const { return indices_.GetRoomRemaining(); }
    bool Lock(int nMaxIndexCount, bool bAppend, IndexDesc_t &desc) override { return indices_.Lock(nMaxIndexCount,bAppend,desc); }
    void Unlock(int nWrittenIndexCount, IndexDesc_t &desc) override { indices_.Unlock(nWrittenIndexCount,desc); }
    void ModifyBegin(bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc) override { indices_.ModifyBegin(bReadOnly,nFirstIndex,nIndexCount,desc); }
    void ModifyEnd(IndexDesc_t &desc) override { indices_.ModifyEnd(desc); }
    void Spew(int nIndexCount, const IndexDesc_t &desc) override { indices_.Spew(nIndexCount,desc); }
    void ValidateData(int nIndexCount, const IndexDesc_t &desc) override { indices_.ValidateData(nIndexCount,desc); }
    void SetPrimitiveType(MaterialPrimitiveType_t type) override;
    void Draw(int nFirstIndex = -1, int nIndexCount = 0) override;
    void SetColorMesh(IMesh *mesh, int offset) override;
    void Draw(CPrimList *lists, int nLists) override;
    void CopyToMeshBuilder(int,int,int,int,int,CMeshBuilder &) override;
    void Spew(int,int,const MeshDesc_t &) override;
    void ValidateData(int,int,const MeshDesc_t &) override;
    void LockMesh(int nVertexCount, int nIndexCount, MeshDesc_t &desc) override;
    void ModifyBegin(int nFirstVertex,int nVertexCount,int nFirstIndex,int nIndexCount,MeshDesc_t &desc) override;
    void ModifyEnd(MeshDesc_t &desc) override;
    void UnlockMesh(int nVertexCount,int nIndexCount,MeshDesc_t &desc) override;
    void ModifyBeginEx(bool bReadOnly,int nFirstVertex,int nVertexCount,int nFirstIndex,int nIndexCount,MeshDesc_t &desc) override;
    void SetFlexMesh(IMesh *mesh,int offset) override;
    void DisableFlexMesh() override { flexMesh_ = nullptr; flexOffset_ = 0; }
    void MarkAsDrawn() override { drawn_ = true; }
    unsigned ComputeMemoryUsed() override;
    CVertexBufferDX12 &Vertices() { return vertices_; }
    CIndexBufferDX12 &Indices() { return indices_; }
    CVertexBufferDX12 &DrawVertices() { return vertexOverride_ ? vertexOverride_->DrawVertices() : vertices_; }
    const CVertexBufferDX12 &DrawVertices() const { return vertexOverride_ ? vertexOverride_->DrawVertices() : vertices_; }
    CIndexBufferDX12 &DrawIndices() { return indexOverride_ ? indexOverride_->DrawIndices() : indices_; }
    const CIndexBufferDX12 &DrawIndices() const { return indexOverride_ ? indexOverride_->DrawIndices() : indices_; }
    CMeshDX12 *VertexSourceMesh() { return vertexOverride_ ? vertexOverride_->VertexSourceMesh() : this; }
    CMeshDX12 *IndexSourceMesh() { return indexOverride_ ? indexOverride_->IndexSourceMesh() : this; }
    bool DependsOn(const CMeshDX12 *mesh) const;
    bool OverrideBuffers(IMesh *vertexMesh,IMesh *indexMesh);
    MaterialPrimitiveType_t PrimitiveType() const { return primitive_; }
    IMesh *ColorMesh() const { return colorMesh_; }
    IMesh *FlexMesh() const { return flexMesh_; }
    int ColorOffset() const { return colorOffset_; }
    int FlexOffset() const { return flexOffset_; }
private:
    CVertexBufferDX12 vertices_;
    CIndexBufferDX12 indices_;
    MaterialPrimitiveType_t primitive_ = MATERIAL_TRIANGLES;
    DrawCallback draw_ = nullptr;
    void *drawContext_ = nullptr;
    IMesh *colorMesh_ = nullptr;
    IMesh *flexMesh_ = nullptr;
    CMeshDX12 *vertexOverride_ = nullptr, *indexOverride_ = nullptr;
    int colorOffset_ = 0, flexOffset_ = 0;
    bool drawn_ = false;
};

} // namespace shaderapidx12
