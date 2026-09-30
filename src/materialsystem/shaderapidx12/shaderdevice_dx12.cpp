//========= Copyright Valve Corporation, All rights reserved. ============//
// Native DX12 device; display-gamma CPU curve follows shaderdevicedx8.cpp.
#include "shaderdevice_dx12.h"
#include "shaderapi_dx12.h"
#include "hardwareconfig_dx12.h"
#include "resources_dx12.h"
#include "filesystem.h"
#include "shader_translate_dx12.h"
#include "tier0/dbg.h"
#include "tier0/platform.h"
#include "tier1/tier1.h"
#include "tracy_dx12.h"
#include "tier2/tier2.h"
#include "tier1/convar.h"
#include "icvar.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <windows.h>
#include "tier0/icommandline.h"
#include <d3d12sdklayers.h>
#include "shaderapi/ishaderutil.h"
#include <climits>
#include <cmath>
#include <atomic>

namespace shaderapidx12
{
static bool LoadDxbcSigner(HMODULE &module,SignDxbcFnDX12 &signer)
{
 char path[MAX_PATH]{};HMODULE self=nullptr;GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&LoadDxbcSigner),&self);if(!self||!GetModuleFileNameA(self,path,sizeof(path)))return false;char *slash=strrchr(path,'\\');if(!slash)slash= strrchr(path,'/');if(slash)slash[1]=0;std::string full=std::string(path)+"dxbcSigner.dll";module=LoadLibraryExA(full.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module)return false;signer=reinterpret_cast<SignDxbcFnDX12>(GetProcAddress(module,"SignDxbc"));if(!signer){FreeLibrary(module);module=nullptr;return false;}return true;
}
 
CShaderDeviceMgrDX12 *g_pShaderDeviceMgrDX12 = nullptr;
CShaderDeviceDX12 *g_pShaderDeviceDX12 = nullptr;
std::atomic<bool> g_tracyZonesActiveDX12{false};
void RefreshTracyZonesDX12()
{
#ifdef TRACY_ENABLE
    // -dx12nozones keeps Tracy sampling/frames while disabling renderer zones, so sampled
    // captures measure the disconnected hot path rather than instrumentation cost.
    static const bool zonesDisabled = CommandLine() && CommandLine()->CheckParm("-dx12nozones");
    g_tracyZonesActiveDX12.store(!zonesDisabled && TracyIsStarted && tracy::GetProfiler().IsConnected(), std::memory_order_relaxed);
#endif
}

CShaderDeviceDX12::CShaderDeviceDX12() = default;
CShaderDeviceDX12::~CShaderDeviceDX12() { ShutdownDevice(); }

void CShaderDeviceDX12::FailDevice(const char *operation, HRESULT hr)
{
    if (failed_) return;
    failed_ = true;
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : S_OK;
    Warning("ShaderAPIDX12: %s failed (0x%08x), device removal reason 0x%08x\n", operation, static_cast<unsigned>(hr), static_cast<unsigned>(reason));
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (device_ && SUCCEEDED(device_.As(&dred)))
    {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
        D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)) && breadcrumbs.pHeadAutoBreadcrumbNode)
            Warning("ShaderAPIDX12: DRED breadcrumbs available at %p\n", breadcrumbs.pHeadAutoBreadcrumbNode);
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault)) && fault.PageFaultVA)
            Warning("ShaderAPIDX12: DRED page fault GPU VA 0x%llx\n", static_cast<unsigned long long>(fault.PageFaultVA));
    }
    Error("ShaderAPIDX12: native D3D12 rendering stopped at %s (0x%08x), removal 0x%08x\n", operation, static_cast<unsigned>(hr), static_cast<unsigned>(reason));
}

bool CShaderDeviceDX12::CheckDevice(const char *operation, HRESULT hr)
{
    if (SUCCEEDED(hr)) return true;
    FailDevice(operation, hr);
    return false;
}

bool CShaderDeviceDX12::Initialize(void *hwnd, int adapter, const ShaderDeviceInfo_t &info, IDXGIAdapter1 *selectedAdapter)
{
    ShutdownDevice();
    if (!hwnd || !IsWindow(static_cast<HWND>(hwnd)) || !selectedAdapter) { Warning("ShaderAPIDX12: valid HWND and adapter required\n"); return false; }
    const bool debug = CommandLine() && CommandLine()->CheckParm("-dx12debug");
    const bool gbv = CommandLine() && CommandLine()->CheckParm("-dx12gpuvalidation");
    if (debug || gbv)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> controller;
        const HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&controller));
        if (FAILED(hr)) { Warning("ShaderAPIDX12: requested DX12 debug layer unavailable (0x%08x); install Graphics Tools\n", static_cast<unsigned>(hr)); return false; }
        controller->EnableDebugLayer();
        if (gbv) { Microsoft::WRL::ComPtr<ID3D12Debug1> validation; if (FAILED(controller.As(&validation))) { Warning("ShaderAPIDX12: GPU validation interface unavailable\n"); return false; } validation->SetEnableGPUBasedValidation(TRUE); }
    }
    HRESULT hr = CreateDXGIFactory2((debug || gbv) ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) { Warning("ShaderAPIDX12: CreateDXGIFactory2 failed (0x%08x)\n", static_cast<unsigned>(hr)); ShutdownDevice(); return false; }
    hr = D3D12CreateDevice(selectedAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) { Warning("ShaderAPIDX12: D3D12CreateDevice failed (0x%08x)\n", static_cast<unsigned>(hr)); ShutdownDevice(); return false; }
    if (!LoadDxbcSigner(signerModule_, signer_)) { Warning("ShaderAPIDX12: required dxbcSigner.dll/SignDxbc missing beside renderer\n"); ShutdownDevice(); return false; }
    char rendererPath[MAX_PATH]{};HMODULE rendererModule=nullptr;GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&LoadDxbcSigner),&rendererModule);if(!rendererModule||!GetModuleFileNameA(rendererModule,rendererPath,sizeof(rendererPath))){Warning("ShaderAPIDX12: unable to locate renderer module while checking stdshader_dx12.dll\n");ShutdownDevice();return false;}char *rendererSlash=strrchr(rendererPath,'\\');if(!rendererSlash)rendererSlash=strrchr(rendererPath,'/');if(rendererSlash)rendererSlash[1]=0;const std::string nativeShaderDll=std::string(rendererPath)+"stdshader_dx12.dll";if(GetFileAttributesA(nativeShaderDll.c_str())==INVALID_FILE_ATTRIBUTES){Warning("ShaderAPIDX12: required stdshader_dx12.dll missing beside renderer: %s\n",nativeShaderDll.c_str());ShutdownDevice();return false;}
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_));
    if (FAILED(hr)) { Warning("ShaderAPIDX12: CreateCommandQueue failed (0x%08x)\n", static_cast<unsigned>(hr)); ShutdownDevice(); return false; }
    adapterIndex_ = adapter; window_ = hwnd; ownerThread_ = GetCurrentThreadId();
    waitForVsync_ = info.m_bWaitForVSync; windowed_ = info.m_bWindowed;
    backBufferCount_ = std::clamp(info.m_nBackBufferCount, 1, 2) + 1;
    sampleCount_ = std::max(1, info.m_nAASamples); sampleQuality_ = info.m_nAAQuality;
    if (!SupportsMSAA(sampleCount_, sampleQuality_)) { Warning("ShaderAPIDX12: unsupported MSAA count %d quality %d\n", sampleCount_, sampleQuality_); ShutdownDevice(); return false; }
    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    BOOL supported = FALSE;
    if (SUCCEEDED(factory_.As(&factory5))) factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported));
    allowTearing_ = supported != FALSE;
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    RECT rect{}; GetClientRect(static_cast<HWND>(hwnd), &rect);
    const int w = info.m_DisplayMode.m_nWidth > 0 ? info.m_DisplayMode.m_nWidth : static_cast<int>(rect.right-rect.left);
    const int h = info.m_DisplayMode.m_nHeight > 0 ? info.m_DisplayMode.m_nHeight : static_cast<int>(rect.bottom-rect.top);
    if (!CreateFrameObjects() || !AddView(hwnd)) { ShutdownDevice(); return false; }
    if (w > 0 && h > 0 && (w != width_ || h != height_) && !ResizeView(*currentView_, w, h)) { ShutdownDevice(); return false; }
    width_ = currentView_->width; height_ = currentView_->height;
    g_pShaderDeviceDX12 = this;
    StartSubmitThread();
    return true;
}

bool CShaderDeviceDX12::SupportsMSAA(int count, int quality) const
{
    if (!device_ || count < 1 || quality < 0) return false;
    if (count == 1) return quality == 0;
    for (DXGI_FORMAT format : { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT })
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{format, static_cast<UINT>(count), D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0};
        if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) || static_cast<UINT>(quality) >= levels.NumQualityLevels) return false;
    }
    return true;
}

bool CShaderDeviceDX12::CreateFrameObjects()
{
    for (auto &frame : frames_) if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)))) { Warning("ShaderAPIDX12: CreateCommandAllocator failed\n"); return false; }
    // One list per frame context: the submission worker may still be closing/executing the previous list.
    for (size_t i = 0; i < frames_.size(); ++i)
    {
        HRESULT hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[i].allocator.Get(), nullptr, IID_PPV_ARGS(&frames_[i].list));
        if (FAILED(hr)) { Warning("ShaderAPIDX12: CreateCommandList failed (0x%08x)\n", static_cast<unsigned>(hr)); return false; }
        if (i != 0 && FAILED(frames_[i].list->Close())) return false;
    }
    frameIndex_ = 0;
    recording_ = true;
    recorder_.acquireChunk=[this]{return AcquireCommandChunk();};
    recorder_.flushChunk=[this](unsigned char *chunk,size_t bytes){FlushCommandChunk(chunk,bytes);};
    HRESULT hr;
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) { Warning("ShaderAPIDX12: CreateFence failed (0x%08x)\n", static_cast<unsigned>(hr)); return false; }
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return fenceEvent_ != nullptr;
}

