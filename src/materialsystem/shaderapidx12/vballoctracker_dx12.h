#pragma once
#include "materialsystem/ivballoctracker.h"
#include "tier0/threadtools.h"
#include "tier1/utlhashtable.h"
namespace shaderapidx12
{
class CVBAllocTrackerDX12 final : public IVBAllocTracker
{
public:
    void CountVB(void *buffer,bool isDynamic,int bufferSize,int vertexSize,VertexFormat_t fmt) override;
    void UnCountVB(void *buffer) override;
    bool TrackMeshAllocations(const char *allocatorName) override;
    size_t LiveBytes() const;
private:
    struct Record{bool dynamic;int bytes,vertexSize;VertexFormat_t format;};
    mutable CThreadFastMutex mutex_; CUtlHashtable<void *, Record> records_; bool tracking_=false;
};
extern CVBAllocTrackerDX12 *g_pVBAllocTrackerDX12;
} // namespace shaderapidx12
