#pragma once
#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <functional>

namespace shaderapidx12
{
// Records the graphics command-list subset used by the renderer into byte chunks. The owning device
// replays full chunks onto the native list on its submission worker, so driver work leaves the
// recording thread. All pointed-to arguments are copied at record time.
//
// Invariants for callers: every object named by a recorded command (resources, heaps, PSOs, root
// signatures) must stay alive until the recording fence completes (already required for GPU use),
// and CPU descriptors consumed at call time (OMSetRenderTargets, Clear*View) must not be rewritten
// until the recording is replayed.
class CCommandRecorderDX12
{
public:
    enum class Op : uint16_t
    {
        OMSetRenderTargets, OMSetStencilRef, RSSetViewports, RSSetScissorRects, SetGraphicsRootSignature,
        SetGraphicsRootDescriptorTable, SetGraphicsRootConstantBufferView, SetGraphicsRoot32BitConstants,
        SetPipelineState, SetDescriptorHeaps, IASetVertexBuffers, IASetIndexBuffer, IASetPrimitiveTopology,
        DrawInstanced, DrawIndexedInstanced, ResourceBarrier, CopyBufferRegion, CopyTextureRegion, CopyResource,
        ResolveSubresource, ClearRenderTargetView, ClearDepthStencilView, BeginQuery, EndQuery, ResolveQueryData,
        CopyDescriptorTable,
    };
    static constexpr size_t kChunkBytes = 256 * 1024;

    // Chunk provider: returns a buffer of kChunkBytes. Flush hands off [data, data+size) for replay.
    std::function<unsigned char *()> acquireChunk;
    std::function<void(unsigned char *, size_t)> flushChunk;

    void Flush()
    {
        if (chunk_ && used_) flushChunk(chunk_, used_);
        else if (chunk_) releaseEmpty_ = chunk_;
        chunk_ = nullptr; used_ = 0;
    }
    // Returns an unused chunk to the caller on shutdown paths; null when the current chunk holds data.
    unsigned char *TakeEmptyChunk() { unsigned char *chunk = releaseEmpty_; releaseEmpty_ = nullptr; return chunk; }