bool CShaderDeviceDX12::BeginRecording()
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 BeginRecording", DX12_ZONES_ACTIVE);
    frameIndex_ = (frameIndex_ + 1) % frames_.size();
    FrameContext &frame = frames_[frameIndex_];
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 BeginRecording Fence Wait", DX12_ZONES_ACTIVE);
        if (frame.fence && !WaitForFence(frame.fence)) return false;
    }
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 BeginRecording Retained Release", DX12_ZONES_ACTIVE);
        for (int i = 0; i < frame.retained.Count(); ++i) frame.retained[i]->Release();
        frame.retained.RemoveAll();
        frame.fence = 0;
    }
    {
        // The allocator's previous list completed on the GPU (fence above); reset runs in submission order.
        SubmitOpDX12 op{};op.kind=SubmitOpDX12::Reset;op.list=frame.list.Get();op.allocator=frame.allocator.Get();
        EnqueueSubmission(op);
        if (failed_) return false;
    }
    recording_ = true;
    return true;
}
void CShaderDeviceDX12::RetainResource(ID3D12Resource *resource)
{
    if (resource && recording_ && IsRecordingOwner())
    {
        resource->AddRef();
        frames_[frameIndex_].retained.AddToTail(resource);
    }
}
void CShaderDeviceDX12::QueueTextureDeletion(uintptr_t handle)
{
    if (!handle) return;
    AUTO_LOCK(textureDeletionMutex_);
    pendingTextureDeletions_.AddToTail(handle);
    pendingTextureDeletionCount_.store(pendingTextureDeletions_.Count(), std::memory_order_relaxed);
}
void CShaderDeviceDX12::TakeTextureDeletionRequests(CUtlVector<uintptr_t> &handles)
{
    handles.RemoveAll();
    AUTO_LOCK(textureDeletionMutex_);
    handles.Swap(pendingTextureDeletions_);
    pendingTextureDeletionCount_.store(0, std::memory_order_relaxed);
}

uint64_t CShaderDeviceDX12::Submit(bool wait)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 Submit", DX12_ZONES_ACTIVE);
    if (!recording_ || !IsRecordingOwner() || !queue_ || !fence_ || failed_) { Warning("ShaderAPIDX12: Submit rejected (wrong owner or no recording)\n"); return 0; }
    const bool timing = GpuTimingBeforeSubmit();
    FrameContext &frame = frames_[frameIndex_];
    const uint64_t value = fenceValue_ + 1;
    // Recorded commands replay, then Close/ExecuteCommandLists/Signal run, in FIFO order (inline without a worker).
    recorder_.Flush();
    SubmitOpDX12 op{};op.kind=SubmitOpDX12::Execute;op.list=frame.list.Get();op.value=value;
    EnqueueSubmission(op);
    if (failed_) return 0;
    recording_ = false;
    fenceValue_ = value;
    frame.fence = value;
    if (currentView_) currentView_->lastFence = value;
    if (wait)
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 Submit Wait", DX12_ZONES_ACTIVE);
        if (!WaitForFence(value)) return 0;
    }
    if (!BeginRecording()) return 0;
    if (timing) GpuTimingAfterSubmit();
    return value;
}
uint64_t CShaderDeviceDX12::SubmitFrameSync()
{
    if (!CommandList()) return 0;
    // Reference DX9 waits for the previous sync query, then issues this frame's query.
    // Current-frame completion remains mandatory in readback/resource lifecycle paths.
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 PreviousFrameWait", DX12_ZONES_ACTIVE);
        if (!WaitForFence(frameSyncFence_)) return 0;
    }
    const uint64_t value = Submit(false);
    if (value) frameSyncFence_ = value;
    return value;
}


bool CShaderDeviceDX12::WaitForFence(uint64_t value)
{
    if (!value || !fence_) return true;
    const uint64_t initial = fence_->GetCompletedValue();
    if (initial == UINT64_MAX) { FailDevice("fence device removal", device_ ? device_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED); return false; }
    if (initial >= value) return true;
    if (!CheckDevice("fence SetEventOnCompletion", fence_->SetEventOnCompletion(value, fenceEvent_))) return false;
    while (fence_->GetCompletedValue() < value)
    {
        const DWORD result = WaitForSingleObject(fenceEvent_, 5000);
        if (result == WAIT_OBJECT_0) continue;
        const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : E_FAIL;
        FailDevice(result == WAIT_TIMEOUT ? "fence wait timed out" : "fence wait failed", FAILED(reason) ? reason : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        return false;
    }
    const uint64_t completed=fence_->GetCompletedValue();if(completed==UINT64_MAX){const HRESULT reason=device_?device_->GetDeviceRemovedReason():DXGI_ERROR_DEVICE_REMOVED;FailDevice("fence device removal",reason);return false;}
    return fence_->GetCompletedValue() >= value;
}


CShaderDeviceDX12::View *CShaderDeviceDX12::CurrentView() const { return currentView_; }
ID3D12Resource *CShaderDeviceDX12::SceneColor() const { return currentView_ ? currentView_->sceneColor.Get() : nullptr; }
ID3D12Resource *CShaderDeviceDX12::SceneDepth() const { return currentView_ ? currentView_->sceneDepth.Get() : nullptr; }
D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::SceneRTV(bool srgb) const { auto handle=currentView_ && currentView_->sceneRTVHeap ? currentView_->sceneRTVStart : D3D12_CPU_DESCRIPTOR_HANDLE{};if(srgb&&handle.ptr)handle.ptr+=rtvStride_;return handle; }
D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::SceneDSV() const { return currentView_ && currentView_->sceneDSVHeap ? currentView_->sceneDSVStart : D3D12_CPU_DESCRIPTOR_HANDLE{}; }
uint32_t CShaderDeviceDX12::CurrentBackBufferIndex() const { return currentView_ && currentView_->swap ? currentView_->swap->GetCurrentBackBufferIndex() : 0; }
ID3D12Resource *CShaderDeviceDX12::CurrentBackBuffer() const { return currentView_ && currentView_->swap ? currentView_->backBuffers[CurrentBackBufferIndex()] : nullptr; }
D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::CurrentBackBufferRTV() const { auto h=currentView_ && currentView_->rtvHeap ? currentView_->rtvHeap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{}; h.ptr+=static_cast<SIZE_T>(CurrentBackBufferIndex())*rtvStride_; return h; }

void CShaderDeviceDX12::TransitionSceneColor(D3D12_RESOURCE_STATES state)
{
    if (!currentView_ || !SceneColor() || !CommandList() || currentView_->sceneColorState == state) return;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=SceneColor();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=currentView_->sceneColorState;barrier.Transition.StateAfter=state;
    recorder_.ResourceBarrier(1,&barrier);currentView_->sceneColorState=state;
}
void CShaderDeviceDX12::TransitionSceneDepth(D3D12_RESOURCE_STATES state)
{
    if (!currentView_ || !SceneDepth() || !CommandList() || currentView_->sceneDepthState == state) return;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=SceneDepth();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=currentView_->sceneDepthState;barrier.Transition.StateAfter=state;
    recorder_.ResourceBarrier(1,&barrier);currentView_->sceneDepthState=state;
}

bool CShaderDeviceDX12::CreateViewTargets(View &view)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors=backBufferCount_;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view.rtvHeap)))) return false;
    heap.NumDescriptors=2;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view.sceneRTVHeap)))) return false;
    heap.NumDescriptors=1;
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view.sceneDSVHeap)))) return false;
    view.ReleaseBackBuffers();
    view.backBuffers.SetCount(backBufferCount_);
    for (auto &buffer : view.backBuffers) buffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE handle=view.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i=0;i<backBufferCount_;++i)
    {
        HRESULT hr=view.swap->GetBuffer(i,IID_PPV_ARGS(&view.backBuffers[i]));
        if (FAILED(hr)) { Warning("ShaderAPIDX12: GetBuffer failed (0x%08x)\n",static_cast<unsigned>(hr)); return false; }
        device_->CreateRenderTargetView(view.backBuffers[i],nullptr,handle); handle.ptr+=rtvStride_;
    }
    D3D12_HEAP_PROPERTIES heapProperties{}; heapProperties.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resource{};resource.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;resource.Width=view.width;resource.Height=view.height;resource.DepthOrArraySize=1;resource.MipLevels=1;resource.SampleDesc.Count=sampleCount_;resource.SampleDesc.Quality=sampleQuality_;
    resource.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;resource.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE color{};color.Format=SceneColorFormat();color.Color[3]=1;
    HRESULT hr=device_->CreateCommittedResource(&heapProperties,D3D12_HEAP_FLAG_NONE,&resource,D3D12_RESOURCE_STATE_RENDER_TARGET,&color,IID_PPV_ARGS(&view.sceneColor));
    if (FAILED(hr)) { Warning("ShaderAPIDX12: Create scene color failed (0x%08x)\n",static_cast<unsigned>(hr)); return false; }
    view.sceneColorState=D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RENDER_TARGET_VIEW_DESC colorView{};colorView.ViewDimension=sampleCount_>1?D3D12_RTV_DIMENSION_TEXTURE2DMS:D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto sceneViewStart=view.sceneRTVHeap->GetCPUDescriptorHandleForHeapStart();view.sceneRTVStart=sceneViewStart;
    colorView.Format=SceneColorFormat(false);device_->CreateRenderTargetView(view.sceneColor.Get(),&colorView,sceneViewStart);
    colorView.Format=SceneColorFormat(true);D3D12_CPU_DESCRIPTOR_HANDLE gammaView{sceneViewStart.ptr+rtvStride_};device_->CreateRenderTargetView(view.sceneColor.Get(),&colorView,gammaView);
    resource.Format=SceneDepthFormat();resource.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depth{};depth.Format=resource.Format;depth.DepthStencil.Depth=1;
    hr=device_->CreateCommittedResource(&heapProperties,D3D12_HEAP_FLAG_NONE,&resource,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depth,IID_PPV_ARGS(&view.sceneDepth));
    if (FAILED(hr)) { Warning("ShaderAPIDX12: Create scene depth failed (0x%08x)\n",static_cast<unsigned>(hr)); return false; }
    view.sceneDepthState=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    view.sceneDSVStart=view.sceneDSVHeap->GetCPUDescriptorHandleForHeapStart();
    device_->CreateDepthStencilView(view.sceneDepth.Get(),nullptr,view.sceneDSVStart);
    return true;
}

