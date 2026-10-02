#include "resources_dx12.h"
#include "shaderdevice_dx12.h"
#include "shaderapi/ishaderutil.h"
#include "tier0/dbg.h"
#include <algorithm>
#include <cstring>

namespace shaderapidx12
{
namespace
{
bool ResizeBytes(CUtlMemoryConservative<unsigned char> &bytes, size_t &currentSize, size_t requestedSize)
{
    if (requestedSize > currentSize)
    {
        const size_t capacity = bytes.AllocSize();
        if (requestedSize > capacity)
        {
            size_t newCapacity = capacity ? capacity : 64;
            const size_t maxSize = static_cast<size_t>(-1);
            while (newCapacity < requestedSize)
            {
                if (newCapacity > maxSize / 2)
                {
                    newCapacity = requestedSize;
                    break;
                }
                newCapacity *= 2;
            }
            bytes.ReAlloc(newCapacity);
            if (!bytes.Base()) return false;
        }
        std::memset(bytes.Base() + currentSize, 0, requestedSize - currentSize);
    }
    currentSize = requestedSize;
    return true;
}
}

CShaderBufferDX12::CShaderBufferDX12(const void *data, size_t size)
{
    if (data && size)
    {
        bytes_.ReAlloc(size);
        if (!bytes_.Base()) return;
        std::memcpy(bytes_.Base(), data, size);
        byteSize_ = size;
    }
}

CVertexBufferDX12::CVertexBufferDX12(VertexFormat_t format, int count, bool dynamic)
    : format_(format), vertexCount_(std::max(0, count)), dynamic_(dynamic)
{
    layout_=ComputeVertexLayoutDX12(format);stride_=layout_.valid?layout_.stride:0;
    if (!dynamic_ && stride_ && vertexCount_ > 0 &&
        !ResizeBytes(bytes_, byteSize_, static_cast<size_t>(stride_) * static_cast<size_t>(vertexCount_)))
        vertexCount_ = 0;
}
void CVertexBufferDX12::MarkModified()
{
    ++contentVersion_; if (!contentVersion_) contentVersion_=1;
}
bool CVertexBufferDX12::EnsureCapacity(int count)
{
    if(count<0||!stride_||static_cast<size_t>(count)>static_cast<size_t>(-1)/stride_)return false;
    if(count<=vertexCount_)return true;
    if(!ResizeBytes(bytes_,byteSize_,static_cast<size_t>(count)*stride_))return false;
    vertexCount_=count;MarkModified();return true;
}

void CVertexBufferDX12::BeginCastBuffer(VertexFormat_t format)
{
    format_ = format;
    layout_=ComputeVertexLayoutDX12(format);stride_=layout_.valid?layout_.stride:0;
    written_ = 0;
    byteSize_ = 0;
    if (!dynamic_ && stride_ && vertexCount_ > 0 &&
        !ResizeBytes(bytes_, byteSize_, static_cast<size_t>(stride_) * static_cast<size_t>(vertexCount_)))
        vertexCount_ = 0;
    MarkModified();
}
void CVertexBufferDX12::EndCastBuffer() {}
int CVertexBufferDX12::GetRoomRemaining() const
{
    return std::max(0, vertexCount_ - written_);
}

bool CVertexBufferDX12::Lock(int nVertexCount, bool bAppend, VertexDesc_t &desc)
{
    if (nVertexCount < 0 || nVertexCount > (bAppend ? GetRoomRemaining() : vertexCount_))
        return false;
    const int first = bAppend ? written_ : 0;
    if (!bAppend) written_ = 0;
    return LockRange(first, nVertexCount, desc);
}

bool CVertexBufferDX12::LockStatic(int nVertexCount, VertexDesc_t &desc)
{
    staticLockCount_ = 0;
    if (dynamic_ || nVertexCount < 0 || nVertexCount > vertexCount_ || !LockRange(0, nVertexCount, desc))
        return false;
    staticLockCount_ = nVertexCount;
    return true;
}

bool CVertexBufferDX12::LockRange(int first, int nVertexCount, VertexDesc_t &desc)
{
    const size_t firstSize = static_cast<size_t>(first);
    const size_t countSize = static_cast<size_t>(nVertexCount);
    if (!stride_ || firstSize > static_cast<size_t>(-1) / stride_ || countSize > static_cast<size_t>(-1) / stride_ - firstSize)
        return false;
    const size_t required=(firstSize+countSize)*stride_;
    if ((dynamic_ && byteSize_ < required && !ResizeBytes(bytes_, byteSize_, required)) ||
        (!dynamic_ && byteSize_ < required)) return false;
    unsigned char *data = byteSize_ ? bytes_.Base() + static_cast<size_t>(first) * stride_ : nullptr;
    VertexLayoutDX12 layout = ComputeVertexLayoutDX12(format_, data, &desc);
    if (!layout.valid) return false;
    desc.m_nFirstVertex = first;
    desc.m_nOffset = static_cast<unsigned int>(static_cast<size_t>(first) * stride_);
    return true;
}

void CVertexBufferDX12::Unlock(int nVertexCount, VertexDesc_t &desc)
{
    if (nVertexCount > 0)
    {
        written_ = std::min(vertexCount_, std::max(written_, static_cast<int>(desc.m_nFirstVertex)) + nVertexCount);
        MarkModified();
    }
}

void CVertexBufferDX12::UnlockStatic()
{
    if (staticLockCount_ > 0)
    {
        written_ = std::max(written_, std::min(vertexCount_, staticLockCount_));
        MarkModified();
    }
    staticLockCount_ = 0;
}
void CVertexBufferDX12::Spew(int nVertexCount, const VertexDesc_t &desc)
{
    Msg("ShaderAPIDX12: vertex buffer %d vertices, stride %u, offset %u\n", nVertexCount, stride_, desc.m_nOffset);
}

void CVertexBufferDX12::ValidateData(int nVertexCount, const VertexDesc_t &desc)
{
    if (nVertexCount < 0 || nVertexCount > vertexCount_ || desc.m_ActualVertexSize != static_cast<int>(stride_))
        Warning("ShaderAPIDX12: invalid vertex buffer range or stride\n");
}

CIndexBufferDX12::CIndexBufferDX12(MaterialIndexFormat_t format, int count, bool dynamic)
    : format_(format), indexCount_(std::max(0, count)), dynamic_(dynamic)
{
    indexSize_ = format == MATERIAL_INDEX_FORMAT_32BIT ? 4u : 2u;
    if(!dynamic_ && !ResizeBytes(bytes_,byteSize_,static_cast<size_t>(indexSize_) * static_cast<size_t>(indexCount_)))indexCount_=0;
}
bool CIndexBufferDX12::EnsureCapacity(int count)
{
    if(count<0||static_cast<size_t>(count)>static_cast<size_t>(-1)/indexSize_)return false;
    if(count<=indexCount_)return true;
    if(!ResizeBytes(bytes_,byteSize_,static_cast<size_t>(count)*indexSize_))return false;
    indexCount_=count;MarkModified();return true;
}
void CIndexBufferDX12::MarkModified()
{
    ++contentVersion_; if (!contentVersion_) contentVersion_=1;
}

void CIndexBufferDX12::BeginCastBuffer(MaterialIndexFormat_t format)
{
    format_ = format;
    indexSize_ = format == MATERIAL_INDEX_FORMAT_32BIT ? 4u : 2u;
    written_ = 0;
    byteSize_=0;if(!dynamic_ && !ResizeBytes(bytes_,byteSize_,static_cast<size_t>(indexSize_) * static_cast<size_t>(indexCount_)))indexCount_=0;MarkModified();
}

void CIndexBufferDX12::EndCastBuffer() {}

int CIndexBufferDX12::GetRoomRemaining() const
{
    return std::max(0, indexCount_ - written_);
}

bool CIndexBufferDX12::Lock(int nMaxIndexCount, bool bAppend, IndexDesc_t &desc)
{
    if (nMaxIndexCount < 0 || nMaxIndexCount > (bAppend ? GetRoomRemaining() : indexCount_))
        return false;
    const int first = bAppend ? written_ : 0;
    if (!bAppend) written_ = 0;
    const size_t firstSize = static_cast<size_t>(first);
    const size_t countSize = static_cast<size_t>(nMaxIndexCount);
    if (firstSize > static_cast<size_t>(-1) / indexSize_ || countSize > static_cast<size_t>(-1) / indexSize_ - firstSize)
        return false;
    const size_t required=(firstSize+countSize)*indexSize_;
    if ((dynamic_ && byteSize_ < required && !ResizeBytes(bytes_, byteSize_, required)) ||
        (!dynamic_ && byteSize_ < required)) return false;
    desc.m_pIndices = byteSize_ ? reinterpret_cast<unsigned short *>(bytes_.Base() + static_cast<size_t>(first) * indexSize_) : nullptr;
    desc.m_nOffset = static_cast<unsigned int>(static_cast<size_t>(first) * indexSize_);
    desc.m_nFirstIndex = static_cast<unsigned int>(first);
    desc.m_nIndexSize = static_cast<unsigned char>(indexSize_ / sizeof(unsigned short));
    return true;
}

void CIndexBufferDX12::Unlock(int nWrittenIndexCount, IndexDesc_t &desc)
{
    if (nWrittenIndexCount > 0)
    {
        written_ = std::min(indexCount_, std::max(written_, static_cast<int>(desc.m_nFirstIndex)) + nWrittenIndexCount);
        MarkModified();
    }
}

void CIndexBufferDX12::ModifyBegin(bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc)
{
    modifyingWritable_ = false;
    if (nFirstIndex < 0 || nIndexCount < 0 || nFirstIndex > indexCount_ || nIndexCount > indexCount_ - nFirstIndex)
    {
        std::memset(&desc, 0, sizeof(desc));
        return;
    }
    const size_t firstSize = static_cast<size_t>(nFirstIndex);
    const size_t countSize = static_cast<size_t>(nIndexCount);
    if (firstSize > static_cast<size_t>(-1) / indexSize_ || countSize > static_cast<size_t>(-1) / indexSize_ - firstSize)
    {
        std::memset(&desc, 0, sizeof(desc));
        return;
    }
    const size_t required=(firstSize+countSize)*indexSize_;
    if(required>byteSize_)
    {
        if(bReadOnly||!dynamic_||!ResizeBytes(bytes_,byteSize_,required)){std::memset(&desc,0,sizeof(desc));return;}
    }
    desc.m_pIndices = byteSize_ ? reinterpret_cast<unsigned short *>(bytes_.Base() + static_cast<size_t>(nFirstIndex) * indexSize_) : nullptr;
    desc.m_nOffset = static_cast<unsigned int>(static_cast<size_t>(nFirstIndex) * indexSize_);
    desc.m_nFirstIndex = static_cast<unsigned int>(nFirstIndex);
    desc.m_nIndexSize = static_cast<unsigned char>(indexSize_ / sizeof(unsigned short));
    if (!bReadOnly && nIndexCount > 0)
    {
        written_ = std::max(written_, nFirstIndex + nIndexCount);
        modifyingWritable_ = true;
    }
}

void CIndexBufferDX12::ModifyEnd(IndexDesc_t &) { if(modifyingWritable_)MarkModified();modifyingWritable_=false; }

void CIndexBufferDX12::Spew(int nIndexCount, const IndexDesc_t &desc)
{
    Msg("ShaderAPIDX12: index buffer %d indices, %u-byte elements, offset %u\n", nIndexCount, indexSize_, desc.m_nOffset);
}

void CIndexBufferDX12::ValidateData(int nIndexCount, const IndexDesc_t &desc)
{
    if (nIndexCount < 0 || nIndexCount > indexCount_ || desc.m_nIndexSize != indexSize_ / sizeof(unsigned short))
        Warning("ShaderAPIDX12: invalid index buffer range or format\n");
}

CMeshDX12::CMeshDX12(VertexFormat_t format, int vertexCount, bool dynamic, DrawCallback draw, void *drawContext)
    : vertices_(format, vertexCount, dynamic), indices_(MATERIAL_INDEX_FORMAT_16BIT, INDEX_BUFFER_SIZE, dynamic), draw_(draw), drawContext_(drawContext)
{
}
bool CMeshDX12::DependsOn(const CMeshDX12 *mesh) const
{
    return this==mesh||(vertexOverride_&&vertexOverride_->DependsOn(mesh))||(indexOverride_&&indexOverride_->DependsOn(mesh));
}
bool CMeshDX12::OverrideBuffers(IMesh *vertexMesh, IMesh *indexMesh)
{
    auto *vertex=static_cast<CMeshDX12 *>(vertexMesh);
    auto *index=static_cast<CMeshDX12 *>(indexMesh);
    if((vertex&&vertex->DependsOn(this))||(index&&index->DependsOn(this)))return false;
    vertexOverride_=vertex;indexOverride_=index;
    return true;
}
void CMeshDX12::EndCastBuffer()
{
    vertices_.EndCastBuffer();
    indices_.EndCastBuffer();
}

bool CMeshDX12::IsDynamic() const
{
    return vertices_.IsDynamic() || indices_.IsDynamic();
}

void CMeshDX12::SetPrimitiveType(MaterialPrimitiveType_t type)
{
    if(g_pShaderDeviceMgrDX12&&g_pShaderDeviceMgrDX12->HostShaderUtil()&&!g_pShaderDeviceMgrDX12->HostShaderUtil()->OnSetPrimitiveType(this,type))return;
    primitive_=type;
}
void CMeshDX12::SetColorMesh(IMesh *mesh,int offset)
{
    if(g_pShaderDeviceMgrDX12&&g_pShaderDeviceMgrDX12->HostShaderUtil()&&!g_pShaderDeviceMgrDX12->HostShaderUtil()->OnSetColorMesh(this,mesh,offset))return;
    colorMesh_=mesh;colorOffset_=offset;
}
void CMeshDX12::SetFlexMesh(IMesh *mesh,int offset)
{
    if(g_pShaderDeviceMgrDX12&&g_pShaderDeviceMgrDX12->HostShaderUtil()&&!g_pShaderDeviceMgrDX12->HostShaderUtil()->OnSetFlexMesh(this,mesh,offset))return;
    flexMesh_=mesh;flexOffset_=offset;
}
void CMeshDX12::Draw(int nFirstIndex,int nIndexCount)
{
    if(g_pShaderDeviceMgrDX12&&g_pShaderDeviceMgrDX12->HostShaderUtil()&&!g_pShaderDeviceMgrDX12->HostShaderUtil()->OnDrawMesh(this,nFirstIndex,nIndexCount)){drawn_=true;return;}
    if(nFirstIndex<0)nFirstIndex=0;
    if(nIndexCount<=0)nIndexCount=DrawIndices().WrittenCount();
    if(nFirstIndex>DrawIndices().WrittenCount()||nIndexCount>DrawIndices().WrittenCount()-nFirstIndex)return;
    if(draw_)draw_(drawContext_,this,nFirstIndex,nIndexCount);
    drawn_=true;
}
void CMeshDX12::Draw(CPrimList *lists,int nLists)
{
    if(g_pShaderDeviceMgrDX12&&g_pShaderDeviceMgrDX12->HostShaderUtil()&&!g_pShaderDeviceMgrDX12->HostShaderUtil()->OnDrawMesh(this,lists,nLists)){drawn_=true;return;}
    if(!lists||nLists<=0)return;
    for(int i=0;i<nLists;++i){int first=lists[i].m_FirstIndex,count=lists[i].m_NumIndices;if(first>=0&&count>0&&first<=DrawIndices().WrittenCount()&&count<=DrawIndices().WrittenCount()-first&&draw_)draw_(drawContext_,this,first,count);}drawn_=true;
}
void CMeshDX12::CopyToMeshBuilder(int firstVertex,int vertexCount,int firstIndex,int indexCount,int indexOffset,CMeshBuilder &builder)
{
    const auto &sourceVertices=DrawVertices();const auto &sourceIndices=DrawIndices();
    if(firstVertex<0||vertexCount<0||firstIndex<0||indexCount<0||firstVertex>sourceVertices.WrittenCount()||vertexCount>sourceVertices.WrittenCount()-firstVertex||firstIndex>sourceIndices.WrittenCount()||indexCount>sourceIndices.WrittenCount()-firstIndex||
       (vertexCount>0&&(builder.VertexSize()!=static_cast<int>(sourceVertices.Stride())||!builder.Position())))
    {
        Warning("ShaderAPIDX12: CopyToMeshBuilder received incompatible vertex layout or range\n");return;
    }
    const unsigned char *source=sourceIndices.Bytes().data()+static_cast<size_t>(firstIndex)*sourceIndices.IndexSize();
    for(int i=0;i<indexCount;++i)
    {
        uint32_t index=0;
        if(sourceIndices.IndexSize()==2){uint16_t value;std::memcpy(&value,source+static_cast<size_t>(i)*2,2);index=value;}
        else std::memcpy(&index,source+static_cast<size_t>(i)*4,4);
        const int64_t adjusted=static_cast<int64_t>(index)+indexOffset;
        if(adjusted<0||adjusted>UINT16_MAX){Warning("ShaderAPIDX12: CopyToMeshBuilder index exceeds 16-bit destination\n");return;}
    }
    if(vertexCount){std::memcpy(const_cast<float *>(builder.Position()),sourceVertices.Bytes().data()+static_cast<size_t>(firstVertex)*sourceVertices.Stride(),static_cast<size_t>(vertexCount)*sourceVertices.Stride());builder.AdvanceVertices(vertexCount);}
    for(int i=0;i<indexCount;++i)
    {
        uint32_t index=0;
        if(sourceIndices.IndexSize()==2){uint16_t value;std::memcpy(&value,source+static_cast<size_t>(i)*2,2);index=value;}
        else std::memcpy(&index,source+static_cast<size_t>(i)*4,4);
        builder.Index(static_cast<unsigned short>(static_cast<int64_t>(index)+indexOffset));builder.AdvanceIndex();
    }
}

void CMeshDX12::Spew(int nVertexCount, int nIndexCount, const MeshDesc_t &desc)
{
    vertices_.Spew(nVertexCount, desc);
    indices_.Spew(nIndexCount, desc);
}

void CMeshDX12::ValidateData(int nVertexCount, int nIndexCount, const MeshDesc_t &desc)
{
    vertices_.ValidateData(nVertexCount, desc);
    indices_.ValidateData(nIndexCount, desc);
}

void CMeshDX12::LockMesh(int nVertexCount, int nIndexCount, MeshDesc_t &desc)
{
    if(nVertexCount<0||nIndexCount<-1){std::memset(&desc,0,sizeof(desc));return;}
    const int writableVertices=vertexOverride_?0:nVertexCount;
    const int writableIndices=indexOverride_&&nIndexCount>=0?0:nIndexCount;
    if(!vertices_.EnsureCapacity(writableVertices)||(writableIndices>=0&&!indices_.EnsureCapacity(writableIndices))) {std::memset(&desc,0,sizeof(desc));return;}
    const bool vertices=vertices_.IsDynamic()?vertices_.Lock(writableVertices,false,desc):vertices_.LockStatic(writableVertices,desc);
    const bool indices=writableIndices<0||indices_.Lock(writableIndices,false,desc);
    if(!vertices||!indices)std::memset(&desc,0,sizeof(desc));
}

void CMeshDX12::ModifyBegin(int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, MeshDesc_t &desc)
{
    if (nFirstVertex < 0 || nVertexCount < 0 || nFirstIndex < 0 || nIndexCount < 0 ||
        nFirstVertex > vertices_.VertexCount() || nVertexCount > vertices_.VertexCount()-nFirstVertex || nFirstIndex > indices_.IndexCount() || nIndexCount > indices_.IndexCount()-nFirstIndex)
    {
        std::memset(&desc, 0, sizeof(desc));
        return;
    }
    if(nVertexCount>0)vertices_.MarkModified();
    VertexLayoutDX12 layout = ComputeVertexLayoutDX12(vertices_.GetVertexFormat(),
        vertices_.Data().empty()?nullptr:const_cast<unsigned char *>(vertices_.Data().data()) + static_cast<size_t>(nFirstVertex) * vertices_.Stride(), &desc);
    indices_.ModifyBegin(false, nFirstIndex, nIndexCount, desc);
    desc.m_nFirstVertex = nFirstVertex;
    desc.VertexDesc_t::m_nOffset = static_cast<unsigned int>(static_cast<size_t>(nFirstVertex) * vertices_.Stride());
    if (!layout.valid) std::memset(&desc, 0, sizeof(desc));
}

void CMeshDX12::ModifyEnd(MeshDesc_t &desc)
{
    indices_.ModifyEnd(desc);
}

void CMeshDX12::UnlockMesh(int nVertexCount, int nIndexCount, MeshDesc_t &desc)
{
    if(vertices_.IsDynamic())vertices_.Unlock(vertexOverride_?0:nVertexCount, desc);
    else vertices_.UnlockStatic();
    if (nIndexCount >= 0) indices_.Unlock(indexOverride_?0:nIndexCount, desc);
}

void CMeshDX12::ModifyBeginEx(bool bReadOnly, int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, MeshDesc_t &desc)
{
    if (bReadOnly)
    {
        if (nFirstVertex < 0 || nVertexCount < 0 || nFirstVertex > vertices_.VertexCount() || nVertexCount > vertices_.VertexCount()-nFirstVertex)
        {
            std::memset(&desc, 0, sizeof(desc));
            return;
        }
        ComputeVertexLayoutDX12(vertices_.GetVertexFormat(), vertices_.Data().empty()?nullptr:const_cast<unsigned char *>(vertices_.Data().data()) + static_cast<size_t>(nFirstVertex) * vertices_.Stride(), &desc);
        desc.m_nFirstVertex = nFirstVertex;
        desc.VertexDesc_t::m_nOffset = static_cast<unsigned int>(static_cast<size_t>(nFirstVertex) * vertices_.Stride());
        if (nIndexCount >= 0) indices_.ModifyBegin(true, nFirstIndex, nIndexCount, desc);
    }
    else
        ModifyBegin(nFirstVertex, nVertexCount, nFirstIndex, nIndexCount, desc);
}

unsigned CMeshDX12::ComputeMemoryUsed()
{
    return static_cast<unsigned>(vertices_.Data().size() + indices_.Data().size());
}

} // namespace shaderapidx12