    void OMSetRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs, BOOL single, const D3D12_CPU_DESCRIPTOR_HANDLE *dsv)
    {
        const UINT stored = rtvs ? (single ? (count ? 1u : 0u) : count) : 0u;
        auto *p = Begin(Op::OMSetRenderTargets, 16 + stored * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) + sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
        Put(p, count); Put(p, single); Put(p, stored); Put(p, static_cast<UINT>(dsv != nullptr));
        PutArray(p, rtvs, stored); D3D12_CPU_DESCRIPTOR_HANDLE depth = dsv ? *dsv : D3D12_CPU_DESCRIPTOR_HANDLE{}; Put(p, depth);
    }
    void OMSetStencilRef(UINT value) { auto *p = Begin(Op::OMSetStencilRef, 4); Put(p, value); }
    void RSSetViewports(UINT count, const D3D12_VIEWPORT *viewports) { auto *p = Begin(Op::RSSetViewports, 4 + count * sizeof(D3D12_VIEWPORT)); Put(p, count); PutArray(p, viewports, count); }
    void RSSetScissorRects(UINT count, const D3D12_RECT *rects) { auto *p = Begin(Op::RSSetScissorRects, 4 + count * sizeof(D3D12_RECT)); Put(p, count); PutArray(p, rects, count); }
    void SetGraphicsRootSignature(ID3D12RootSignature *root) { auto *p = Begin(Op::SetGraphicsRootSignature, sizeof(root)); Put(p, root); }
    void SetGraphicsRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table) { auto *p = Begin(Op::SetGraphicsRootDescriptorTable, 4 + sizeof(table)); Put(p, index); Put(p, table); }
    void SetGraphicsRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS address) { auto *p = Begin(Op::SetGraphicsRootConstantBufferView, 4 + sizeof(address)); Put(p, index); Put(p, address); }
    void SetGraphicsRoot32BitConstants(UINT index, UINT count, const void *data, UINT offset)
    {
        auto *p = Begin(Op::SetGraphicsRoot32BitConstants, 12 + count * 4); Put(p, index); Put(p, count); Put(p, offset);
        std::memcpy(p, data, count * 4);
    }
    void SetPipelineState(ID3D12PipelineState *pso) { auto *p = Begin(Op::SetPipelineState, sizeof(pso)); Put(p, pso); }
    void SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap *const *heaps) { auto *p = Begin(Op::SetDescriptorHeaps, 4 + count * sizeof(void *)); Put(p, count); PutArray(p, heaps, count); }
    void IASetVertexBuffers(UINT start, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views)
    {
        const UINT stored = views ? count : 0u;
        auto *p = Begin(Op::IASetVertexBuffers, 12 + stored * sizeof(D3D12_VERTEX_BUFFER_VIEW)); Put(p, start); Put(p, count); Put(p, stored); PutArray(p, views, stored);
    }
    void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *view)
    {
        auto *p = Begin(Op::IASetIndexBuffer, 4 + sizeof(D3D12_INDEX_BUFFER_VIEW)); Put(p, static_cast<UINT>(view != nullptr));
        const D3D12_INDEX_BUFFER_VIEW value = view ? *view : D3D12_INDEX_BUFFER_VIEW{}; Put(p, value);
    }
    void IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) { auto *p = Begin(Op::IASetPrimitiveTopology, sizeof(topology)); Put(p, topology); }
    void DrawInstanced(UINT vertices, UINT instances, UINT startVertex, UINT startInstance)
    {
        auto *p = Begin(Op::DrawInstanced, 16); Put(p, vertices); Put(p, instances); Put(p, startVertex); Put(p, startInstance);
    }
    void DrawIndexedInstanced(UINT indices, UINT instances, UINT startIndex, INT baseVertex, UINT startInstance)
    {
        auto *p = Begin(Op::DrawIndexedInstanced, 20); Put(p, indices); Put(p, instances); Put(p, startIndex); Put(p, baseVertex); Put(p, startInstance);
    }
    void ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers) { auto *p = Begin(Op::ResourceBarrier, 4 + count * sizeof(D3D12_RESOURCE_BARRIER)); Put(p, count); PutArray(p, barriers, count); }
    void CopyBufferRegion(ID3D12Resource *dst, UINT64 dstOffset, ID3D12Resource *src, UINT64 srcOffset, UINT64 bytes)
    {
        auto *p = Begin(Op::CopyBufferRegion, 2 * sizeof(void *) + 24); Put(p, dst); Put(p, dstOffset); Put(p, src); Put(p, srcOffset); Put(p, bytes);
    }
    void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION *dst, UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *box)
    {
        auto *p = Begin(Op::CopyTextureRegion, 2 * sizeof(D3D12_TEXTURE_COPY_LOCATION) + 16 + sizeof(D3D12_BOX));
        Put(p, *dst); Put(p, x); Put(p, y); Put(p, z); Put(p, *src); Put(p, static_cast<UINT>(box != nullptr));
        const D3D12_BOX value = box ? *box : D3D12_BOX{}; Put(p, value);
    }
    void CopyResource(ID3D12Resource *dst, ID3D12Resource *src) { auto *p = Begin(Op::CopyResource, 2 * sizeof(void *)); Put(p, dst); Put(p, src); }
    void ResolveSubresource(ID3D12Resource *dst, UINT dstSub, ID3D12Resource *src, UINT srcSub, DXGI_FORMAT format)
    {
        auto *p = Begin(Op::ResolveSubresource, 2 * sizeof(void *) + 12); Put(p, dst); Put(p, dstSub); Put(p, src); Put(p, srcSub); Put(p, format);
    }
    void ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rectCount, const D3D12_RECT *rects)
    {
        const UINT stored = rects ? rectCount : 0u;
        auto *p = Begin(Op::ClearRenderTargetView, sizeof(rtv) + 16 + 8 + stored * sizeof(D3D12_RECT)); Put(p, rtv); PutArray(p, color, 4); Put(p, rectCount); Put(p, stored); PutArray(p, rects, stored);
    }
    void ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT rectCount, const D3D12_RECT *rects)
    {
        const UINT stored = rects ? rectCount : 0u;
        auto *p = Begin(Op::ClearDepthStencilView, sizeof(dsv) + 20 + stored * sizeof(D3D12_RECT)); Put(p, dsv); Put(p, flags); Put(p, depth); Put(p, static_cast<UINT>(stencil)); Put(p, rectCount); Put(p, stored); PutArray(p, rects, stored);
    }
    void BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index) { auto *p = Begin(Op::BeginQuery, sizeof(heap) + 8); Put(p, heap); Put(p, type); Put(p, index); }
    void EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index) { auto *p = Begin(Op::EndQuery, sizeof(heap) + 8); Put(p, heap); Put(p, type); Put(p, index); }
    void ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start, UINT count, ID3D12Resource *dst, UINT64 offset)
    {
        auto *p = Begin(Op::ResolveQueryData, 2 * sizeof(void *) + 20); Put(p, heap); Put(p, type); Put(p, start); Put(p, count); Put(p, dst); Put(p, offset);
    }

    // Device-side CBV/SRV/UAV copy of `count` single descriptors into one contiguous destination range, performed at
    // replay (before ExecuteCommandLists). Sources must not be rewritten until the recording fence completes.
    void CopyDescriptorTable(D3D12_CPU_DESCRIPTOR_HANDLE destination, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE *sources)
    {
        auto *p = Begin(Op::CopyDescriptorTable, sizeof(destination) + 8 + count * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
        Put(p, destination); Put(p, count); Put(p, count); PutArray(p, sources, count);
    }

    // Replays [data, data+size) onto the native list; device performs descriptor copies.
    static void Replay(ID3D12GraphicsCommandList *list, ID3D12Device *device, const unsigned char *data, size_t size);

private:
    struct Header { Op op; uint16_t pad; uint32_t size; };
    unsigned char *Begin(Op op, size_t payload)
    {
        const size_t total = (sizeof(Header) + payload + 7) & ~size_t(7);
        if (!chunk_ || used_ + total > kChunkBytes)
        {
            Flush();
            chunk_ = TakeEmptyChunk();
            if (!chunk_) chunk_ = acquireChunk();
        }
        auto *header = reinterpret_cast<Header *>(chunk_ + used_);
        header->op = op; header->pad = 0; header->size = static_cast<uint32_t>(total);
        unsigned char *payloadStart = chunk_ + used_ + sizeof(Header);
        used_ += total;
        return payloadStart;
    }
    template<class T> static void Put(unsigned char *&p, const T &value) { std::memcpy(p, &value, sizeof(T)); p += sizeof(T); }
    template<class T> static void PutArray(unsigned char *&p, const T *values, size_t count) { if (count) std::memcpy(p, values, sizeof(T) * count); p += sizeof(T) * count; }

    unsigned char *chunk_ = nullptr;
    unsigned char *releaseEmpty_ = nullptr;
    size_t used_ = 0;
};
}