bool CShaderDeviceDX12::CreateView(View &view, HWND hwnd, int width, int height)
{
    view.hwnd=hwnd;view.width=width;view.height=height;
    if (width<=0 || height<=0) { view.suspended=true; return true; }
    view.suspended=false;view.occluded=false;
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=SceneColorFormat();desc.BufferCount=backBufferCount_;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.SampleDesc.Count=1;
    if (allowTearing_) desc.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
    HRESULT hr=factory_->CreateSwapChainForHwnd(queue_.Get(),hwnd,&desc,nullptr,nullptr,&swap);
    if (FAILED(hr)) { Warning("ShaderAPIDX12: CreateSwapChainForHwnd failed (0x%08x)\n",static_cast<unsigned>(hr)); return false; }
    if (FAILED(swap.As(&view.swap))) return false;
    factory_->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
    if (!windowed_ && !CheckDevice("SetFullscreenState",view.swap->SetFullscreenState(TRUE,nullptr))) return false;
    return CreateViewTargets(view);
}

bool CShaderDeviceDX12::ResizeView(View &view, int width, int height)
{
    FlushSubmissions();
    if (width<=0 || height<=0) { view.suspended=true; return true; }
    if (view.swap && !view.suspended && width==view.width && height==view.height) return true;
    if (!changingMode_ && g_pShaderDeviceDX12==this && view.swap && (width!=view.width || height!=view.height)) { changingMode_=true; if(g_pShaderDeviceMgrDX12)g_pShaderDeviceMgrDX12->NotifyModeChange(); changingMode_=false; }
    if (recording_ && !Submit(false)) return false;
    if (!WaitForFence(view.lastFence)) return false;
    view.sceneColor.Reset();view.sceneDepth.Reset();view.ReleaseBackBuffers();view.rtvHeap.Reset();view.sceneRTVHeap.Reset();view.sceneDSVHeap.Reset();
    view.width=width;view.height=height;view.suspended=false;view.occluded=false;
    if (!view.swap) return CreateView(view,view.hwnd,width,height);
    DXGI_SWAP_CHAIN_DESC1 old{}; if (FAILED(view.swap->GetDesc1(&old))) return false;
    HRESULT hr=view.swap->ResizeBuffers(backBufferCount_,width,height,SceneColorFormat(),old.Flags);
    if (!CheckDevice("ResizeBuffers",hr)) return false;
    return CreateViewTargets(view);
}

bool CShaderDeviceDX12::ChangeMode(const ShaderDeviceInfo_t &info)
{
    if (!IsRecordingOwner()) return false;
    FlushSubmissions();
    if (!SupportsMSAA(std::max(1,info.m_nAASamples),info.m_nAAQuality)) return false;
    RECT newRect{};if(currentView_)GetClientRect(currentView_->hwnd,&newRect);
    const int newWidth=info.m_DisplayMode.m_nWidth>0?info.m_DisplayMode.m_nWidth:newRect.right-newRect.left;
    const int newHeight=info.m_DisplayMode.m_nHeight>0?info.m_DisplayMode.m_nHeight:newRect.bottom-newRect.top;
    const bool changesResources=info.m_bWindowed!=windowed_||std::max(1,info.m_nAASamples)!=sampleCount_||info.m_nAAQuality!=sampleQuality_||std::clamp(info.m_nBackBufferCount,1,2)+1!=backBufferCount_||(currentView_&&(newWidth!=currentView_->width||newHeight!=currentView_->height));
    changingMode_=true;struct ChangeScope{bool &flag;~ChangeScope(){flag=false;}} changeScope{changingMode_};
    if(changesResources&&g_pShaderDeviceMgrDX12)g_pShaderDeviceMgrDX12->NotifyModeChange();
    if (recording_ && !Submit(true)) return false;
    for (auto &view:views_) if (!WaitForFence(view->lastFence)) return false;
    const bool fullscreen=!info.m_bWindowed;
    for (auto &view:views_) if (view->swap && windowed_ != info.m_bWindowed && !CheckDevice("SetFullscreenState",view->swap->SetFullscreenState(fullscreen,nullptr))) return false;
    windowed_=info.m_bWindowed;waitForVsync_=info.m_bWaitForVSync;
    const int samples=std::max(1,info.m_nAASamples),quality=info.m_nAAQuality;
    const int buffers=std::clamp(info.m_nBackBufferCount, 1, 2)+1;
    if (samples != sampleCount_ || quality != sampleQuality_ || buffers != backBufferCount_)
    {
        sampleCount_=samples;sampleQuality_=quality;backBufferCount_=buffers;
        for (auto &view:views_) {view->sceneColor.Reset();view->sceneDepth.Reset();view->ReleaseBackBuffers();view->rtvHeap.Reset();view->sceneRTVHeap.Reset();view->sceneDSVHeap.Reset();if(view->swap){view->swap.Reset();}if(!CreateView(*view,view->hwnd,view->width,view->height)) return false;}
    }
    if (currentView_) { RECT rect{};GetClientRect(currentView_->hwnd,&rect);const int w=info.m_DisplayMode.m_nWidth>0?info.m_DisplayMode.m_nWidth:rect.right-rect.left;const int h=info.m_DisplayMode.m_nHeight>0?info.m_DisplayMode.m_nHeight:rect.bottom-rect.top;if(!ResizeView(*currentView_,w,h))return false;width_=currentView_->width;height_=currentView_->height; }
    return true;
}

