//========= Copyright Valve Corporation, All rights reserved. ============//
// Selection record semantics follow shaderapidx8.cpp and meshdx8.cpp.
#include "mesh_dx12.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace shaderapidx12
{
static_assert(sizeof(VertexFormat_t) == sizeof(uint64_t), "DX12 vertex formats must remain 64-bit");

void CSelectionStateDX12::SetBuffer(unsigned int *buffer, int words)
{
    if (enabled_) return;
    buffer_ = buffer;
    capacity_ = buffer && words > 0 ? static_cast<size_t>(words) : 0;
    used_ = 0;
}
int CSelectionStateDX12::SetMode(bool enabled)
{
    if (enabled_) Flush();
    const int result = overflow_ ? -1 : hits_;
    enabled_ = enabled;
    used_ = 0;
    hits_ = 0;
    overflow_ = false;
    minimum_ = std::numeric_limits<float>::max();
    maximum_ = 0.f;
    return result;
}
void CSelectionStateDX12::Flush()
{
    if (enabled_ && !names_.IsEmpty() && minimum_ != std::numeric_limits<float>::max())
    {
        const size_t nameCount = static_cast<size_t>(names_.Count());
        if (nameCount > capacity_ || capacity_ - nameCount < 3 || used_ > capacity_ - nameCount - 3)
            overflow_ = true;
        else
        {
            buffer_[used_++] = static_cast<unsigned int>(nameCount);
            const double range = static_cast<double>(std::numeric_limits<unsigned int>::max());
            buffer_[used_++] = static_cast<unsigned int>(0.5 + minimum_ * range);
            buffer_[used_++] = static_cast<unsigned int>(0.5 + maximum_ * range);
            std::copy_n(names_.Base(), names_.Count(), buffer_ + used_);
            used_ += nameCount;
            ++hits_;
        }
    }
    minimum_ = std::numeric_limits<float>::max();
    maximum_ = 0.f;
}
void CSelectionStateDX12::ClearNames() { Flush(); names_.RemoveAll(); }
void CSelectionStateDX12::LoadName(unsigned int name)
{
    if (!enabled_) return;
    Flush();
    if (!names_.IsEmpty()) names_.Tail() = name;
}
void CSelectionStateDX12::PushName(unsigned int name)
{
    if (enabled_) { Flush(); names_.AddToTail(name); }
}
void CSelectionStateDX12::PopName()
{
    if (enabled_) { Flush(); if (!names_.IsEmpty()) names_.RemoveMultipleFromTail(1); }
}
void CSelectionStateDX12::Record(float minimum, float maximum)
{
    if (!enabled_ || !std::isfinite(minimum) || !std::isfinite(maximum)) return;
    minimum_ = std::min(minimum_, std::clamp(minimum, 0.f, 1.f));
    maximum_ = std::max(maximum_, std::clamp(maximum, 0.f, 1.f));
}

namespace
{
using ClipVertex = std::array<float,4>;
float PlaneDistance(const ClipVertex &vertex, int plane)
{
    switch (plane)
    {
    case 0: return vertex[0] + vertex[3];
    case 1: return vertex[3] - vertex[0];
    case 2: return vertex[1] + vertex[3];
    case 3: return vertex[3] - vertex[1];
    case 4: return vertex[2];
    default: return vertex[3] - vertex[2];
    }
}
}

void TestSelectionDX12(const CVertexBufferDX12 &vertices, const CIndexBufferDX12 &indices,
                       MaterialPrimitiveType_t primitive, int firstIndex, int indexCount,
                       const VMatrix &modelToClip, bool cull, bool frontCounterClockwise,
                       CSelectionStateDX12 &selection, size_t vertexOffset, size_t indexOffset)
{
    if (!selection.Enabled() || (primitive != MATERIAL_TRIANGLES && primitive != MATERIAL_TRIANGLE_STRIP) ||
        firstIndex < 0 || indexCount < 3 || !vertices.Stride() || vertexOffset > vertices.Bytes().size() ||
        indexOffset > indices.Bytes().size()) return;
    const size_t indexSize = indices.IndexSize();
    const size_t available = (indices.Bytes().size() - indexOffset) / indexSize;
    if (static_cast<size_t>(firstIndex) > available || static_cast<size_t>(indexCount) > available - firstIndex) return;
    const auto layout = ComputeVertexLayoutDX12(vertices.GetVertexFormat());
    const VertexInputDX12 *position = nullptr;
    for (uint32_t i=0; i<layout.inputCount; ++i)
        if (std::strcmp(layout.inputs[i].semantic, "POSITION") == 0 && layout.inputs[i].semanticIndex == 0)
            position = &layout.inputs[i];
    if (!position || position->format != DXGI_FORMAT_R32G32B32_FLOAT) return;
    const int triangles = primitive == MATERIAL_TRIANGLES ? indexCount/3 : indexCount-2;
    for (int triangle=0; triangle<triangles; ++triangle)
    {
        std::array<ClipVertex,16> polygon{}, temporary{};
        bool valid = true;
        for (int corner=0; corner<3; ++corner)
        {
            const int local = primitive == MATERIAL_TRIANGLES ? triangle*3+corner : triangle+corner;
            const unsigned char *index = indices.Bytes().data()+indexOffset+static_cast<size_t>(firstIndex+local)*indexSize;
            uint32_t vertexIndex = 0;
            if (indexSize == 4) std::memcpy(&vertexIndex,index,4);
            else { uint16_t shortIndex; std::memcpy(&shortIndex,index,2); vertexIndex=shortIndex; }
            const size_t offset = vertexOffset+static_cast<size_t>(vertexIndex)*vertices.Stride()+position->byteOffset;
            if (offset > vertices.Bytes().size() || vertices.Bytes().size()-offset < 3*sizeof(float)) { valid=false; break; }
            float p[3]; std::memcpy(p,vertices.Bytes().data()+offset,sizeof(p));
            for (int row=0; row<4; ++row)
            {
                polygon[corner][row]=modelToClip[row][0]*p[0]+modelToClip[row][1]*p[1]+modelToClip[row][2]*p[2]+modelToClip[row][3];
                valid = valid && std::isfinite(polygon[corner][row]);
            }
        }
        if (!valid) continue;
        if (primitive == MATERIAL_TRIANGLE_STRIP && (triangle&1)) std::swap(polygon[0],polygon[1]);
        int count=3;
        for (int plane=0; plane<6 && count>=3; ++plane)
        {
            int output=0;
            ClipVertex previous=polygon[count-1];
            float previousDistance=PlaneDistance(previous,plane);
            for (int i=0; i<count; ++i)
            {
                const ClipVertex &current=polygon[i];
                const float distance=PlaneDistance(current,plane);
                if ((distance>=0) != (previousDistance>=0))
                {
                    const float t=previousDistance/(previousDistance-distance);
                    for (int component=0; component<4; ++component)
                        temporary[output][component]=previous[component]+t*(current[component]-previous[component]);
                    ++output;
                }
                if (distance>=0) temporary[output++]=current;
                previous=current; previousDistance=distance;
            }
            count=output;
            std::copy_n(temporary.begin(),count,polygon.begin());
        }
        if (count<3) continue;
        float minimum=1.f,maximum=0.f;
        for (int i=0; i<count; ++i)
        {
            if (!(polygon[i][3]>0.f)) { valid=false; break; }
            const float inverse=1.f/polygon[i][3];
            for (int component=0; component<3; ++component) polygon[i][component]*=inverse;
            minimum=std::min(minimum,polygon[i][2]); maximum=std::max(maximum,polygon[i][2]);
        }
        if (!valid) continue;
        float area=0.f;
        for (int i=0; i<count; ++i)
        {
            const auto &a=polygon[i], &b=polygon[(i+1)%count];
            area+=a[0]*b[1]-a[1]*b[0];
        }
        if (area==0.f || (cull && (frontCounterClockwise ? area>=0.f : area<=0.f))) continue;
        selection.Record(minimum,maximum);
    }
}
}
