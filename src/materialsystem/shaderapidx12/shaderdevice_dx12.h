#pragma once
#include <intrin.h>

#include "shaderapi/IShaderDevice.h"
#include "materialsystem/imesh.h"
#include "materialsystem/shaderapidx12/resources_dx12.h"
#include "materialsystem/shaderapidx12/hardwareconfig_dx12.h"
#include "materialsystem/shaderapidx12/dxsupport_dx12.h"
#include "materialsystem/shaderapidx12/shader_translate_dx12.h"
#include "materialsystem/shaderapidx12/command_recorder_dx12.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include <atomic>
#include <cstdint>
#include "tier1/utlvector.h"
#include "tier0/threadtools.h"
class IShaderUtil;
class IFileSystem;

namespace shaderapidx12
{

struct ShaderRecordDX12
{
    uint64_t identity=0;
    // Hot per-draw constant layout for the active variant (see DrawBuffers); invalidated with the variant.
    std::array<uint32_t,3> constantCounts{};
    uint64_t constantLayoutVariant=0;
    uint32_t inlineConstantMask=0;
    bool constantLayoutValid=false;
    std::vector<unsigned char> legacyBytecode;
    std::vector<unsigned char> bytecode;
    bool stagePixel=false;
    // SM2 centroid declarations live in the VCS header, not the bytecode's DCL tokens.
    uint32_t centroidTexcoordMask=0;
    ShaderTranslationResultDX12 translated;
    bool stageGeometry=false;
    uint64_t activeVariantKey=0;
    bool activeVariantValid=false, inputSignatureReady=false;
    std::vector<ShaderInputElementDX12> inputSignature;
    std::array<uint32_t,3> nativeConstantRegisters{};
    // Native records: reflected cbuffers (engine or bridge-written material blocks) and their combined ABI hash.
    struct NativeCBufferMemberDX12 { std::string name; uint32_t offset=0, byteSize=0; };
    struct NativeCBufferBindingDX12 { std::string name; uint32_t shaderRegister=0, registerSpace=0, byteSize=0; uint64_t layoutHash=0; std::vector<NativeCBufferMemberDX12> members; };
    std::vector<NativeCBufferBindingDX12> nativeCBuffers;
    uint64_t nativeAbiHash=0;
    bool nativeReflectionReady=false;
    // Hash of translated.outputLinkage for the active variant; cleared whenever the linkage or variant changes.
    uint64_t linkageHash=0,linkageHashVariant=0;
    bool linkageHashValid=false;
    struct DerivedConstants {
        std::array<uint64_t,3> versions{};
        CUtlVector<std::array<float,4>> floats;
        CUtlVector<std::array<int,4>> integers;
        CUtlVector<uint32_t> booleans;

        DerivedConstants() = default;
        DerivedConstants(const DerivedConstants &) = delete;
        DerivedConstants &operator=(const DerivedConstants &) = delete;
        DerivedConstants(DerivedConstants &&other) noexcept { Swap(other); }
        DerivedConstants &operator=(DerivedConstants &&other) noexcept { Swap(other); return *this; }