void CCommandRecorderDX12::Replay(ID3D12GraphicsCommandList *list,ID3D12Device *device,const unsigned char *data,size_t size)
{
    ZoneNamedN(replayZone, "DX12 CommandReplay", DX12_ZONES_ACTIVE);
    size_t offset=0;
    while(offset<size)
    {
        Header header;std::memcpy(&header,data+offset,sizeof(header));
        const unsigned char *p=data+offset+sizeof(Header);offset+=header.size;
        const auto get=[&](auto &value){std::memcpy(&value,p,sizeof(value));p+=sizeof(value);};
        switch(header.op)
        {
        case Op::OMSetRenderTargets:{UINT count,single,stored,hasDepth;get(count);get(single);get(stored);get(hasDepth);D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8];std::memcpy(rtvs,p,stored*sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));p+=stored*sizeof(D3D12_CPU_DESCRIPTOR_HANDLE);D3D12_CPU_DESCRIPTOR_HANDLE dsv;get(dsv);list->OMSetRenderTargets(count,stored?rtvs:nullptr,single,hasDepth?&dsv:nullptr);break;}
        case Op::OMSetStencilRef:{UINT value;get(value);list->OMSetStencilRef(value);break;}
        case Op::RSSetViewports:{UINT count;get(count);D3D12_VIEWPORT viewports[16];std::memcpy(viewports,p,count*sizeof(D3D12_VIEWPORT));list->RSSetViewports(count,viewports);break;}
        case Op::RSSetScissorRects:{UINT count;get(count);D3D12_RECT rects[16];std::memcpy(rects,p,count*sizeof(D3D12_RECT));list->RSSetScissorRects(count,rects);break;}
        case Op::SetGraphicsRootSignature:{ID3D12RootSignature *root;get(root);list->SetGraphicsRootSignature(root);break;}
        case Op::SetGraphicsRootDescriptorTable:{UINT index;D3D12_GPU_DESCRIPTOR_HANDLE table;get(index);get(table);list->SetGraphicsRootDescriptorTable(index,table);break;}
        case Op::SetGraphicsRootConstantBufferView:{UINT index;D3D12_GPU_VIRTUAL_ADDRESS address;get(index);get(address);list->SetGraphicsRootConstantBufferView(index,address);break;}
        case Op::SetGraphicsRoot32BitConstants:{UINT index,count,first;get(index);get(count);get(first);UINT values[64];std::memcpy(values,p,(count<64?count:64)*4);list->SetGraphicsRoot32BitConstants(index,count,values,first);break;}
        case Op::SetPipelineState:{ID3D12PipelineState *pso;get(pso);list->SetPipelineState(pso);break;}
        case Op::SetDescriptorHeaps:{UINT count;get(count);ID3D12DescriptorHeap *heaps[2]={};std::memcpy(heaps,p,count*sizeof(void *));list->SetDescriptorHeaps(count,heaps);break;}
        case Op::IASetVertexBuffers:{UINT start,count,stored;get(start);get(count);get(stored);D3D12_VERTEX_BUFFER_VIEW views[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];std::memcpy(views,p,stored*sizeof(D3D12_VERTEX_BUFFER_VIEW));list->IASetVertexBuffers(start,count,stored?views:nullptr);break;}
        case Op::IASetIndexBuffer:{UINT present;D3D12_INDEX_BUFFER_VIEW view;get(present);get(view);list->IASetIndexBuffer(present?&view:nullptr);break;}
        case Op::IASetPrimitiveTopology:{D3D12_PRIMITIVE_TOPOLOGY topology;get(topology);list->IASetPrimitiveTopology(topology);break;}
        case Op::DrawInstanced:{UINT a,b,c,d;get(a);get(b);get(c);get(d);list->DrawInstanced(a,b,c,d);break;}
        case Op::DrawIndexedInstanced:{UINT a,b,c,e;INT d;get(a);get(b);get(c);get(d);get(e);list->DrawIndexedInstanced(a,b,c,d,e);break;}
        case Op::ResourceBarrier:{UINT count;get(count);D3D12_RESOURCE_BARRIER barriers[16];for(UINT done=0;done<count;){const UINT n=(count-done)<16?count-done:16;std::memcpy(barriers,p+done*sizeof(D3D12_RESOURCE_BARRIER),n*sizeof(D3D12_RESOURCE_BARRIER));list->ResourceBarrier(n,barriers);done+=n;}break;}
        case Op::CopyBufferRegion:{ID3D12Resource *dst,*src;UINT64 dstOffset,srcOffset,bytes;get(dst);get(dstOffset);get(src);get(srcOffset);get(bytes);list->CopyBufferRegion(dst,dstOffset,src,srcOffset,bytes);break;}
        case Op::CopyTextureRegion:{D3D12_TEXTURE_COPY_LOCATION dst,src;UINT x,y,z,hasBox;D3D12_BOX box;get(dst);get(x);get(y);get(z);get(src);get(hasBox);get(box);list->CopyTextureRegion(&dst,x,y,z,&src,hasBox?&box:nullptr);break;}
        case Op::CopyResource:{ID3D12Resource *dst,*src;get(dst);get(src);list->CopyResource(dst,src);break;}
        case Op::ResolveSubresource:{ID3D12Resource *dst,*src;UINT dstSub,srcSub;DXGI_FORMAT format;get(dst);get(dstSub);get(src);get(srcSub);get(format);list->ResolveSubresource(dst,dstSub,src,srcSub,format);break;}
        case Op::ClearRenderTargetView:{D3D12_CPU_DESCRIPTOR_HANDLE rtv;FLOAT color[4];UINT count,stored;get(rtv);get(color);get(count);get(stored);D3D12_RECT rects[16];std::memcpy(rects,p,(stored<16?stored:16)*sizeof(D3D12_RECT));list->ClearRenderTargetView(rtv,color,count,stored?rects:nullptr);break;}
        case Op::ClearDepthStencilView:{D3D12_CPU_DESCRIPTOR_HANDLE dsv;D3D12_CLEAR_FLAGS flags;FLOAT depth;UINT stencil,count,stored;get(dsv);get(flags);get(depth);get(stencil);get(count);get(stored);D3D12_RECT rects[16];std::memcpy(rects,p,(stored<16?stored:16)*sizeof(D3D12_RECT));list->ClearDepthStencilView(dsv,flags,depth,static_cast<UINT8>(stencil),count,stored?rects:nullptr);break;}
        case Op::BeginQuery:{ID3D12QueryHeap *heap;D3D12_QUERY_TYPE type;UINT index;get(heap);get(type);get(index);list->BeginQuery(heap,type,index);break;}
        case Op::EndQuery:{ID3D12QueryHeap *heap;D3D12_QUERY_TYPE type;UINT index;get(heap);get(type);get(index);list->EndQuery(heap,type,index);break;}
        case Op::CopyDescriptorTable:{D3D12_CPU_DESCRIPTOR_HANDLE destination;UINT count,pad;get(destination);get(count);get(pad);D3D12_CPU_DESCRIPTOR_HANDLE sources[32];const UINT n=count<32?count:32;std::memcpy(sources,p,n*sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));device->CopyDescriptors(1,&destination,&n,n,sources,nullptr,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);break;}
        case Op::ResolveQueryData:{ID3D12QueryHeap *heap;D3D12_QUERY_TYPE type;UINT start,count;ID3D12Resource *dst;UINT64 destOffset;get(heap);get(type);get(start);get(count);get(dst);get(destOffset);list->ResolveQueryData(heap,type,start,count,dst,destOffset);break;}
        }
    }
}
void CShaderDeviceDX12::ReleaseViews() { FlushSubmissions(); for (auto &view:views_) { if (view->swap && !windowed_) view->swap->SetFullscreenState(FALSE,nullptr); } views_.PurgeAndDeleteElements();currentView_=nullptr; }
HRESULT CShaderDeviceDX12::RunSubmission(const SubmitOpDX12 &op)
{
    HRESULT hr=S_OK;
    switch(op.kind)
    {
    case SubmitOpDX12::Replay:
        CCommandRecorderDX12::Replay(op.list,device_.Get(),op.chunk,op.chunkBytes);
        {AUTO_LOCK(chunkMutex_);freeChunks_.AddToTail(op.chunk);}
        break;
    case SubmitOpDX12::Reset:
        hr=op.allocator->Reset();
        if(SUCCEEDED(hr))hr=op.list->Reset(op.allocator,nullptr);
        break;
    case SubmitOpDX12::Execute:
    {
        ZoneNamedN(submitZone, "DX12 Submit Execute", DX12_ZONES_ACTIVE);
        hr=op.list->Close();
        if(SUCCEEDED(hr)){ID3D12CommandList *lists[]={op.list};queue_->ExecuteCommandLists(1,lists);}
        // Signal even after a failed Close so fence waits cannot hang; the error is reported at the next flush.
        const HRESULT signal=queue_->Signal(fence_.Get(),op.value);
        if(SUCCEEDED(hr))hr=signal;
        break;
    }
    case SubmitOpDX12::Present:
    {
        ZoneNamedN(nativePresent, "DX12 DXGI Present", DX12_ZONES_ACTIVE);
        hr=op.swap->Present(op.interval,op.flags);
        if(hr==DXGI_STATUS_OCCLUDED){op.view->occluded=true;hr=S_OK;}
        break;
    }
    }
    return hr;
}
unsigned char *CShaderDeviceDX12::AcquireCommandChunk()
{
    for(;;)
    {
        {
            AUTO_LOCK(chunkMutex_);
            if(freeChunks_.Count()){unsigned char *chunk=freeChunks_.Tail();freeChunks_.RemoveMultipleFromTail(1);return chunk;}
            if(allChunks_.Count()<kMaxCommandChunks||!submitThread_){auto *chunk=static_cast<unsigned char *>(MemAlloc_AllocAligned(CCommandRecorderDX12::kChunkBytes,64));allChunks_.AddToTail(chunk);return chunk;}
        }
        // Every chunk is queued: wait for the worker to replay one.
        submitDoneEvent_.Wait();
    }
}
void CShaderDeviceDX12::FlushCommandChunk(unsigned char *chunk,size_t bytes)
{
    SubmitOpDX12 op{};op.kind=SubmitOpDX12::Replay;op.list=frames_[frameIndex_].list.Get();op.chunk=chunk;op.chunkBytes=bytes;
    EnqueueSubmission(op);
}
void CShaderDeviceDX12::ReleaseCommandChunks()
{
    AUTO_LOCK(chunkMutex_);
    for(auto *chunk:allChunks_)MemAlloc_FreeAligned(chunk);
    allChunks_.RemoveAll();freeChunks_.RemoveAll();
}
uintp CShaderDeviceDX12::SubmitThreadMain(void *param)
{
    auto &device=*static_cast<CShaderDeviceDX12 *>(param);
    for(;;)
    {
        device.submitWorkEvent_.Wait();
        for(;;)
        {
            const uint32_t tail=device.submitTail_.load(std::memory_order_relaxed);
            if(tail==device.submitHead_.load(std::memory_order_acquire))break;
            auto &op=device.submitOps_[tail%device.submitOps_.size()];
            const HRESULT hr=device.RunSubmission(op);
            if(FAILED(hr)){HRESULT expected=S_OK;device.submitError_.compare_exchange_strong(expected,hr);}
            device.submitTail_.store(tail+1,std::memory_order_release);
            device.submitDoneEvent_.Set();
        }
        if(device.submitExit_.load(std::memory_order_acquire))break;
    }
    return 0;
}
void CShaderDeviceDX12::StartSubmitThread()
{
    if(submitThread_||(CommandLine()&&CommandLine()->CheckParm("-dx12syncsubmit")))return;
    submitExit_.store(false,std::memory_order_release);submitHead_.store(0);submitTail_.store(0);submitError_.store(S_OK);
    submitThread_=CreateSimpleThread(&CShaderDeviceDX12::SubmitThreadMain,this);
}
void CShaderDeviceDX12::StopSubmitThread()
{
    if(!submitThread_)return;
    FlushSubmissions();
    submitExit_.store(true,std::memory_order_release);submitWorkEvent_.Set();
    ThreadJoin(submitThread_);ReleaseThreadHandle(submitThread_);submitThread_=nullptr;
}
void CShaderDeviceDX12::EnqueueSubmission(const SubmitOpDX12 &op)
{
    if(!submitThread_){CheckDevice("submission",RunSubmission(op));return;}
    const uint32_t head=submitHead_.load(std::memory_order_relaxed);
    while(head-submitTail_.load(std::memory_order_acquire)>=submitOps_.size())submitDoneEvent_.Wait();
    submitOps_[head%submitOps_.size()]=op;
    submitHead_.store(head+1,std::memory_order_release);
    submitWorkEvent_.Set();
}
void CShaderDeviceDX12::FlushSubmissions()
{
    if(!submitThread_)return;
    if(submitTail_.load(std::memory_order_acquire)!=submitHead_.load(std::memory_order_relaxed))
    {
        ZoneNamedN(presentWait, "DX12 SubmitThreadWait", DX12_ZONES_ACTIVE);
        DrainSubmissions();
    }
    const HRESULT hr=submitError_.exchange(S_OK);
    if(FAILED(hr))CheckDevice("queued submission",hr);
}
void CShaderDeviceDX12::ShutdownDevice()
{
    StopSubmitThread();
    if (recording_ && IsRecordingOwner() && queue_ && fence_ && !failed_) Submit(true);
    if (queue_ && fence_ && fenceValue_ && !failed_) WaitForFence(fenceValue_);
    ReleaseViews();
    for (auto &buffer : dynamicVertices_) if (buffer) buffer->NativeResourceRef().Reset();
    for (auto &buffer : dynamicIndices_) if (buffer) buffer->NativeResourceRef().Reset();
    if (fenceEvent_) {CloseHandle(fenceEvent_);fenceEvent_=nullptr;}
    if(timestampReadback_)timestampReadback_->Unmap(0,nullptr);timestampReadback_.Reset();timestampHeap_.Reset();timestampData_=nullptr;timestampBegun_=false;
    recorder_.Flush();if(auto *chunk=recorder_.TakeEmptyChunk()){AUTO_LOCK(chunkMutex_);freeChunks_.AddToTail(chunk);}
    ReleaseCommandChunks();
    for(auto &frame:frames_){frame.list.Reset();for(int i=0;i<frame.retained.Count();++i)frame.retained[i]->Release();frame.retained.RemoveAll();frame.allocator.Reset();frame.fence=0;}
    fence_.Reset();queue_.Reset();device_.Reset();factory_.Reset();
    if(signerModule_){FreeLibrary(signerModule_);signerModule_=nullptr;}signer_=nullptr;
    fenceValue_=frameSyncFence_=0;frameIndex_=0;recording_=false;failed_=false;ownerThread_=0;window_=nullptr;width_=height_=0;
    if(g_pShaderDeviceDX12==this)g_pShaderDeviceDX12=nullptr;
}

