#pragma once
// Native-resolution temporal anti-aliasing providers (DLAA, FSR native AA, XeSS AA) and the DLSS-NR neural rendering
// post-process for the DX12 backend. Every provider export is resolved at runtime through LoadLibraryExW/GetProcAddress;
// a missing module or export only makes that provider unavailable. Provider objects are owned by the recording thread;
// the worker only runs the recorded dispatch callback, which names stable objects that stay alive until a GPU-idle
// boundary.
#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct MaterialAdapterInfo_t;

namespace shaderapidx12
{
class CCommandRecorderDX12;
struct NrConstantsDX12;
constexpr uint32_t kNrMaxLayersDX12 = 8;
constexpr uint32_t kNrMinWidth = 320, kNrMinHeight = 180;

enum class UpscalerKindDX12 : uint8_t { None = 0, DLSS = 1, FSR = 2, XeSS = 3 };
const char *UpscalerKindNameDX12(UpscalerKindDX12 kind);

// Written by the worker when a recorded dispatch replays; read by the recording owner once `serial` matches.
struct UpscalerReplayResultDX12
{
    std::atomic<uint64_t> serial{0};
    std::atomic<uint32_t> code{0};   // 0 success, otherwise the provider's failure code (never 0)
    std::atomic<uint32_t> nrCode{0}; // 0 success or no DLSS-NR layers requested, otherwise the NR failure code
    std::atomic<uint64_t> frame{~0ull};
};

struct UpscalerFeatureDescDX12
{
    UpscalerKindDX12 kind = UpscalerKindDX12::None;
    uint32_t width = 0, height = 0;
    bool depthInverted = false;
    bool operator==(const UpscalerFeatureDescDX12 &o) const { return kind == o.kind && width == o.width && height == o.height && depthInverted == o.depthInverted; }
    bool operator!=(const UpscalerFeatureDescDX12 &o) const { return !(*this == o); }
};

// DLSS-NR model tuning. The model reads these once, while each layer's feature is created; a change rebuilds the chain.
struct DlssNrTuningDX12
{
    uint32_t preset = 0, style = 0;
    float intensity = 1.f, localStructure = 1.f, localTone = 1.f, skinStructure = -1.f;
    bool autoMask = true, uiCorrection = true;
    bool operator==(const DlssNrTuningDX12 &o) const
    {
        return preset == o.preset && style == o.style && intensity == o.intensity && localStructure == o.localStructure && localTone == o.localTone &&
               skinStructure == o.skinStructure && autoMask == o.autoMask && uiCorrection == o.uiCorrection;
    }
    bool operator!=(const DlssNrTuningDX12 &o) const { return !(*this == o); }
};

// DLSS-NR composition of the model's answer back onto each layer's input; read per dispatch.
struct DlssNrComposeDX12
{
    float whitePoint = 1.f, transferStrength = 1.f, colourStrength = 1.f, maxRatio = 2.f, compareSplit = .5f, compareZoom = 1.f;
    uint32_t reversibleMode = 0, debugView = 0, compareMode = 0;
    bool applyModel = true, compareSwap = false;
};

// One dispatch. `*Before` are the tracked states at record time; the callback leaves colour/motion in
// RENDER_TARGET, depth in DEPTH_WRITE and output in UNORDERED_ACCESS on every path.
struct UpscalerDispatchDX12
{
    ID3D12Resource *color = nullptr, *depth = nullptr, *motion = nullptr, *output = nullptr;
    D3D12_RESOURCE_STATES colorBefore = D3D12_RESOURCE_STATE_RENDER_TARGET, depthBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    D3D12_RESOURCE_STATES motionBefore = D3D12_RESOURCE_STATE_RENDER_TARGET, outputBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    float jitter[2] = {}, motionScale[2] = {};
    float cameraNear = 0, cameraFar = 0, fovY = 0, frameTimeMs = 0;
    uint32_t width = 0, height = 0;
    uint32_t nrLayers = 0; // DLSS-NR layers chained after the temporal AA; each feeds the next
    DlssNrComposeDX12 nr;
    bool reset = false, nrReset = false;
    bool forceFailure = false; // test hook: skip the provider call and report a replay failure
    uint64_t frame = 0, serial = 0;
    UpscalerReplayResultDX12 *result = nullptr;
    // Shader-visible descriptor block for the depth clone and DLSS-NR passes (DescriptorCount(nrLayers) entries),
    // written at record time and retired with the recording fence.
    ID3D12DescriptorHeap *heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
};

class CUpscalerDX12
{
public:
    CUpscalerDX12();
    ~CUpscalerDX12();
    CUpscalerDX12(const CUpscalerDX12 &) = delete;
    CUpscalerDX12 &operator=(const CUpscalerDX12 &) = delete;