        void Swap(DerivedConstants &other) noexcept {
            if (this == &other) return;
            versions.swap(other.versions);
            floats.Swap(other.floats);
            integers.Swap(other.integers);
            booleans.Swap(other.booleans);
        }
    } derived;
    struct Variant {
        uint64_t key=0;
        ShaderTranslationResultDX12 result;
        std::vector<ShaderInputElementDX12> inputSignature;
        bool inputSignatureReady=false;
        DerivedConstants derived;
    };
    CUtlBlockVector<Variant> variants;
    // Generated raster-stage variants share this vertex shader's retirement identity.
    struct GeometryVariant {
        uint64_t vertexVariant=0;
        uint32_t rasterKey=0;
        ShaderTranslationResultDX12 result;
    };
    CUtlBlockVector<GeometryVariant> geometryVariants;
    D3D12_SHADER_BYTECODE Bytecode() const {
        const auto &code=legacyBytecode.empty()?bytecode:translated.bytecode;
        return {code.data(),code.size()};
    }
};

class CShaderDeviceDX12 final : public IShaderDevice
{
public:
    CShaderDeviceDX12();
    ~CShaderDeviceDX12();
    bool Initialize(void *hwnd, int adapter, const ShaderDeviceInfo_t &info, IDXGIAdapter1 *selectedAdapter);
    void ShutdownDevice();
    bool IsInitialized() const { return !failed_ && device_ != nullptr && queue_ != nullptr; }
    ID3D12Device *NativeDevice() const { return device_.Get(); }
    // Drains queued submissions so direct queue operations keep submission order.
    ID3D12CommandQueue *Queue() { DrainSubmissions(); return queue_.Get(); }
    void FlushSubmissions();
    bool ConsumeGpuTime(double &averageMs,uint32_t &frames);
    // Replays every recorded command before returning; required before rewriting or freeing CPU RTV/DSV descriptors
    // that recorded OMSetRenderTargets/Clear*View calls still name.
    void DrainRecording() { recorder_.Flush(); DrainSubmissions(); }
    // Waits until the worker has issued every queued operation (header-inline so callers outside the DLL can use it).
    void DrainSubmissions() { const uint32_t head=submitHead_.load(std::memory_order_relaxed); while(submitTail_.load(std::memory_order_acquire)!=head)submitDoneEvent_.Wait(); }
    uint64_t Submit(bool wait);
    uint64_t SubmitFrameSync();
    bool WaitForFence(uint64_t value);
    // Commands are recorded and replayed onto the native list on the submission worker.
    CCommandRecorderDX12 *CommandList() { return !failed_ && recording_ && IsRecordingOwner() ? &recorder_ : nullptr; }
    ID3D12Resource *CurrentBackBuffer() const;
    uint32_t CurrentBackBufferIndex() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferRTV() const;
    ID3D12Resource *SceneColor() const;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV(bool srgb = false) const;
    ID3D12Resource *SceneDepth() const;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneDSV() const;
    DXGI_FORMAT SceneColorFormat(bool srgb = false) const { return srgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM; }
    DXGI_FORMAT SceneDepthFormat() const { return DXGI_FORMAT_D24_UNORM_S8_UINT; }
    int SceneSampleCount() const { return sampleCount_; }
    int SceneWidth() const { return width_; }
    int SceneHeight() const { return height_; }
    void TransitionSceneColor(D3D12_RESOURCE_STATES state);
    void TransitionSceneDepth(D3D12_RESOURCE_STATES state);
    void RetainResource(ID3D12Resource *resource);
    // Workers enqueue handles only; the recording owner drains at public API boundaries.
    void QueueTextureDeletion(uintptr_t handle);
    void TakeTextureDeletionRequests(CUtlVector<uintptr_t> &handles);
    // Relaxed hint; a request queued concurrently is taken by the next check.
    bool HasTextureDeletionRequests() const { return pendingTextureDeletionCount_.load(std::memory_order_relaxed) != 0; }
    bool SupportsMSAA(int count, int quality = 0) const;
    bool ChangeMode(const ShaderDeviceInfo_t &info);
    // x64 TEB ClientId.UniqueThread (what GetCurrentThreadId returns), read inline on the per-draw path.
    bool IsRecordingOwner() const { return ownerThread_ == static_cast<DWORD>(__readgsdword(0x48)); }
    bool AcquireRecordingOwnership();
    void ReleaseRecordingOwnership();
    SignDxbcFnDX12 Signer() const { return signer_; }
    uint64_t NextFenceValue() const { return fenceValue_ + 1; }
    uint64_t CompletedFenceValue() const { return fence_ ? fence_->GetCompletedValue() : 0; }
    uint64_t NextSubmissionFence() const { return NextFenceValue(); }
    uint64_t CompletedFence() const { return CompletedFenceValue(); }
    void ReleaseResources() override;
    void ReacquireResources() override;
    ImageFormat GetBackBufferFormat() const override { return IMAGE_FORMAT_BGRX8888; }
    void GetBackBufferDimensions(int &width, int &height) const override { width = width_; height = height_; }
    int GetCurrentAdapter() const override { return adapterIndex_; }
    bool IsUsingGraphics() const override { return IsInitialized(); }
    void SpewDriverInfo() const override;
    int StencilBufferBits() const override { return 8; }
    bool IsAAEnabled() const override { return sampleCount_ > 1; }
    void Present() override;
    void GetWindowSize(int &width, int &height) const override;
    void SetHardwareGammaRamp(float fGamma,float fGammaTVRangeMin,float fGammaTVRangeMax,float fGammaTVExponent,bool bTVEnabled) override;
    bool AddView(void *hwnd) override;
    void RemoveView(void *hwnd) override;
    void SetView(void *hwnd) override;
    IShaderBuffer *CompileShader(const char *pProgram,size_t nBufLen,const char *pShaderVersion) override;
    VertexShaderHandle_t CreateVertexShader(IShaderBuffer *pShaderBuffer) override;
    void DestroyVertexShader(VertexShaderHandle_t hShader) override;
    GeometryShaderHandle_t CreateGeometryShader(IShaderBuffer *pShaderBuffer) override;
    void DestroyGeometryShader(GeometryShaderHandle_t hShader) override;
    PixelShaderHandle_t CreatePixelShader(IShaderBuffer *pShaderBuffer) override;
    void DestroyPixelShader(PixelShaderHandle_t hShader) override;
    IMesh *CreateStaticMesh(VertexFormat_t vertexFormat,const char *pTextureBudgetGroup,IMaterial *pMaterial = nullptr) override;
    void DestroyStaticMesh(IMesh *mesh) override;
    IVertexBuffer *CreateVertexBuffer(ShaderBufferType_t type,VertexFormat_t fmt,int nVertexCount,const char *pBudgetGroup) override;
    void DestroyVertexBuffer(IVertexBuffer *buffer) override;
    IIndexBuffer *CreateIndexBuffer(ShaderBufferType_t type,MaterialIndexFormat_t fmt,int nIndexCount,const char *pBudgetGroup) override;
    void DestroyIndexBuffer(IIndexBuffer *buffer) override;
    IVertexBuffer *GetDynamicVertexBuffer(int nStreamID,VertexFormat_t vertexFormat,bool bBuffered = true) override;
    IIndexBuffer *GetDynamicIndexBuffer(MaterialIndexFormat_t fmt,bool bBuffered = true) override;
    void EnableNonInteractiveMode(MaterialNonInteractiveMode_t mode,ShaderNonInteractiveInfo_t *pInfo = nullptr) override;
    void RefreshFrontBufferNonInteractive() override;
    void HandleThreadEvent(uint32 threadEvent) override;
    char *GetDisplayDeviceName() override;

private:
    struct View {
        HWND hwnd = nullptr;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap, sceneRTVHeap, sceneDSVHeap;
        // Heap starts cached at creation; SceneRTV/SceneDSV run per draw.
        D3D12_CPU_DESCRIPTOR_HANDLE sceneRTVStart{}, sceneDSVStart{};
        CUtlVector<ID3D12Resource *> backBuffers;
        Microsoft::WRL::ComPtr<ID3D12Resource> sceneColor, sceneDepth;
        D3D12_RESOURCE_STATES sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        D3D12_RESOURCE_STATES sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        int width = 0, height = 0;
        uint64_t lastFence = 0;
        bool suspended = false, occluded = false;
        ~View() { ReleaseBackBuffers(); }
        void ReleaseBackBuffers() {
            for (auto *buffer : backBuffers) if (buffer) buffer->Release();
            backBuffers.RemoveAll();
        }
    };
    // -dx12stats GPU frame timing: begin/end timestamps per presented frame, read back once its fence completes.
    static constexpr uint32_t kTimestampSlots=32;
    bool GpuTimingBeforeSubmit();
    void GpuTimingAfterSubmit();
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestampHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadback_;
    const uint64_t *timestampData_=nullptr;
    std::array<uint64_t,kTimestampSlots> timestampFences_{};
    uint32_t timestampSlot_=0;
    bool timestampBegun_=false;
    uint64_t timestampFrequency_=0;
    double gpuTimeSumMs_=0.0;
    uint32_t gpuTimePresented_=0;
    // Submission worker: Close/ExecuteCommandLists/Signal and Present run there in FIFO order. The recording
    // owner calls FlushSubmissions before any other queue or swap-chain access.
    struct SubmitOpDX12 { enum Kind { Execute, Present, Replay, Reset } kind=Execute; ID3D12GraphicsCommandList *list=nullptr; ID3D12CommandAllocator *allocator=nullptr; uint64_t value=0; IDXGISwapChain3 *swap=nullptr; View *view=nullptr; UINT interval=0,flags=0; unsigned char *chunk=nullptr; size_t chunkBytes=0; };
    HRESULT RunSubmission(const SubmitOpDX12 &op);
    unsigned char *AcquireCommandChunk();
    void FlushCommandChunk(unsigned char *chunk,size_t bytes);
    void ReleaseCommandChunks();
    static constexpr int kMaxCommandChunks=32;
    CCommandRecorderDX12 recorder_;
    CThreadFastMutex chunkMutex_;
    CUtlVector<unsigned char *> freeChunks_,allChunks_;
    static uintp SubmitThreadMain(void *param);
    void StartSubmitThread();
    void StopSubmitThread();
    void EnqueueSubmission(const SubmitOpDX12 &op);
    ThreadHandle_t submitThread_=nullptr;
    CThreadEvent submitWorkEvent_,submitDoneEvent_;
    std::atomic<bool> submitExit_{false};
    std::atomic<uint32_t> submitHead_{0},submitTail_{0};
    std::atomic<HRESULT> submitError_{S_OK};
    std::array<SubmitOpDX12,16> submitOps_{};
    bool CreateView(View &view, HWND hwnd, int width, int height);
    bool ResizeView(View &view, int width, int height);
    bool CreateViewTargets(View &view);
    bool CreateFrameObjects();
    bool BeginRecording();
    void FailDevice(const char *operation, HRESULT hr);
    bool CheckDevice(const char *operation, HRESULT hr);
    void ReleaseViews();
    View *CurrentView() const;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    struct FrameContext { Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator; Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list; uint64_t fence=0; CUtlVector<ID3D12Resource *> retained; };
    std::array<FrameContext,3> frames_{};
    uint32_t frameIndex_=0;
    bool recording_=false, failed_=false;
    bool changingMode_=false;
    std::atomic<DWORD> ownerThread_{0};
    CThreadFastMutex textureDeletionMutex_;
    CUtlVector<uintptr_t> pendingTextureDeletions_;
    std::atomic<int> pendingTextureDeletionCount_{0};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    CUtlVector<View *> views_;
    std::array<std::unique_ptr<CVertexBufferDX12>, 64> dynamicVertices_;
    std::array<std::unique_ptr<CIndexBufferDX12>, 4> dynamicIndices_;
    View *currentView_ = nullptr;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0, frameSyncFence_ = 0;
    HMODULE signerModule_ = nullptr;
    SignDxbcFnDX12 signer_ = nullptr;
    UINT rtvStride_ = 0;
    int adapterIndex_ = -1;
    int width_ = 0, height_ = 0;
    int sampleCount_ = 1, sampleQuality_ = 0;
    int backBufferCount_ = 2;
    void *window_ = nullptr;
    std::array<char, 128> displayDeviceName_{};
    float gamma_ = 2.2f, gammaMin_ = 0.0f, gammaMax_ = 255.0f, gammaExponent_ = 2.2f;
    bool gammaTV_ = false;
    bool waitForVsync_ = true, windowed_ = true;
    bool allowTearing_ = false;
};

class CShaderDeviceMgrDX12 final : public IShaderDeviceMgr
{
public:
    CShaderDeviceMgrDX12();
    ~CShaderDeviceMgrDX12();
    bool Connect(CreateInterfaceFn factory) override;
    void Disconnect() override;
    void *QueryInterface(const char *name) override;
    InitReturnVal_t Init() override;
    void Shutdown() override;
    int GetAdapterCount() const override { return static_cast<int>(adapters_.size()); }
    void GetAdapterInfo(int nAdapter, MaterialAdapterInfo_t &info) const override;
    bool GetRecommendedConfigurationInfo(int nAdapter,int nDXLevel,KeyValues *pConfiguration) override;
    int GetModeCount(int nAdapter) const override;
    void GetModeInfo(ShaderDisplayMode_t *pInfo,int nAdapter,int nMode) const override;
    void GetCurrentModeInfo(ShaderDisplayMode_t *pInfo,int nAdapter) const override;
    bool SetAdapter(int nAdapter,int nFlags) override;
    CreateInterfaceFn SetMode(void *hWnd,int nAdapter,const ShaderDeviceInfo_t &mode) override;
    void AddModeChangeCallback(ShaderModeChangeCallbackFunc_t func) override;
    void RemoveModeChangeCallback(ShaderModeChangeCallbackFunc_t func) override;
    void NotifyModeChange();
    CShaderDeviceDX12 *Device() { return &device_; }
    const std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> &Adapters() const { return adapters_; }
    IShaderUtil *HostShaderUtil() const { return shaderUtil_; }
    IFileSystem *HostFileSystem() const { return filesystem_; }
private:
    std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters_;
    std::vector<MaterialAdapterInfo_t> adapterInfo_;
    std::vector<DXSupportCapsDX12> adapterCaps_;
    std::vector<std::vector<ShaderDisplayMode_t>> adapterModes_;
    std::vector<ShaderModeChangeCallbackFunc_t> callbacks_;
    CDXSupportDX12 dxSupport_;
    CShaderDeviceDX12 device_;
    CreateInterfaceFn hostFactory_ = nullptr;
    IFileSystem *filesystem_ = nullptr;
    IShaderUtil *shaderUtil_ = nullptr;
    int currentAdapter_ = -1;
};

extern CShaderDeviceMgrDX12 *g_pShaderDeviceMgrDX12;
extern CShaderDeviceDX12 *g_pShaderDeviceDX12;

} // namespace shaderapidx12