void CShaderDeviceDX12::ReleaseResources()
{
    if (!IsRecordingOwner())
    {
        if (g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil()) g_pShaderDeviceMgrDX12->HostShaderUtil()->OnThreadEvent(SHADER_THREAD_RELEASE_RESOURCES);
        else Warning("ShaderAPIDX12: cannot queue resource release without host shader utility\n");
        return;
    }
    FlushSubmissions();
    if (recording_) Submit(true);
    for (auto &view:views_) { if (!WaitForFence(view->lastFence)) return;view->sceneColor.Reset();view->sceneDepth.Reset();view->ReleaseBackBuffers();view->rtvHeap.Reset();view->sceneRTVHeap.Reset();view->sceneDSVHeap.Reset();if(view->swap && !windowed_)view->swap->SetFullscreenState(FALSE,nullptr);view->swap.Reset(); }
}

void CShaderDeviceDX12::ReacquireResources()
{
    if (!IsRecordingOwner())
    {
        if (g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil()) g_pShaderDeviceMgrDX12->HostShaderUtil()->OnThreadEvent(SHADER_THREAD_ACQUIRE_RESOURCES);
        else Warning("ShaderAPIDX12: cannot queue resource acquisition without host shader utility\n");
        return;
    }
    for (auto &view:views_) if (!view->swap) {RECT rect{};GetClientRect(view->hwnd,&rect);if(!CreateView(*view,view->hwnd,rect.right-rect.left,rect.bottom-rect.top)) { FailDevice("reacquire view",E_FAIL);return; }}
}

void CShaderDeviceDX12::SpewDriverInfo() const
{
    if(!device_)return;
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    levels.NumFeatureLevels=ARRAYSIZE(requested);levels.pFeatureLevelsRequested=requested;
    Msg("ShaderAPIDX12: native feature level 0x%x\n",SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,&levels,sizeof(levels)))?levels.MaxSupportedFeatureLevel:D3D_FEATURE_LEVEL_11_0);
}

bool CShaderDeviceDX12::GpuTimingBeforeSubmit()
{
    static const bool enabled=CommandLine()&&CommandLine()->CheckParm("-dx12stats");
    if(!enabled)return false;
    if(!timestampHeap_)
    {
        D3D12_QUERY_HEAP_DESC heapDesc{};heapDesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;heapDesc.Count=kTimestampSlots*2;
        if(FAILED(device_->CreateQueryHeap(&heapDesc,IID_PPV_ARGS(&timestampHeap_))))return false;
        D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=kTimestampSlots*2*sizeof(uint64_t);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timestampReadback_)))){timestampHeap_.Reset();return false;}
        void *mapped=nullptr;if(FAILED(timestampReadback_->Map(0,nullptr,&mapped))){timestampHeap_.Reset();timestampReadback_.Reset();return false;}
        timestampData_=static_cast<const uint64_t *>(mapped);
        FlushSubmissions();if(FAILED(queue_->GetTimestampFrequency(&timestampFrequency_))||!timestampFrequency_){timestampHeap_.Reset();return false;}
        timestampFences_.fill(0);timestampSlot_=0;timestampBegun_=false;
    }
    const uint64_t completed=CompletedFenceValue();
    for(uint32_t slot=0;slot<kTimestampSlots;++slot)if(timestampFences_[slot]&&timestampFences_[slot]<=completed)
    {
        const uint64_t begin=timestampData_[slot*2],end=timestampData_[slot*2+1];
        if(end>begin)gpuTimeSumMs_+=1000.0*static_cast<double>(end-begin)/static_cast<double>(timestampFrequency_);
        timestampFences_[slot]=0;
    }
    if(timestampBegun_)
    {
        const uint32_t slot=timestampSlot_;
        recorder_.EndQuery(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2+1);
        recorder_.ResolveQueryData(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2,2,timestampReadback_.Get(),slot*2*sizeof(uint64_t));
        timestampFences_[slot]=NextFenceValue();timestampSlot_=(slot+1)%kTimestampSlots;timestampBegun_=false;
    }
    return timestampFences_[timestampSlot_]==0;
}
void CShaderDeviceDX12::GpuTimingAfterSubmit()
{
    // The next list's first command starts its GPU interval; intervals sum per presented frame.
    recorder_.EndQuery(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timestampSlot_*2);timestampBegun_=true;
}
bool CShaderDeviceDX12::ConsumeGpuTime(double &averageMs,uint32_t &frames)
{
    frames=gpuTimePresented_;averageMs=frames?gpuTimeSumMs_/frames:0.0;gpuTimeSumMs_=0.0;gpuTimePresented_=0;return frames!=0;
}
void CShaderDeviceDX12::Present()
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 Present", DX12_ZONES_ACTIVE);
    if (!IsRecordingOwner() || !currentView_ || !CommandList()) return;
    FlushSubmissions();
    View &view=*currentView_;
    RECT rect{};GetClientRect(view.hwnd,&rect);
    const int w=rect.right-rect.left,h=rect.bottom-rect.top;
    if (w<=0 || h<=0 || IsIconic(view.hwnd)) {view.suspended=true;Submit(false);return;}
    if (!ResizeView(view,w,h)) return;
    width_=view.width;height_=view.height;
    if (!view.sceneColor || !view.swap) return;
    if (view.occluded)
    {
        const HRESULT test=view.swap->Present(0,DXGI_PRESENT_TEST);
        if (test==DXGI_STATUS_OCCLUDED) {Submit(false);return;}
        if (!CheckDevice("occlusion test",test)) return;
        view.occluded=false;
    }
    const bool correctGamma = windowed_ && (gammaTV_ || gamma_ != 2.2f);
    auto *back=CurrentBackBuffer();
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=back;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter=correctGamma?D3D12_RESOURCE_STATE_RENDER_TARGET:(sampleCount_>1?D3D12_RESOURCE_STATE_RESOLVE_DEST:D3D12_RESOURCE_STATE_COPY_DEST);
    recorder_.ResourceBarrier(1,&barrier);
    if (correctGamma)
    {
        if (!g_pShaderAPIDX12 || !g_pShaderAPIDX12->PresentGamma(back, gamma_, gammaMin_, gammaMax_, gammaExponent_, gammaTV_))
        {
            FailDevice("windowed gamma presentation", E_FAIL);
            return;
        }
    }
    else if (sampleCount_ > 1)
    {
        TransitionSceneColor(D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        recorder_.ResolveSubresource(back, 0, view.sceneColor.Get(), 0, SceneColorFormat());
    }
    else
    {
        TransitionSceneColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        recorder_.CopyResource(back, view.sceneColor.Get());
    }
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);recorder_.ResourceBarrier(1,&barrier);
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(!Submit(false))return;
    ++gpuTimePresented_;
    const UINT interval=waitForVsync_?1:0;
    const UINT flags=!waitForVsync_ && allowTearing_ && windowed_ ? DXGI_PRESENT_ALLOW_TEARING : 0;
    if(submitThread_)
    {
        // Present runs on the submission worker after this frame's ExecuteCommandLists. Every later
        // swap-chain or queue access on this thread calls FlushPresent/FlushSubmissions first.
        SubmitOpDX12 op{};op.kind=SubmitOpDX12::Present;op.swap=view.swap.Get();op.view=&view;op.interval=interval;op.flags=flags;
        EnqueueSubmission(op);
    }
    else
    {
        HRESULT hr;
        { ZoneNamedN(nativePresent, "DX12 DXGI Present", DX12_ZONES_ACTIVE); hr=view.swap->Present(interval,flags); }
        if(hr==DXGI_STATUS_OCCLUDED){view.occluded=true;return;}
        CheckDevice("Present",hr);
    }