    // Loads every installed provider module beside the renderer and logs vendor/device and runtime versions.
    bool Initialize(ID3D12Device *device, const MaterialAdapterInfo_t &adapter, const wchar_t *moduleDir, bool verbose);
    bool Initialized() const { return device_ != nullptr; }
    // Mode 1 auto, 2 DLSS/DLAA, 3 FSR native AA, 4 XeSS AA; None when the mode's provider is unavailable.
    UpscalerKindDX12 Resolve(int mode) const;
    // Creates the provider context for `desc` when it differs from the current one. DLSS records its creation on a
    // private list executed on `queue` and waited for, so no recorded frame ever creates and evaluates together.
    // The caller destroys any previous feature through ReleaseFeature first.
    bool PrepareFeature(const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *queue);
    const UpscalerFeatureDescDX12 &Feature() const { return feature_; }
    // Provider jitter for the current feature; false keeps the backend's Halton(2,3) eight-phase sequence.
    bool ProviderJitter(uint64_t index, float &x, float &y) const;
    // Records the whole provider envelope (temporal AA plus any DLSS-NR layers) as one ExternalCommand.
    bool RecordDispatch(CCommandRecorderDX12 &recorder, const UpscalerDispatchDX12 &dispatch);
    // Destroys the current feature and the DLSS-NR chain. Only after SubmitAndWaitForGpu: no recorded callback may
    // still name them.
    void ReleaseFeature();
    // ReleaseFeature plus runtime shutdown and module unload; the same GPU-idle precondition applies.
    void Shutdown();

    // DLSS-NR: availability is decided at Initialize from the installed files; the snippet itself loads on first use.
    bool NrAvailable() const { return nr_.available; }
    const std::string &NrReason() const { return nr_.reason; }
    bool NrReady(const DlssNrTuningDX12 &tuning, uint32_t layers, ID3D12Resource *source) const;
    uint32_t NrLayers() const;
    // Builds `layers` features over `source` (the temporal-AA output, which every chain starts from). Any existing
    // chain must have been released through ReleaseNr behind a GPU-idle boundary first.
    bool PrepareNr(const DlssNrTuningDX12 &tuning, uint32_t layers, ID3D12Resource *source, ID3D12CommandQueue *queue);
    void ReleaseNr();
    // The dispatch needs a descriptor block when DLSS or DLSS-NR reads the depth clone.
    bool NeedsDescriptors(const UpscalerDispatchDX12 &d) const { return NeedsDepthClone(d); }
    static uint32_t DescriptorCount(uint32_t nrLayers) { return 6u * (1u + 2u * nrLayers); }

private:
    enum class State : uint8_t { Disabled, Ready, Failed };
    struct XeSSApi;
    struct FfxApi;
    struct NgxApi;
    struct NrApi;
    struct NrChain;
    struct Provider
    {
        HMODULE module = nullptr;
        bool available = false;
        std::string version, reason;
        uint64_t versionId = 0; // FFX: the provider id returned by the version query
    };
    static void ReplayThunk(ID3D12GraphicsCommandList *list, ID3D12Device *device, const void *payload) noexcept;
    uint32_t Execute(ID3D12GraphicsCommandList *list, const UpscalerDispatchDX12 &dispatch) const noexcept;
    // Runs the DLSS-NR chain over d.output; on success the final layer is in NrResult (UNORDERED_ACCESS).
    uint32_t ExecuteNr(ID3D12GraphicsCommandList *list, const UpscalerDispatchDX12 &d) const noexcept;
    ID3D12Resource *NrResult() const;
    bool EnsureCompute();
    void WriteDescriptors(const UpscalerDispatchDX12 &d) const;
    void BindCompute(ID3D12GraphicsCommandList *list, const UpscalerDispatchDX12 &d, ID3D12PipelineState *pso, uint32_t pass, const NrConstantsDX12 &constants) const;
    bool LoadFfx(const std::wstring &dir);
    // DLSS and DLSS-NR read a typed R24_UNORM_X8 clone of the R24G8_TYPELESS scene depth, refreshed in every replay.
    bool NeedsDepthClone(const UpscalerDispatchDX12 &d) const { return feature_.kind == UpscalerKindDX12::DLSS || d.nrLayers; }
    bool EnsureDepthClone(uint32_t width, uint32_t height);
    bool LoadXeSS(const std::wstring &dir);
    bool LoadNgx(const std::wstring &dir);
    void ProbeNr(const std::wstring &dir);
    bool InitNrSnippet();
    bool CreateXeSS(const UpscalerFeatureDescDX12 &desc);
    bool CreateFfx(const UpscalerFeatureDescDX12 &desc);
    bool CreateDlss(const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *queue);
    // Private one-shot list for feature creation; EndImmediate executes it on `queue` and waits for completion.
    ID3D12GraphicsCommandList *BeginImmediate();
    bool EndImmediate(ID3D12CommandQueue *queue);

    ID3D12Device *device_ = nullptr;
    bool verbose_ = false;
    unsigned vendor_ = 0, deviceId_ = 0;
    std::wstring dir_;
    Provider dlss_, fsr_, xess_, ngxCore_, nr_;
    XeSSApi *xessApi_ = nullptr;
    FfxApi *ffxApi_ = nullptr;
    NgxApi *ngxApi_ = nullptr;
    NrApi *nrApi_ = nullptr;
    NrChain *nrChain_ = nullptr;
    State state_ = State::Disabled;
    UpscalerFeatureDescDX12 feature_{};
    void *xessContext_ = nullptr;
    void *ffxContext_ = nullptr;
    void *dlssHandle_ = nullptr;
    std::vector<float> ffxJitter_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthClone_; // R32_FLOAT, NON_PIXEL_SHADER_RESOURCE between replays
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRoot_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> nrPso_, depthPso_;
    UINT descriptorStride_ = 0;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> immediateAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> immediateList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> immediateFence_;
    uint64_t immediateValue_ = 0;
};

} // namespace shaderapidx12
