#include "vballoctracker_dx12.h"
namespace shaderapidx12
{
CVBAllocTrackerDX12 *g_pVBAllocTrackerDX12=nullptr;
void CVBAllocTrackerDX12::CountVB(void *buffer,bool isDynamic,int bufferSize,int vertexSize,VertexFormat_t fmt)
{
    if (!buffer) return;
    AUTO_LOCK(mutex_);
    records_[records_.Insert(buffer)] = {isDynamic,bufferSize,vertexSize,fmt};
}
void CVBAllocTrackerDX12::UnCountVB(void *buffer)
{
    if (!buffer) return;
    AUTO_LOCK(mutex_);
    records_.Remove(buffer);
}
bool CVBAllocTrackerDX12::TrackMeshAllocations(const char *)
{
    AUTO_LOCK(mutex_);
    const bool old=tracking_;
    tracking_=true;
    return old;
}
size_t CVBAllocTrackerDX12::LiveBytes() const
{
    AUTO_LOCK(mutex_);
    size_t total=0;
    FOR_EACH_HASHTABLE(records_, handle)
        total+=static_cast<size_t>(records_[handle].bytes);
    return total;
}
} // namespace shaderapidx12