#ifdef TRACY_ENABLE
    if(TracyIsStarted) { FrameMark; }
#endif
    RefreshTracyZonesDX12();
}

void CShaderDeviceDX12::SetHardwareGammaRamp(float fGamma,float fGammaTVRangeMin,float fGammaTVRangeMax,float fGammaTVExponent,bool bTVEnabled)
{
    gamma_=fGamma;gammaMin_=fGammaTVRangeMin;gammaMax_=fGammaTVRangeMax;gammaExponent_=fGammaTVExponent;gammaTV_=bTVEnabled;
    // DXGI output gamma is defined only for exclusive fullscreen presentation.
    FlushSubmissions();
    if (windowed_ || !currentView_ || !currentView_->swap) return;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    HRESULT hr = currentView_->swap->GetContainingOutput(&output);
    DXGI_GAMMA_CONTROL_CAPABILITIES caps{};
    if (SUCCEEDED(hr)) hr = output->GetGammaControlCapabilities(&caps);
    if (FAILED(hr)) { Warning("ShaderAPIDX12: gamma capabilities failed (0x%08x)\n", static_cast<unsigned>(hr)); return; }
    DXGI_GAMMA_CONTROL ramp{};
    ramp.Scale = {1, 1, 1};
    for (UINT i = 0; i < caps.NumGammaControlPoints; ++i)
    {
        float correction = std::pow(std::clamp(caps.ControlPointPositions[i], 0.0f, 1.0f), fGamma / 2.2f);
        if (bTVEnabled)
        {
            correction = std::pow(correction, 2.2f / fGammaTVExponent);
            correction = correction * (fGammaTVRangeMax - fGammaTVRangeMin) / 255.0f + fGammaTVRangeMin / 255.0f;
        }
        correction = std::clamp(correction, caps.MinConvertedValue, caps.MaxConvertedValue);
        ramp.GammaCurve[i] = {correction, correction, correction};
    }
    hr = output->SetGammaControl(&ramp);
    if (FAILED(hr)) Warning("ShaderAPIDX12: SetGammaControl failed (0x%08x)\n", static_cast<unsigned>(hr));
}

bool CShaderDeviceDX12::AddView(void *hwnd)
{
    if(!IsRecordingOwner() || !hwnd || !IsWindow(static_cast<HWND>(hwnd)))return false;
    FlushSubmissions();
    for(const auto &view:views_)if(view->hwnd==hwnd)return true;
    RECT rect{};GetClientRect(static_cast<HWND>(hwnd),&rect);
    View *view=new View;
    if(!CreateView(*view,static_cast<HWND>(hwnd),rect.right-rect.left,rect.bottom-rect.top)){delete view;return false;}
    if(!currentView_){currentView_=view;width_=view->width;height_=view->height;}
    views_.AddToTail(view);return true;
}
void CShaderDeviceDX12::RemoveView(void *hwnd)
{
    if(!IsRecordingOwner())return;
    FlushSubmissions();
    for(int i=0;i<views_.Count();++i)if(views_[i]->hwnd==hwnd)
    {
        View *view=views_[i];if(recording_)Submit(false);
        if(!WaitForFence(view->lastFence))return;
        if(view->swap && !windowed_)view->swap->SetFullscreenState(FALSE,nullptr);
        if(currentView_==view)currentView_=nullptr;
        delete view;views_.Remove(i);if(!currentView_ && !views_.IsEmpty())currentView_=views_[0];
        width_=currentView_?currentView_->width:0;height_=currentView_?currentView_->height:0;
        return;
    }
}

void CShaderDeviceDX12::SetView(void *hwnd)
{
    if(!IsRecordingOwner())return;
    FlushSubmissions();
    if(!hwnd)hwnd=window_;
    bool found=false;for(const auto &view:views_)if(view->hwnd==hwnd){found=true;break;}
    if(!found&&hwnd&&IsWindow(static_cast<HWND>(hwnd))){if(!AddView(hwnd))return;found=true;}
    if(!found){Warning("ShaderAPIDX12: SetView requires a valid HWND\n");return;}
    for(auto &view:views_)if(view->hwnd==hwnd)
    {
        if(view!=currentView_ && recording_ && !Submit(false))return;
        currentView_=view;width_=view->width;height_=view->height;return;
    }
}

bool CShaderDeviceDX12::AcquireRecordingOwnership()
{
    const DWORD thread = GetCurrentThreadId();
    DWORD expected = 0;
    if (ownerThread_.compare_exchange_strong(expected, thread, std::memory_order_acq_rel) || expected == thread) return true;
    Warning("ShaderAPIDX12: recording owner still active on another thread\n");
    return false;
}
void CShaderDeviceDX12::ReleaseRecordingOwnership()
{
    if(!IsRecordingOwner()) {Warning("ShaderAPIDX12: release recording owner from wrong thread\n");return;}
    FlushSubmissions();
    if(recording_)Submit(true);
    ownerThread_.store(0, std::memory_order_release);
}
IShaderBuffer *CShaderDeviceDX12::CompileShader(const char *pProgram,size_t nBufLen,const char *pShaderVersion)
{
    if (!pProgram || !nBufLen) return nullptr;
    if (nBufLen >= 4 && std::memcmp(pProgram, "DXBC", 4) == 0)
        return new CShaderBufferDX12(pProgram, nBufLen);
    if (!pShaderVersion || !*pShaderVersion) return nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const bool legacy = std::strlen(pShaderVersion) > 3 && pShaderVersion[3] < '4';
    const UINT flags = (legacy ? D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY : D3DCOMPILE_ENABLE_STRICTNESS) | ((g_pHardwareConfigDX12 && g_pHardwareConfigDX12->DisableShaderOptimizations()) ? D3DCOMPILE_SKIP_OPTIMIZATION : 0);
    HRESULT hr = D3DCompile(pProgram, nBufLen, "shaderapidx12", nullptr, nullptr, "main", pShaderVersion,
        flags, 0, &code, &errors);
    if (FAILED(hr))
    {
        if (errors) Warning("ShaderAPIDX12: shader compile failed: %s\n", static_cast<const char *>(errors->GetBufferPointer()));
        else Warning("ShaderAPIDX12: shader compile failed (0x%08x)\n", static_cast<unsigned>(hr));
        return nullptr;
    }
    return new CShaderBufferDX12(code->GetBufferPointer(), code->GetBufferSize());
}

namespace
{
template <typename T> T *CreateShaderRecord(IShaderBuffer *buffer,bool pixel)
{
    if (!buffer || !buffer->GetBits() || buffer->GetSize() < 4) return nullptr;
    const unsigned char *bits = static_cast<const unsigned char *>(buffer->GetBits());
    static std::atomic<uint64_t> nextIdentity{1};T *record = new T;record->identity=nextIdentity.fetch_add(1,std::memory_order_relaxed);record->stagePixel=pixel;
    if (std::memcmp(bits,"DXBC",4)==0) record->bytecode.assign(bits,bits+buffer->GetSize());
    else record->legacyBytecode.assign(bits,bits+buffer->GetSize());
    return record;
}
} // namespace

VertexShaderHandle_t CShaderDeviceDX12::CreateVertexShader(IShaderBuffer *buffer)
{
    return reinterpret_cast<VertexShaderHandle_t>(CreateShaderRecord<ShaderRecordDX12>(buffer,false));
}
void CShaderDeviceDX12::DestroyVertexShader(VertexShaderHandle_t hShader) { auto *record=reinterpret_cast<ShaderRecordDX12 *>(hShader);if(g_pShaderAPIDX12)g_pShaderAPIDX12->RetireShaderPipelines(record);delete record; }
GeometryShaderHandle_t CShaderDeviceDX12::CreateGeometryShader(IShaderBuffer *buffer)
{
    ShaderRecordDX12 *record=CreateShaderRecord<ShaderRecordDX12>(buffer,false);if(record)record->stageGeometry=true;return reinterpret_cast<GeometryShaderHandle_t>(record);
}
void CShaderDeviceDX12::DestroyGeometryShader(GeometryShaderHandle_t hShader) { auto *record=reinterpret_cast<ShaderRecordDX12 *>(hShader);if(g_pShaderAPIDX12)g_pShaderAPIDX12->RetireShaderPipelines(record);delete record; }
PixelShaderHandle_t CShaderDeviceDX12::CreatePixelShader(IShaderBuffer *buffer)
{
    return reinterpret_cast<PixelShaderHandle_t>(CreateShaderRecord<ShaderRecordDX12>(buffer,true));
}
void CShaderDeviceDX12::DestroyPixelShader(PixelShaderHandle_t hShader) { auto *record=reinterpret_cast<ShaderRecordDX12 *>(hShader);if(g_pShaderAPIDX12)g_pShaderAPIDX12->RetireShaderPipelines(record);delete record; }

