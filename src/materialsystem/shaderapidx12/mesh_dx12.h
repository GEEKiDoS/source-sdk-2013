//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once
#include "resources_dx12.h"
#include "mathlib/vmatrix.h"
#include "tier1/utlvector.h"
#include <limits>

namespace shaderapidx12
{
// Source selection records are count, minimum depth, maximum depth, then names.
// Accumulate geometry under one name stack until a name/state transition.
class CSelectionStateDX12
{
public:
    void SetBuffer(unsigned int *buffer, int words);
    int SetMode(bool enabled);
    bool Enabled() const { return enabled_; }
    void ClearNames();
    void LoadName(unsigned int name);
    void PushName(unsigned int name);
    void PopName();
    void Record(float minimum, float maximum);
    void Flush();
private:
    unsigned int *buffer_ = nullptr;
    size_t capacity_ = 0, used_ = 0;
    CUtlVector<unsigned int> names_;
    int hits_ = 0;
    bool enabled_ = false, overflow_ = false;
    float minimum_ = std::numeric_limits<float>::max(), maximum_ = 0.f;
};

// Like the reference temp-mesh path, selection tests CPU positions rather than
// vertex-shader deformation. Homogeneous clipping also handles negative w.
void TestSelectionDX12(const CVertexBufferDX12 &vertices, const CIndexBufferDX12 &indices,
                       MaterialPrimitiveType_t primitive, int firstIndex, int indexCount,
                       const VMatrix &modelToClip, bool cull, bool frontCounterClockwise,
                       CSelectionStateDX12 &selection, size_t vertexOffset = 0, size_t indexOffset = 0);
}