IMesh *CShaderDeviceDX12::CreateStaticMesh(VertexFormat_t format,const char *,IMaterial *)
{
    return new CMeshDX12(format,0,false,+[](void *, CMeshDX12 *mesh,int first,int count){if(g_pShaderAPIDX12)g_pShaderAPIDX12->DrawMaterialMesh(mesh,first,count);},nullptr);
}
void CShaderDeviceDX12::DestroyStaticMesh(IMesh *mesh) { delete mesh; }
IVertexBuffer *CShaderDeviceDX12::CreateVertexBuffer(ShaderBufferType_t type,VertexFormat_t fmt,int nVertexCount,const char *)
{
    return new CVertexBufferDX12(fmt, nVertexCount, IsDynamicBufferType(type));
}
void CShaderDeviceDX12::DestroyVertexBuffer(IVertexBuffer *buffer) { delete buffer; }
IIndexBuffer *CShaderDeviceDX12::CreateIndexBuffer(ShaderBufferType_t type,MaterialIndexFormat_t fmt,int nIndexCount,const char *)
{
    return new CIndexBufferDX12(fmt, nIndexCount, IsDynamicBufferType(type));
}
void CShaderDeviceDX12::DestroyIndexBuffer(IIndexBuffer *buffer) { delete buffer; }
IVertexBuffer *CShaderDeviceDX12::GetDynamicVertexBuffer(int stream,VertexFormat_t fmt,bool buffered)
{
    if (stream < 0 || stream >= 32) return nullptr;
    auto &buffer = dynamicVertices_[stream * 2 + (buffered ? 1 : 0)];
    if (!buffer) buffer = std::make_unique<CVertexBufferDX12>(fmt, 65536, true);
    else if (buffer->GetVertexFormat() != fmt) buffer->BeginCastBuffer(fmt);
    return buffer.get();
}
IIndexBuffer *CShaderDeviceDX12::GetDynamicIndexBuffer(MaterialIndexFormat_t fmt,bool buffered)
{
    if (fmt != MATERIAL_INDEX_FORMAT_16BIT && fmt != MATERIAL_INDEX_FORMAT_32BIT) return nullptr;
    auto &buffer = dynamicIndices_[(fmt == MATERIAL_INDEX_FORMAT_32BIT ? 2 : 0) + (buffered ? 1 : 0)];
    if (!buffer) buffer = std::make_unique<CIndexBufferDX12>(fmt, 65536, true);
    return buffer.get();
}
// These callbacks tick a console front buffer during loading; Windows has no such path.
void CShaderDeviceDX12::EnableNonInteractiveMode(MaterialNonInteractiveMode_t,ShaderNonInteractiveInfo_t *) {}
void CShaderDeviceDX12::RefreshFrontBufferNonInteractive() {}
void CShaderDeviceDX12::HandleThreadEvent(uint32 threadEvent)
{
    if (!IsRecordingOwner()) { Warning("ShaderAPIDX12: queued device event executed on non-owner thread\n"); return; }
    switch (threadEvent)
    {
    case SHADER_THREAD_RELEASE_RESOURCES:
    case SHADER_THREAD_OTHER_APP_START: ReleaseResources(); break;
    case SHADER_THREAD_ACQUIRE_RESOURCES:
    case SHADER_THREAD_OTHER_APP_END: ReacquireResources(); break;
    case SHADER_THREAD_EVICT_RESOURCES: if (g_pShaderAPIDX12) g_pShaderAPIDX12->EvictManagedResources(); break;
    case SHADER_THREAD_RESET_RENDER_STATE: if (g_pShaderAPIDX12) g_pShaderAPIDX12->ResetRenderState(); break;
    case SHADER_THREAD_DEVICE_LOST: FailDevice("host device-lost event", device_ ? device_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED); break;
    default: Warning("ShaderAPIDX12: unknown thread event %u\n", threadEvent); break;
    }
}
void CShaderDeviceDX12::GetWindowSize(int &width, int &height) const
{
    RECT rect{};
    if (currentView_) GetClientRect(currentView_->hwnd, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

char *CShaderDeviceDX12::GetDisplayDeviceName()
{
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    if (currentView_ && currentView_->swap) currentView_->swap->GetContainingOutput(&output);
    if (!output && g_pShaderDeviceMgrDX12 && !g_pShaderDeviceMgrDX12->Adapters().empty())
    {
        const size_t index = adapterIndex_ >= 0 ? static_cast<size_t>(adapterIndex_) : 0;
        if (index < g_pShaderDeviceMgrDX12->Adapters().size()) g_pShaderDeviceMgrDX12->Adapters()[index]->EnumOutputs(0, &output);
    }
    DXGI_OUTPUT_DESC desc{};
    displayDeviceName_[0] = 0;
    if (output && SUCCEEDED(output->GetDesc(&desc))) WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, displayDeviceName_.data(), static_cast<int>(displayDeviceName_.size()), nullptr, nullptr);
    return displayDeviceName_.data();
}

CShaderDeviceMgrDX12::CShaderDeviceMgrDX12() = default;
CShaderDeviceMgrDX12::~CShaderDeviceMgrDX12() { Shutdown(); }

bool CShaderDeviceMgrDX12::Connect(CreateInterfaceFn factory)
{
    if (!factory) return false;
    hostFactory_=factory;
    filesystem_=static_cast<IFileSystem *>(factory(FILESYSTEM_INTERFACE_VERSION,nullptr));
    shaderUtil_=static_cast<IShaderUtil *>(factory(SHADER_UTIL_INTERFACE_VERSION,nullptr));
    if (!filesystem_ || !shaderUtil_) { Warning("ShaderAPIDX12: required host filesystem/shader utility interface missing\n");hostFactory_=nullptr;filesystem_=nullptr;shaderUtil_=nullptr;return false; }
    ConnectTier1Libraries(&factory, 1);
    ConnectTier2Libraries(&factory, 1);
    // Registers this module's development commands (shader_precache) with the host cvar system. FCVAR_CHEAT gates
    // them behind sv_cheats; FCVAR_DEVELOPMENTONLY would make retail engines reject them outright (cmd.cpp:1036).
    if (g_pCVar) ConVar_Register(FCVAR_CHEAT);
    MathLib_Init(2.2f, 2.2f, 0.0f, 2);
    dxSupport_.Load(filesystem_); // Malformed profiles log and leave hardware-derived caps intact.
    return true;
}
void CShaderDeviceMgrDX12::Disconnect()
{
    Shutdown();
    dxSupport_.Clear();
    if (hostFactory_) { if (g_pCVar) ConVar_Unregister(); DisconnectTier2Libraries(); DisconnectTier1Libraries(); }
    filesystem_ = nullptr; shaderUtil_ = nullptr; hostFactory_ = nullptr;
}
void *CShaderDeviceMgrDX12::QueryInterface(const char *name)
{
    return name ? Sys_GetFactoryThis()(name, nullptr) : nullptr;
}
InitReturnVal_t CShaderDeviceMgrDX12::Init()
{
    adapters_.clear();adapterInfo_.clear();adapterCaps_.clear();adapterModes_.clear();const bool allowWarp=CommandLine()&&CommandLine()->CheckParm("-dx12warp");
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;HRESULT hr=CreateDXGIFactory2(0,IID_PPV_ARGS(&factory));if(FAILED(hr)){Warning("ShaderAPIDX12: adapter factory failed (0x%08x)\n",static_cast<unsigned>(hr));return INIT_FAILED;}
    for(UINT i=0;;++i)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;const HRESULT enumResult=factory->EnumAdapters1(i,&adapter);if(enumResult==DXGI_ERROR_NOT_FOUND)break;if(FAILED(enumResult)){Warning("ShaderAPIDX12: adapter enumeration failed (0x%08x)\n",static_cast<unsigned>(enumResult));return INIT_FAILED;}
        DXGI_ADAPTER_DESC1 desc{};if(FAILED(adapter->GetDesc1(&desc))||(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
        adapters_.push_back(adapter);MaterialAdapterInfo_t info{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,info.m_pDriverName,sizeof(info.m_pDriverName),nullptr,nullptr);info.m_VendorID=desc.VendorId;info.m_DeviceID=desc.DeviceId;info.m_SubSysID=desc.SubSysId;info.m_Revision=desc.Revision;
        DXSupportCapsDX12 caps{};caps.vendor=desc.VendorId;caps.device=desc.DeviceId;caps.memory=desc.DedicatedVideoMemory;dxSupport_.ReadDXSupportLevels(caps);info.m_nDXSupportLevel=caps.recommended;info.m_nMaxDXSupportLevel=caps.max;adapterInfo_.push_back(info);adapterCaps_.push_back(caps);
    }
    if(allowWarp)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> warpBase;if(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpBase))))
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> warp;if(SUCCEEDED(warpBase.As(&warp))){DXGI_ADAPTER_DESC1 desc{};if(SUCCEEDED(warp->GetDesc1(&desc))){adapters_.push_back(warp);MaterialAdapterInfo_t info{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,info.m_pDriverName,sizeof(info.m_pDriverName),nullptr,nullptr);info.m_VendorID=desc.VendorId;info.m_DeviceID=desc.DeviceId;info.m_SubSysID=desc.SubSysId;info.m_Revision=desc.Revision;DXSupportCapsDX12 caps{};caps.vendor=desc.VendorId;caps.device=desc.DeviceId;caps.memory=desc.DedicatedVideoMemory;dxSupport_.ReadDXSupportLevels(caps);info.m_nDXSupportLevel=caps.recommended;info.m_nMaxDXSupportLevel=caps.max;adapterInfo_.push_back(info);adapterCaps_.push_back(caps);}}
        }
    }
    adapterModes_.resize(adapters_.size());
    for (size_t index = 0; index < adapters_.size(); ++index)
    {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        for (UINT outputIndex = 0; ; ++outputIndex)
        {
            Microsoft::WRL::ComPtr<IDXGIOutput> candidate;
            if (adapters_[index]->EnumOutputs(outputIndex, &candidate) == DXGI_ERROR_NOT_FOUND) break;
            if (!candidate) continue;
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(candidate->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
            output = candidate;
            break;
        }
        if (!output && FAILED(adapters_[index]->EnumOutputs(0, &output)))
        {
            DEVMODEW desktop{}; desktop.dmSize = sizeof(desktop);
            ShaderDisplayMode_t mode; mode.m_Format = IMAGE_FORMAT_UNKNOWN;
            if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktop))
            {
                mode.m_nWidth = desktop.dmPelsWidth; mode.m_nHeight = desktop.dmPelsHeight;
                mode.m_nRefreshRateNumerator = desktop.dmDisplayFrequency; mode.m_nRefreshRateDenominator = 1;
            }
            else
            {
                mode.m_nWidth = GetSystemMetrics(SM_CXSCREEN); mode.m_nHeight = GetSystemMetrics(SM_CYSCREEN);
                mode.m_nRefreshRateNumerator = mode.m_nRefreshRateDenominator = 0;
            }
            if (mode.m_nWidth > 0 && mode.m_nHeight > 0) adapterModes_[index].push_back(mode);
            continue;
        }

        UINT count = 0;
        DXGI_FORMAT modeFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        HRESULT modeResult = output->GetDisplayModeList(modeFormat, 0, &count, nullptr);
        if (FAILED(modeResult) || count == 0)
        {
            modeFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            count = 0;
            modeResult = output->GetDisplayModeList(modeFormat, 0, &count, nullptr);
        }
        if (FAILED(modeResult) || count == 0)
        {
            modeFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            count = 0;
            modeResult = output->GetDisplayModeList(modeFormat, 0, &count, nullptr);
        }
        if (FAILED(modeResult) || count == 0) continue;

        std::vector<DXGI_MODE_DESC> modes(count);
        if (FAILED(output->GetDisplayModeList(modeFormat, 0, &count, modes.data()))) continue;
        auto &dest = adapterModes_[index];
        dest.reserve(count);
        for (UINT i = 0; i < count; ++i)
        {
            ShaderDisplayMode_t mode;
            mode.m_nWidth = modes[i].Width;
            mode.m_nHeight = modes[i].Height;
            mode.m_Format = IMAGE_FORMAT_UNKNOWN;
            mode.m_nRefreshRateNumerator = modes[i].RefreshRate.Numerator;
            mode.m_nRefreshRateDenominator = modes[i].RefreshRate.Denominator;
            if (mode.m_nWidth > 0 && mode.m_nHeight > 0) dest.push_back(mode);
        }
        if (adapterModes_[index].empty())
        {
            DEVMODEW desktop{};
            desktop.dmSize = sizeof(desktop);
            ShaderDisplayMode_t mode;
            if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktop))
            {
                mode.m_nWidth = desktop.dmPelsWidth;
                mode.m_nHeight = desktop.dmPelsHeight;
                mode.m_nRefreshRateNumerator = desktop.dmDisplayFrequency;
                mode.m_nRefreshRateDenominator = 1;
            }
            else
            {
                mode.m_nWidth = GetSystemMetrics(SM_CXSCREEN);
                mode.m_nHeight = GetSystemMetrics(SM_CYSCREEN);
                mode.m_nRefreshRateNumerator = 0;
                mode.m_nRefreshRateDenominator = 0;
            }
            mode.m_Format = IMAGE_FORMAT_UNKNOWN;
            if (mode.m_nWidth > 0 && mode.m_nHeight > 0) adapterModes_[index].push_back(mode);
        }
    }
    if(adapters_.empty())return INIT_FAILED;
#ifdef TRACY_ENABLE
    if(!TracyIsStarted)tracy::StartupProfiler();
#endif
    return INIT_OK;
}
void CShaderDeviceMgrDX12::Shutdown()
{
    if (g_pShaderAPIDX12) g_pShaderAPIDX12->ShutdownDeviceResources();
    device_.ShutdownDevice();
    adapters_.clear(); adapterInfo_.clear(); adapterCaps_.clear(); adapterModes_.clear(); currentAdapter_=-1;
#ifdef TRACY_ENABLE
    // Join profiler workers before FreeLibrary acquires the Windows loader lock.
    g_tracyZonesActiveDX12.store(false, std::memory_order_relaxed);
    if(TracyIsStarted)tracy::ShutdownProfiler();
#endif
}
void CShaderDeviceMgrDX12::GetAdapterInfo(int nAdapter,MaterialAdapterInfo_t &info) const { std::memset(&info,0,sizeof(info));if(nAdapter>=0&&nAdapter<(int)adapterInfo_.size())info=adapterInfo_[nAdapter]; }
bool CShaderDeviceMgrDX12::GetRecommendedConfigurationInfo(int nAdapter,int nDXLevel,KeyValues *configuration)
{
    if(nAdapter<0||nAdapter>=(int)adapterCaps_.size()||!configuration||(nDXLevel!=0&&nDXLevel!=90&&nDXLevel!=95))return false;
    return dxSupport_.GetRecommendedConfigurationInfo(adapterCaps_[nAdapter],nDXLevel,configuration);
}
int CShaderDeviceMgrDX12::GetModeCount(int adapter) const
{
    const int resolved = adapter >= 0 ? adapter : (currentAdapter_ >= 0 ? currentAdapter_ : 0);
    return resolved >= 0 && resolved < static_cast<int>(adapterModes_.size()) ? static_cast<int>(adapterModes_[resolved].size()) : 0;
}
void CShaderDeviceMgrDX12::GetModeInfo(ShaderDisplayMode_t *info, int adapter, int mode) const
{
    if (!info) return;
    *info = ShaderDisplayMode_t();
    const int resolved = adapter >= 0 ? adapter : (currentAdapter_ >= 0 ? currentAdapter_ : 0);
    if (mode >= 0 && resolved >= 0 && resolved < static_cast<int>(adapterModes_.size()) && mode < static_cast<int>(adapterModes_[resolved].size()))
        *info = adapterModes_[resolved][mode];
}
void CShaderDeviceMgrDX12::GetCurrentModeInfo(ShaderDisplayMode_t *info, int adapter) const
{
    if (!info) return;
    *info = ShaderDisplayMode_t();
    if (adapter < 0 || adapter >= static_cast<int>(adapters_.size())) return;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    if (FAILED(adapters_[adapter]->EnumOutputs(0, &output)) || FAILED(output->GetDesc(&desc))) return;
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &mode)) return;
    info->m_nWidth = mode.dmPelsWidth; info->m_nHeight = mode.dmPelsHeight;
            info->m_Format = IMAGE_FORMAT_UNKNOWN;
    info->m_nRefreshRateNumerator = mode.dmDisplayFrequency; info->m_nRefreshRateDenominator = 1;
}
bool CShaderDeviceMgrDX12::SetAdapter(int nAdapter,int) {if(nAdapter<0||nAdapter>=(int)adapters_.size())return false;currentAdapter_=nAdapter;return true;}
CreateInterfaceFn CShaderDeviceMgrDX12::SetMode(void *hWnd,int nAdapter,const ShaderDeviceInfo_t &mode)
{
    if (!SetAdapter(nAdapter, 0)) return nullptr;
    auto caps = adapterCaps_[nAdapter];
    const int level = mode.m_nDXLevel ? mode.m_nDXLevel : caps.recommended;
    if ((level != 90 && level != 95) || level > caps.max) return nullptr;
    if (g_pShaderAPIDX12) g_pShaderAPIDX12->ShutdownDeviceResources();
    if (!device_.Initialize(hWnd, nAdapter, mode, adapters_[nAdapter].Get())) return nullptr;
    dxSupport_.ReadHardwareCaps(caps, level);
    MaterialAdapterInfo_t info{};
    GetAdapterInfo(nAdapter, info);
    if (g_pHardwareConfigDX12)
    {
        g_pHardwareConfigDX12->SetAdapter(info, caps.memory, mode.m_nAASamples > 1, mode.m_nAASamples);
        g_pHardwareConfigDX12->SetDXSupportLevels(caps.recommended, caps.max);
        g_pHardwareConfigDX12->SetDXLevel(level);
        g_pHardwareConfigDX12->SetSupportCaps(caps.fastClipping, caps.centroidHack, caps.disableShaderOptimizations);
    }
    if (!g_pShaderAPIDX12 || !g_pShaderAPIDX12->InitializeDeviceResources(&device_))
    {
        if (g_pShaderAPIDX12) g_pShaderAPIDX12->ShutdownDeviceResources();
        device_.ShutdownDevice();
        return nullptr;
    }
    Msg("ShaderAPIDX12: native D3D12 initialized on %s, %dx%d, material DX level %d, %u samples\n", info.m_pDriverName, device_.SceneWidth(), device_.SceneHeight(), level, device_.SceneSampleCount());
    return Sys_GetFactoryThis();
}
void CShaderDeviceMgrDX12::AddModeChangeCallback(ShaderModeChangeCallbackFunc_t func) {if(func&&std::find(callbacks_.begin(),callbacks_.end(),func)==callbacks_.end())callbacks_.push_back(func);}
void CShaderDeviceMgrDX12::RemoveModeChangeCallback(ShaderModeChangeCallbackFunc_t func) {callbacks_.erase(std::remove(callbacks_.begin(),callbacks_.end(),func),callbacks_.end());}
void CShaderDeviceMgrDX12::NotifyModeChange(){for(auto callback:callbacks_)if(callback)callback();}

} // namespace shaderapidx12
