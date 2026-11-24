//========= Copyright Valve Corporation, All rights reserved. ============//
#include "hlight_engine_bridge.h"
#include "shaderdevice_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "tier0/threadtools.h"
#include "tier1/checksum_crc.h"
#include "tier1/strtools.h"
#include "thirdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace shaderapidx12
{
namespace
{
constexpr uintptr_t kWorldBrush = 0x752638, kLoadingBrush = 0x69FDA0;
constexpr uintptr_t kMapName = 0x69F310, kSortInfo = 0x69E530, kSortCount = 0x6984C8;
constexpr uintptr_t kModelLoader = 0x470748, kOverlayManager = 0x470FC0;
constexpr uintptr_t kNativeBspHeader = 0x69EDF0, kNativeLumpOverrides = 0x69F420;
constexpr uintptr_t kNativeQueueReplay = 0x2AAE0;
constexpr uintptr_t kQueuedCallArena = 0x2D0, kQueuedRenderDepth = 0x320;
constexpr uintptr_t kMaterialSystem = 0xF1550, kMaterialSystemVtable = 0xB6470;
constexpr uintptr_t kQueuedContexts[] = {0x27A0, 0x2B50};
constexpr uintptr_t kQueuedContextVtable = 0xB5C28, kQueuedContextCallQueueVtable = 0xB6398;
constexpr uintptr_t kPendingRenderJob = 0x3080, kNewContextInitReturn = 0xE097;
constexpr uint32 kStockQueueReserve = 8u << 20, kExpandedQueueReserve = 32u << 20;
constexpr uint32 kQueueCommitStep = 64u << 10, kQueueInitialCommit = 256u << 10, kQueueAlignment = 4;
// LockRenderData uses two frame-lifetime pools, not the per-context call queue.
// Feature shadow views can submit many scenes before either pool is recycled.
constexpr uintptr_t kRenderDataPools = 0xFAA30, kRenderDataLocks = 0xFAA1C;
constexpr uint32 kRenderDataRequest = 0x44C000, kDataCommitStep = 0x8000;
constexpr uint32 kStockDataReserve = (kRenderDataRequest + kDataCommitStep - 1) & ~(kDataCommitStep - 1);
constexpr uint32 kExpandedDataReserve = 256u << 20, kDataAlignment = 32;

struct QueuedCallArena
{
    uintptr_t next, committedEnd, reservedEnd, base;
    uint32 capacity, alignment, commitStep, initialCommit;
};
static_assert(sizeof(QueuedCallArena) == 0x30 && offsetof(QueuedCallArena, capacity) == 0x20);
constexpr uintptr_t kScratch[4] = { 0x598280, 0x5D8280, 0x618280, 0x658280 };
constexpr uint32 kMaxLuxels = 16384;
constexpr uint32 kNativeBump = 8, kNativeDisplacement = 0x800;
constexpr uint16 kEnd = 0xffff;
// Both static StudioDrawModelArray and ordinary studio mesh drawing enter
// the same depth-override skin setup. Only this IsTranslucent call has the
// early return that precedes DepthWrite's original alpha-test setup.
constexpr uintptr_t kStudioDepthAdmission = 0x4353, kStudioDepthAdmissionReturn = 0x4368;
std::atomic<uintptr_t> g_StudioDepthAdmissionReturn{0};
bool g_StudioCutoutsAttempted = false;

struct Hook
{
    uintptr_t rva;
    const char *bytes;
    void *detour;
    bool material = false;
    void *original = nullptr;
    bool created = false;
};
void FaceLoadHook();
void LevelInitHook();
void GeometryHook(bool);
void UnloadHook(void *, void *);
void BuildHook(void *, void *, const void *, uint32, bool, bool);
bool PlanarHook(void *, void *, const float *, float, float);
void BumpedHook(void *, void *, const float *, float, float, const float *, const float *);
void DisplacementHook(void *, void *, uint32);
void *LumpHook(void *, uint32);
void QueuedMeshLockHook(void *, int, int, MeshDesc_t &);
bool QueuedCallArenaInitHook(QueuedCallArena *, uint32, uint32, uint32, uint32);
enum { kQueuedCallArenaInitHook = 10, kStudioCutoutMaterialHook, kStudioCutoutQueuedMaterialHook };
template<int Index> bool StudioDepthIsTranslucentHook(IMaterial *);
Hook g_Hooks[] = {
    {0x101850, "405355565741544155415641574881ec98010000488b0de5626a00bb07000000", reinterpret_cast<void *>(&FaceLoadHook)},
    {0xD9030, "4883ec28488d0db5422b00ff15cff92700488d0dc8422b00ff1532f82700660f", reinterpret_cast<void *>(&LevelInitHook)},
    {0xD92E0, "40534883ec200fb6d9e842db0100e89d97ffffe80894ffff488b0d4974390048", reinterpret_cast<void *>(&GeometryHook)},
    {0xFA960, "405355574881ec40010000488bf9488bda8b4a2083e9010f84c700000083e901", reinterpret_cast<void *>(&UnloadHook)},
    {0xCFC20, "44894c24204c89442418488954241048894c240855564154415641574881ece0", reinterpret_cast<void *>(&BuildHook)},
    {0xCEA80, "4881eca8000000f30f10413c0f57e40f2ec44c8bd10f29bc24800000000f28fb", reinterpret_cast<void *>(&PlanarHook)},
    {0xCF130, "488bc4f30f11582053555657415441564881ecb80000004c8b0dea3468004c8d", reinterpret_cast<void *>(&BumpedHook)},
    {0xC30B0, "40555741574883ec204883b9f001000000418bf84c8bfa488be9747348897424", reinterpret_cast<void *>(&DisplacementHook)},
    {0xFC790, "405356415641574883ec384863f2488bd983fe3f761441b83f000000488d0d65", reinterpret_cast<void *>(&LumpHook)},
    {0x2CC70, "48895c240848896c24104889742418574883ec2033ed8bc248396970498bd941", reinterpret_cast<void *>(&QueuedMeshLockHook), true},
    {0x772D0, "48895c24084889742410574883ec508b842480000000418bf183c00389512083", reinterpret_cast<void *>(&QueuedCallArenaInitHook), true},
    // CMaterial and CMaterial_QueueFriendly. CMaterialSubRect tail-jumps to
    // its backing material's entry, preserving this same caller return PC.
    {0x83E0, "40534883ec20488b01488bd9ff90f0010000807b2a057638488b4b504885c974", reinterpret_cast<void *>(&StudioDepthIsTranslucentHook<kStudioCutoutMaterialHook>), true},
    {0xAEA0, "40534883ec20488b01ba00000040488bd9ff90f000000084c0741b488b4b08f3", reinterpret_cast<void *>(&StudioDepthIsTranslucentHook<kStudioCutoutQueuedMaterialHook>), true}
};
constexpr int kFirstStudioCutoutHook = kStudioCutoutMaterialHook;
Hook &g_QueuedCallArenaInit = g_Hooks[kQueuedCallArenaInitHook];

template<int Index> bool StudioDepthIsTranslucentHook(IMaterial *material)
{
    auto original = reinterpret_cast<bool (*)(IMaterial *)>(g_Hooks[Index].original);
    // Skin setup executes on the hardware material context during queued
    // replay or an immediate draw. The pass stack follows that command stream
    // even when a queue drain moves replay temporarily to the main thread.
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != g_StudioDepthAdmissionReturn.load(std::memory_order_relaxed) ||
        !g_pShaderDeviceDX12 || !g_pShaderDeviceDX12->Lighting().ShadowPassActive())
        return original(material);
    return original(material) && !material->IsAlphaTested();
}

template<class T> T Load(uintptr_t address)
{
    T result;
    std::memcpy(&result, reinterpret_cast<const void *>(address), sizeof(result));
    return result;
}
bool Readable(uintptr_t address, size_t bytes)
{
    if (!bytes) return true;
    if (!address || bytes > std::numeric_limits<uintptr_t>::max() - address) return false;
    const uintptr_t end = address + bytes;
    while (address < end)
    {
        MEMORY_BASIC_INFORMATION info = {};
        if (!VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = info.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}
bool ModuleHash(HMODULE module, const char *expected)
{
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    uint8 buffer[65536], digest[32];
    while (ok)
    {
        DWORD bytes = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { ok = false; break; }
        if (!bytes) break;
        if (BCryptHashData(hash, buffer, bytes, 0) < 0) ok = false;
    }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return false;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i != sizeof(digest); ++i)
        if (hex[digest[i] >> 4] != expected[i * 2] || hex[digest[i] & 15] != expected[i * 2 + 1]) return false;
    return true;
}
bool EntryMatches(uintptr_t address, const char *hex)
{
    if (!Readable(address, 32)) return false;
    auto nibble = [](char c) -> unsigned { return c <= '9' ? unsigned(c - '0') : unsigned(c - 'a' + 10); };
    for (unsigned i = 0; i != 32; ++i)
        if (Load<uint8>(address + i) != (nibble(hex[i * 2]) << 4 | nibble(hex[i * 2 + 1]))) return false;
    return true;
}

struct FragmentKey
{
    uint32 overlay, fragment, face;
    bool operator<(const FragmentKey &b) const
    {
        if (overlay != b.overlay) return overlay < b.overlay;
        if (fragment != b.fragment) return fragment < b.fragment;
        return face < b.face;
    }
    bool operator==(const FragmentKey &b) const { return overlay == b.overlay && fragment == b.fragment && face == b.face; }
};
struct OverlayVertex
{
    std::array<uint8, 44> nonUV;
    uint32 uv[2];
};
struct OverlayFragment
{
    FragmentKey key;
    std::vector<OverlayVertex> vertices;
};
struct PreservedVertex
{
    double local[2];
    uint32 currentUV[2];
};
struct PreservedFragment
{
    FragmentKey key;
    std::vector<PreservedVertex> vertices;
};
struct ThreadState
{
    int ideal = 0, service = -1;
    bool allowed = false, changed = false;
};
struct State
{
    uintptr_t engine = 0, materialModule = 0;
    IMaterialSystem *materials = nullptr;
    IHlightNativeSink *sink = nullptr;
    std::atomic<bool> supported{false};
    bool installed = false, callbacks = false, ownsMinHook = false;
    bool faceScope = false, loadAnnounced = false;
    uint32 selectedFace = hlight::kMissing, selectedLighting = hlight::kMissing;
    char compatibility[256] = "High-resolution lightmaps: native bridge is not initialized";
    char observedMap[260] = {};
    std::array<hlight::LumpIdentity, HEADER_LUMPS> identities = {};
    std::array<bool, HEADER_LUMPS> identityPresent = {};
    std::shared_ptr<const HlightNativeDomain> domain, lastDomain;
    std::shared_ptr<HlightNativeDomain> pendingDomain;
    std::shared_ptr<const HlightNativeAtlas> atlas;
    std::vector<PreservedFragment> overlays;
    std::vector<float> beforeRGB, finalRGB;
    uint32 scratchLuxels = 0;
    uint64 nextMapGeneration = 1, nextLayoutGeneration = 1;
    uint64 releasedLayoutGeneration = 0;
    bool awaitingRestore = false;
    unsigned transactionDepth = 0;
    ThreadState threading;
    std::atomic<bool> capture{false};
    std::atomic_flag buildBusy = ATOMIC_FLAG_INIT;
    const char *builtinArenaFailure = nullptr;
    bool builtinArenasExpanded = false, builtinArenaReported = false;
    bool dataPoolsReported = false;
} g;

uintptr_t HookTarget(const Hook &hook)
{
    return (hook.material ? g.materialModule : g.engine) + hook.rva;
}

void QueueArenaWarningOnce(const char *reason)
{
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true, std::memory_order_relaxed))
        Warning("ShaderAPIDX12: render-call arena enlargement: %s\n", reason);
}

bool CreateExpandedCallArena(QueuedCallArena &arena)
{
    void *base = VirtualAlloc(nullptr, kExpandedQueueReserve, MEM_RESERVE, PAGE_NOACCESS);
    if (!base) return false;
    if (VirtualAlloc(base, kQueueInitialCommit, MEM_COMMIT, PAGE_READWRITE) != base)
    {
        VirtualFree(base, 0, MEM_RELEASE);
        return false;
    }
    const uintptr_t start = reinterpret_cast<uintptr_t>(base);
    arena = {start, start + kQueueInitialCommit, start + kExpandedQueueReserve, start,
        kExpandedQueueReserve, kQueueAlignment, kQueueCommitStep, kQueueInitialCommit};
    return true;
}

bool QueuedCallArenaInitHook(QueuedCallArena *arena, uint32 capacity, uint32 commitStep, uint32 initialCommit, uint32 alignment)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&QueuedCallArenaInitHook)>(g_Hooks[10].original)(arena, capacity, commitStep, initialCommit, alignment);
    using Init = bool (*)(QueuedCallArena *, uint32, uint32, uint32, uint32);
    const Init init = reinterpret_cast<Init>(g_QueuedCallArenaInit.original);
    const uintptr_t address = reinterpret_cast<uintptr_t>(arena);
    const uintptr_t first = g.materialModule + kRenderDataPools;
    if ((address == first || address == first + sizeof(*arena)) && capacity == kRenderDataRequest &&
        commitStep == kDataCommitStep && !initialCommit && alignment == kDataAlignment)
    {
        if (init(arena, kExpandedDataReserve, commitStep, initialCommit, alignment))
            return true;
        // No initial commit: a failed reserve holds no pages to release.
        return init(arena, capacity, commitStep, initialCommit, alignment);
    }

    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == g.materialModule + kNewContextInitReturn &&
        capacity == kStockQueueReserve && commitStep == kQueueCommitStep &&
        initialCommit == kQueueInitialCommit && alignment == kQueueAlignment)
    {
        QueuedCallArena replacement;
        if (CreateExpandedCallArena(replacement))
        {
            std::memcpy(arena, &replacement, sizeof(replacement));
            return true;
        }
        QueueArenaWarningOnce("new queued context could not reserve 32 MiB; using the SDK's 8 MiB");
    }
    return init(arena, capacity, commitStep, initialCommit, alignment);
}

const char *InspectEmptyCallArena(uintptr_t context, QueuedCallArena &arena)
{
    const uintptr_t address = context + kQueuedCallArena;
    if (!Readable(context, kQueuedRenderDepth + sizeof(int)) ||
        Load<uintptr_t>(context) != g.materialModule + kQueuedContextVtable ||
        Load<uintptr_t>(context + 8) != g.materialModule + kQueuedContextCallQueueVtable ||
        Load<uintptr_t>(context + 0x300) != address)
        return "queued-context vtable or embedded call-arena link differs";
    if (Load<uintptr_t>(context + 0x2C0) || Load<uintptr_t>(context + 0x2C8) ||
        Load<int>(context + kQueuedRenderDepth))
        return "a built-in queue still has pending calls or an open render scope";

    // Native Init can attach a persistent queued mesh before SetMode. Only
    // an in-progress mesh allocation, not the mesh object itself, blocks us.
    const uintptr_t mesh = Load<uintptr_t>(context + 0x310);
    if (mesh && (!Readable(mesh, 0x60) || Load<uintptr_t>(mesh + 0x28) != context ||
        Load<uintptr_t>(mesh + 0x50) || Load<uintptr_t>(mesh + 0x58)))
        return "a built-in queued mesh is busy or has an unexpected owner";

    arena = Load<QueuedCallArena>(address);
    MEMORY_BASIC_INFORMATION committed = {}, reserved = {};
    if (!arena.base || arena.capacity != kStockQueueReserve || arena.alignment != kQueueAlignment ||
        arena.commitStep != kQueueCommitStep || arena.initialCommit != kQueueInitialCommit ||
        arena.reservedEnd - arena.base != kStockQueueReserve ||
        arena.committedEnd <= arena.base || arena.committedEnd >= arena.reservedEnd)
        return "built-in call-arena metadata differs from the pinned profile";
    if (arena.next != arena.base)
        return "a built-in call arena still contains allocations";
    if (!(VirtualQuery(reinterpret_cast<void *>(arena.base), &committed, sizeof(committed)) &&
        reinterpret_cast<uintptr_t>(committed.AllocationBase) == arena.base &&
        reinterpret_cast<uintptr_t>(committed.BaseAddress) == arena.base &&
        committed.State == MEM_COMMIT && committed.Protect == PAGE_READWRITE && committed.Type == MEM_PRIVATE &&
        committed.RegionSize == arena.committedEnd - arena.base &&
        VirtualQuery(reinterpret_cast<void *>(arena.committedEnd), &reserved, sizeof(reserved)) &&
        reserved.AllocationBase == committed.AllocationBase && reserved.State == MEM_RESERVE &&
        reinterpret_cast<uintptr_t>(reserved.BaseAddress) == arena.committedEnd &&
        reserved.RegionSize == arena.reservedEnd - arena.committedEnd))
        return "the live committed/reserved page layout differs from the arena metadata";
    return nullptr;
}

const char *EnlargeBuiltinCallArenas()
{
    // Caller is the main thread, either before the first queued frame at
    // SetMode or in the existing level-init material transaction.
    const uintptr_t material = reinterpret_cast<uintptr_t>(g.materials);
    if (!Readable(material, kPendingRenderJob + sizeof(uintptr_t)) ||
        material != g.materialModule + kMaterialSystem ||
        Load<uintptr_t>(material) != g.materialModule + kMaterialSystemVtable)
        return "material-system singleton or vtable differs from the pinned profile";
    if (g.materials->GetThreadMode() != MATERIAL_SINGLE_THREADED)
        return "material system is not in single-threaded mode";
    if (Load<uintptr_t>(material + kPendingRenderJob))
        return "material system still holds a render job";
    if (!EntryMatches(g.materialModule + 0xE4F5, "498dbfa0270000488bcfe88c0b0200488d051d770a00488d9fd0020000488907") ||
        !EntryMatches(g.materialModule + 0xE5C1, "488d8fb003000066898745030000888747030000e8b60a0200488d0547760a00") ||
        !EntryMatches(g.materialModule + 0xE5E8, "488d9f80060000488bcb4c89afb8030000e8f28a0600ba00008000c744242004"))
        return "built-in queue constructor differs from the pinned profile";

    QueuedCallArena old[ARRAYSIZE(kQueuedContexts)], replacement[ARRAYSIZE(kQueuedContexts)] = {};
    for (size_t i = 0; i < ARRAYSIZE(kQueuedContexts); ++i)
        if (const char *failure = InspectEmptyCallArena(material + kQueuedContexts[i], old[i]))
            return failure;
    for (auto &arena : replacement)
    {
        if (CreateExpandedCallArena(arena)) continue;
        for (const auto &staged : replacement)
            if (staged.base) VirtualFree(reinterpret_cast<void *>(staged.base), 0, MEM_RELEASE);
        return "could not reserve and initially commit both replacements";
    }
    // No live functors or list nodes point inside the old reservations. Keep
    // the embedded arena objects (and the SDK's pointers to them) in place.
    for (size_t i = 0; i < ARRAYSIZE(kQueuedContexts); ++i)
        std::memcpy(reinterpret_cast<void *>(material + kQueuedContexts[i] + kQueuedCallArena),
            &replacement[i], sizeof(replacement[i]));
    for (const auto &arena : old)
        if (!VirtualFree(reinterpret_cast<void *>(arena.base), 0, MEM_RELEASE))
            QueueArenaWarningOnce("could not release a detached 8 MiB reservation");
    // The SDK owns the new reservations and releases them with the contexts,
    // including after the DX12 bridge has been shut down.
    g.builtinArenasExpanded = true;
    return nullptr;
}

// Called before queued frames start or in the pre-draw level-init transaction.
// An empty pool has no queued draw or render-data client pointing into it.
const char *SizeRenderDataPools()
{
    const uintptr_t material = reinterpret_cast<uintptr_t>(g.materials);
    if (material != g.materialModule + kMaterialSystem || Load<uintptr_t>(material + kPendingRenderJob) ||
        g.materials->GetThreadMode() != MATERIAL_SINGLE_THREADED || Load<int>(g.materialModule + kRenderDataLocks))
        return "material system not quiescent";
    auto *pools = reinterpret_cast<QueuedCallArena *>(g.materialModule + kRenderDataPools);
    if (pools[0].capacity == kExpandedDataReserve && pools[1].capacity == kExpandedDataReserve)
        return nullptr;
    for (int i = 0; i < 2; ++i)
    {
        const QueuedCallArena &pool = pools[i];
        if (!pool.base || pool.next != pool.base || pool.capacity != kStockDataReserve ||
            pool.reservedEnd - pool.base != pool.capacity || pool.alignment != kDataAlignment ||
            pool.commitStep != kDataCommitStep || pool.initialCommit)
            return "render-data pool not empty or layout differs";
    }
    void *bases[2] = {};
    for (int i = 0; i < 2; ++i)
    {
        bases[i] = VirtualAlloc(nullptr, kExpandedDataReserve, MEM_RESERVE, PAGE_NOACCESS);
        if (!bases[i])
        {
            if (bases[0]) VirtualFree(bases[0], 0, MEM_RELEASE);
            return "could not reserve both frame pools";
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        const uintptr_t old = pools[i].base;
        const uintptr_t base = reinterpret_cast<uintptr_t>(bases[i]);
        pools[i] = {base, base, base + kExpandedDataReserve, base,
            kExpandedDataReserve, kDataAlignment, kDataCommitStep, 0};
        VirtualFree(reinterpret_cast<void *>(old), 0, MEM_RELEASE);
    }
    return nullptr;
}

uint64 QueuedAllocationSize(uintptr_t arena, uint64 bytes)
{
    const uint32 alignment = Load<uint32>(arena + 0x24);
    return bytes ? (bytes + alignment - 1) & ~uint64(alignment - 1) : alignment;
}

uintptr_t QueuedCommitEnd(uintptr_t arena)
{
    // CommitTo rounds absolute addresses to 128-KiB steps, but VirtualAlloc
    // reservations need only be 64-KiB aligned. The final half-step can therefore
    // be reserved yet uncommittable; admitting it still triggers native failure.
    const uint32 commitSize = Load<uint32>(arena + 0x28);
    return Load<uintptr_t>(arena + 0x10) & ~uintptr_t(commitSize - 1);
}

void ReplayQueuedContext(IMatRenderContext *context)
{
    // Match native synchronous ReadPixels: close all producer render scopes,
    // then wait for the previous CPU render job and select the hardware context.
    const uintptr_t owner = reinterpret_cast<uintptr_t>(context);
    const int renderDepth = Load<int>(owner + kQueuedRenderDepth);
    for (int i = 0; i < renderDepth; ++i) context->EndRender();
    const MaterialLock_t lock = g.materials->Lock();
    // false retains context/render-data state and releases only consumed calls
    // and geometry. Unlock restores this queue; there is no GPU wait or mode change.
    reinterpret_cast<void (*)(void *, bool)>(g.materialModule + kNativeQueueReplay)(context, false);
    g.materials->Unlock(lock);
    for (int i = 0; i < renderDepth; ++i) context->BeginRender();
}

void QueuedMeshLockHook(void *mesh, int vertices, int indices, MeshDesc_t &desc)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&QueuedMeshLockHook)>(g_Hooks[9].original)(mesh, vertices, indices, desc);
    using Original = void (*)(void *, int, int, MeshDesc_t &);
    auto original = reinterpret_cast<Original>(g_Hooks[9].original);
    if (g.capture.load(std::memory_order_acquire))
    {
        const uintptr_t queuedMesh = reinterpret_cast<uintptr_t>(mesh);
        const uintptr_t owner = Load<uintptr_t>(queuedMesh + 0x28);
        const uintptr_t vertexArena = owner + 0x348, indexArena = owner + 0x378;
        const uint64 vertexBytes = vertices > 0 ?
            QueuedAllocationSize(vertexArena, uint64(vertices) * Load<uint16>(queuedMesh + 0x68)) : 0;
        const uint64 indexBytes = indices > 0 && Load<int>(queuedMesh + 0x6C) != MATERIAL_POINTS ?
            QueuedAllocationSize(indexArena, uint64(indices) * sizeof(uint16)) : 0;
        const uintptr_t vertexEnd = QueuedCommitEnd(vertexArena);
        const uintptr_t indexEnd = QueuedCommitEnd(indexArena);
        const bool pressure = vertexBytes > vertexEnd - Load<uintptr_t>(vertexArena) ||
            indexBytes > indexEnd - Load<uintptr_t>(indexArena);
        // One queued mesh owns both arenas. A new lock is the boundary before
        // either allocation: all earlier detached builds/draws can be consumed.
        // Never recycle a partial mesh or hide an individually oversized request.
        if (pressure && vertexBytes <= vertexEnd - Load<uintptr_t>(vertexArena + 0x18) &&
            indexBytes <= indexEnd - Load<uintptr_t>(indexArena + 0x18) &&
            !Load<uintptr_t>(queuedMesh + 0x50) && !Load<uintptr_t>(queuedMesh + 0x58) &&
            ThreadInMainThread())
        {
            CMatRenderContextPtr context(g.materials);
            if (context == reinterpret_cast<IMatRenderContext *>(owner) && context->GetCallQueue())
                ReplayQueuedContext(context);
        }
    }
    original(mesh, vertices, indices, desc);
}

void Failure(const char *reason)
{
    g.capture.store(false, std::memory_order_release);
    if (g.sink) g.sink->OnNativeFailure(reason);
}
template<class F> bool OwnedWork(F &&work)
{
    try { return work(); }
    catch (const std::bad_alloc &) { Failure("High-resolution lightmaps: native metadata allocation failed"); return false; }
}
bool BeginTransaction()
{
    if (!ThreadInMainThread()) { Failure("High-resolution lightmaps: native lifecycle left the main thread"); return false; }
    if (g.transactionDepth++) return true;
    const uintptr_t material = reinterpret_cast<uintptr_t>(g.materials);
    if (!Readable(material + 0x2F04, 0x16A))
    {
        --g.transactionDepth;
        Failure("High-resolution lightmaps: material thread state unavailable");
        return false;
    }
    g.threading.ideal = Load<int>(material + 0x2F08);
    g.threading.service = Load<int>(material + 0x2F10);
    g.threading.allowed = Load<uint8>(material + 0x306D) != 0;
    g.threading.changed = g.threading.allowed || g.materials->GetThreadMode() != MATERIAL_SINGLE_THREADED;
    if (g.threading.changed) g.materials->AllowThreading(false, g.threading.service);
    if (g.materials->GetThreadMode() != MATERIAL_SINGLE_THREADED)
    {
        if (g.threading.changed)
        {
            g.materials->AllowThreading(g.threading.allowed, g.threading.service);
            g.materials->SetThreadMode(static_cast<MaterialThreadMode_t>(g.threading.ideal), g.threading.service);
        }
        --g.transactionDepth;
        Failure("High-resolution lightmaps: material queue did not become quiescent");
        return false;
    }
    return true;
}
void EndTransaction()
{
    if (!g.transactionDepth || --g.transactionDepth) return;
    if (g.threading.changed)
    {
        g.materials->AllowThreading(g.threading.allowed, g.threading.service);
        g.materials->SetThreadMode(static_cast<MaterialThreadMode_t>(g.threading.ideal), g.threading.service);
    }
}

void Retire()
{
    g.capture.store(false, std::memory_order_release);
    if (g.domain && g.sink) g.sink->OnNativeRetire(g.domain->mapGeneration);
    g.atlas.reset();
    g.domain.reset();
    g.pendingDomain.reset();
    g.awaitingRestore = false;
    g.overlays.clear();
    g.faceScope = false;
}

bool ReadFaces(uintptr_t brush, std::vector<HlightNativeFace> &out)
{
    if (!Readable(brush, 256)) return false;
    const int count = Load<int>(brush + 216);
    const uintptr_t surfaces = Load<uintptr_t>(brush + 232), lighting = Load<uintptr_t>(brush + 240);
    if (count < 0 || count > MAX_MAP_FACES || !Readable(surfaces, size_t(count) * 64) || !Readable(lighting, size_t(count) * 64)) return false;
    out.resize(count);
    for (int i = 0; i < count; ++i)
    {
        auto &face = out[i];
        const uintptr_t light = lighting + size_t(i) * 64;
        const uint32 flags = Load<uint32>(surfaces + size_t(i) * 64);
        for (int a = 0; a != 2; ++a)
        {
            face.mins[a] = Load<int16>(light + a * 2);
            face.extents[a] = static_cast<uint32>(Load<int16>(light + 4 + a * 2));
        }
        std::memcpy(face.styles, reinterpret_cast<const void *>(light + 24), sizeof(face.styles));
        face.flags = ((flags & kNativeBump) ? hlight::kFaceBumped : 0) | ((flags & kNativeDisplacement) ? hlight::kFaceDisplacement : 0);
    }
    return true;
}
bool PrepareDynamicStorage(const HlightNativeDomain &domain)
{
    uint32 maximum = 0;
    for (const auto &face : domain.faces)
    {
        if (face.styles[0] == 255 || face.extents[0] == hlight::kMissing || face.extents[1] == hlight::kMissing) continue;
        const uint64 count = (uint64(face.extents[0]) + 1) * (uint64(face.extents[1]) + 1);
        if (count > kMaxLuxels) return false;
        maximum = std::max(maximum, static_cast<uint32>(count));
    }
    g.beforeRGB.resize(size_t(maximum) * 12);
    g.finalRGB.resize(size_t(maximum) * 12);
    g.scratchLuxels = maximum;
    return true;
}
void ReleaseCallback();
void RestoreCallback(int);
bool FindCallback(uintptr_t memory, int count, uintptr_t callback, int &index)
{
    if (count < 0 || count > 4096 || !Readable(memory, size_t(count) * sizeof(uintptr_t))) return false;
    for (int i = 0; i < count; ++i) if (Load<uintptr_t>(memory + i * sizeof(uintptr_t)) == callback) { index = i; return true; }
    return false;
}
bool RegisterCallbacks()
{
    if (g.callbacks) return true;
    const uintptr_t material = reinterpret_cast<uintptr_t>(g.materials);
    if (!Readable(material + 0x2F40, 0x34)) return false;
    int releaseIndex = -1, restoreIndex = -1;
    if (!FindCallback(Load<uintptr_t>(material + 0x2F40), Load<int>(material + 0x2F50), g.engine + 0xF7B20, releaseIndex) ||
        !FindCallback(Load<uintptr_t>(material + 0x2F60), Load<int>(material + 0x2F70), g.engine + 0xF7B50, restoreIndex)) return false;
    g.materials->AddReleaseFunc(&ReleaseCallback);
    g.materials->AddRestoreFunc(&RestoreCallback);
    int oursRelease = -1, oursRestore = -1;
    const bool ok = FindCallback(Load<uintptr_t>(material + 0x2F40), Load<int>(material + 0x2F50), reinterpret_cast<uintptr_t>(&ReleaseCallback), oursRelease) &&
        FindCallback(Load<uintptr_t>(material + 0x2F60), Load<int>(material + 0x2F70), reinterpret_cast<uintptr_t>(&RestoreCallback), oursRestore) &&
        oursRelease > releaseIndex && oursRestore > restoreIndex;
    if (!ok)
    {
        g.materials->RemoveRestoreFunc(&RestoreCallback);
        g.materials->RemoveReleaseFunc(&ReleaseCallback);
        return false;
    }
    g.callbacks = true;
    return true;
}
bool CaptureFaces(uintptr_t brush)
{
    if (g.selectedFace >= HEADER_LUMPS || g.selectedLighting >= HEADER_LUMPS ||
        !g.identityPresent[g.selectedFace] || !g.identityPresent[g.selectedLighting]) return false;
    auto domain = std::make_shared<HlightNativeDomain>();
    V_strncpy(domain->mapName, g.observedMap, sizeof(domain->mapName));
    domain->faceLump = g.selectedFace; domain->lightingLump = g.selectedLighting;
    if (!ReadFaces(brush, domain->faces)) return false;
    g.pendingDomain = std::move(domain);
    return true;
}
bool PublishDomain(uintptr_t brush, bool cached)
{
    auto domain = cached ? std::make_shared<HlightNativeDomain>() : g.pendingDomain;
    if (!domain) return false;
    if (cached)
    {
        if (!g.lastDomain) return false;
        *domain = *g.lastDomain;
        std::vector<HlightNativeFace> current;
        if (!ReadFaces(brush, current) || current.size() != domain->faces.size()) return false;
        for (size_t i = 0; i < current.size(); ++i)
        {
            const auto &a = current[i]; const auto &b = domain->faces[i];
            if (std::memcmp(a.mins, b.mins, sizeof(a.mins)) || std::memcmp(a.extents, b.extents, sizeof(a.extents)) ||
                std::memcmp(a.styles, b.styles, sizeof(a.styles)) || ((a.flags ^ b.flags) & hlight::kFaceDisplacement)) return false;
        }
        domain->faces.swap(current);
    }
    else
    {
        if (V_stricmp(domain->mapName, g.observedMap) || domain->faceLump != g.selectedFace ||
            domain->lightingLump != g.selectedLighting) return false;
        domain->identities = g.identities; domain->identityPresent = g.identityPresent;
    }
    if (!g.nextMapGeneration) return false;
    domain->mapGeneration = g.nextMapGeneration++;
    g.domain = domain;
    g.lastDomain = domain;
    g.pendingDomain.reset();
    g.compatibility[0] = 0;
    const bool enhanced = g.sink->OnNativeDomain(domain);
    if (enhanced && (!RegisterCallbacks() || !PrepareDynamicStorage(*domain)))
    {
        Failure("High-resolution lightmaps: callback order or native sample bounds mismatch");
        return false;
    }
    g.capture.store(enhanced, std::memory_order_release);
    return true;
}

bool SnapshotAtlas(std::shared_ptr<HlightNativeAtlas> &out)
{
    if (!g.domain) return false;
    const uintptr_t brush = Load<uintptr_t>(g.engine + kWorldBrush);
    if (!Readable(brush, 256) || Load<int>(brush + 216) != static_cast<int>(g.domain->faces.size())) return false;
    const uintptr_t surfaces = Load<uintptr_t>(brush + 232), lighting = Load<uintptr_t>(brush + 240);
    const uintptr_t sort = Load<uintptr_t>(g.engine + kSortInfo);
    const int sorts = Load<int>(g.engine + kSortCount);
    const uintptr_t lightmaps = reinterpret_cast<uintptr_t>(g.materials) + 0x3A0;
    if (!Readable(lightmaps, 0x5C) || sorts < 0 || sorts > 65536 || !Readable(sort, size_t(sorts) * 16)) return false;
    // Native binding checks material+0x3F8 (lightmaps+0x58). The handle
    // vector retains old tail slots when a repack reduces the active pages.
    const int count = Load<int>(lightmaps + 0x58), slots = Load<int>(lightmaps + 0x48), capacity = Load<int>(lightmaps + 0x40);
    const uintptr_t pageInfo = Load<uintptr_t>(lightmaps + 0x30), handles = Load<uintptr_t>(lightmaps + 0x38);
    if (count < 0 || count > 65536 || slots < count || capacity < slots || !Readable(pageInfo, size_t(count) * 8) || !Readable(handles, size_t(count) * 8)) return false;
    const size_t faces = g.domain->faces.size();
    if (!Readable(surfaces, faces * 64) || !Readable(lighting, faces * 64)) return false;
    auto atlas = std::make_shared<HlightNativeAtlas>();
    atlas->domain = g.domain;
    if (!g.nextLayoutGeneration) return false;
    atlas->layoutGeneration = g.nextLayoutGeneration++;
    atlas->pages.resize(count);
    for (int p = 0; p < count; ++p)
    {
        auto &page = atlas->pages[p];
        page.handle = Load<ShaderAPITextureHandle_t>(handles + size_t(p) * 8);
        page.width = Load<uint16>(pageInfo + size_t(p) * 8);
        page.height = Load<uint16>(pageInfo + size_t(p) * 8 + 2);
        if (!page.width || !page.height || page.width > 16384 || page.height > 16384) return false;
    }
    atlas->placements.resize(faces);
    for (size_t i = 0; i < faces; ++i)
    {
        const uintptr_t surface = surfaces + i * 64, light = lighting + i * 64;
        const int sortID = Load<int16>(surface + 22);
        if (sortID < 0 || sortID >= sorts) return false;
        auto &placement = atlas->placements[i];
        placement.page = Load<int32>(sort + size_t(sortID) * 16 + 8);
        if (placement.page < 0)
        {
            if (placement.page < -3) return false;
            continue;
        }
        if (placement.page >= count) return false;
        const auto &face = g.domain->faces[i];
        for (int a = 0; a != 2; ++a)
        {
            const int origin = Load<int16>(light + 8 + a * 2);
            if (origin < 1 || Load<int16>(light + a * 2) != face.mins[a] ||
                static_cast<uint32>(Load<int16>(light + 4 + a * 2)) != face.extents[a]) return false;
            placement.origin[a] = static_cast<uint32>(origin);
        }
        placement.planeCount = (Load<uint32>(surface) & kNativeBump) ? 4 : 1;
        const auto &page = atlas->pages[placement.page];
        const uint64 width = (uint64(face.extents[0]) + 3) * placement.planeCount;
        const uint64 height = uint64(face.extents[1]) + 3;
        if (placement.origin[0] - 1 + width > page.width || placement.origin[1] - 1 + height > page.height) return false;
    }
    const uintptr_t loader = Load<uintptr_t>(g.engine + kModelLoader);
    if (!Readable(loader, 0x84)) return false;
    const int modelCount = Load<int>(loader + 0x80);
    const uintptr_t models = Load<uintptr_t>(loader + 0x70);
    if (modelCount <= 0 || modelCount > MAX_MAP_MODELS || !Readable(models, size_t(modelCount) * 96)) return false;
    atlas->models.resize(modelCount);
    uint32 nextFace = 0;
    for (int i = 0; i < modelCount; ++i)
    {
        const uintptr_t model = models + size_t(i) * 96;
        if (Load<uintptr_t>(model + 0x48) != brush) return false;
        const int first = Load<int>(model + 0x50), n = Load<int>(model + 0x54);
        if (first < 0 || n < 0 || static_cast<uint32>(first) != nextFace || uint64(first) + n > faces) return false;
        atlas->models[i] = { static_cast<uint32>(first), static_cast<uint32>(n) };
        nextFace += static_cast<uint32>(n);
    }
    if (nextFace != faces) return false;
    out = std::move(atlas);
    return true;
}

// Enumerate by native overlay/list/fragment/parent identity. A writable address is
// borrowed only inside visitor; it is never stored in a cache or published snapshot.
template<class Visitor> bool VisitOverlays(const HlightNativeAtlas &atlas, Visitor &&visitor)
{
    const uintptr_t manager = g.engine + kOverlayManager;
    const uintptr_t brush = Load<uintptr_t>(g.engine + kWorldBrush);
    if (!Readable(brush, 256) || !Readable(manager, 140)) return false;
    const uintptr_t surfaces = Load<uintptr_t>(brush + 232);
    const uintptr_t overlays = Load<uintptr_t>(manager + 56), fragments = Load<uintptr_t>(manager + 88), nodes = Load<uintptr_t>(manager + 128);
    const int count = Load<int>(manager + 72), fragmentCapacity = Load<int>(manager + 96), nodeCapacity = Load<int>(manager + 136);
    if (count < 0 || count > 65535 || fragmentCapacity < 0 || fragmentCapacity > 65536 || nodeCapacity < 0 || nodeCapacity > 65536 ||
        !Readable(overlays, size_t(count) * 184) || !Readable(fragments, size_t(fragmentCapacity) * 64) || !Readable(nodes, size_t(nodeCapacity) * 6)) return false;
    std::vector<uint8> visited(size_t(nodeCapacity), 0);
    for (int i = 0; i < count; ++i)
    {
        const uintptr_t overlay = overlays + size_t(i) * 184;
        const uint32 overlayID = Load<uint32>(overlay);
        uint16 node = Load<uint16>(overlay + 8);
        while (node != kEnd)
        {
            if (node >= nodeCapacity || visited[node]) return false;
            visited[node] = 1;
            const uintptr_t item = nodes + size_t(node) * 6;
            const uint16 fragment = Load<uint16>(item);
            if (fragment >= fragmentCapacity) return false;
            const uintptr_t f = fragments + size_t(fragment) * 64;
            const uintptr_t parent = Load<uintptr_t>(f + 8), vertices = Load<uintptr_t>(f + 24);
            const int vertexCount = Load<int>(f + 40);
            if (parent < surfaces || (parent - surfaces) % 64 || (parent - surfaces) / 64 >= atlas.domain->faces.size() ||
                vertexCount < 0 || vertexCount > 65535 || !Readable(vertices, size_t(vertexCount) * 52)) return false;
            const FragmentKey key = {overlayID, fragment, static_cast<uint32>((parent - surfaces) / 64)};
            if (!visitor(key, vertices, static_cast<uint32>(vertexCount))) return false;
            node = Load<uint16>(item + 4);
        }
    }
    return true;
}
bool SnapshotOverlays(const HlightNativeAtlas &atlas, std::vector<OverlayFragment> &out)
{
    out.clear();
    if (!VisitOverlays(atlas, [&](const FragmentKey &key, uintptr_t vertices, uint32 count)
    {
        OverlayFragment f; f.key = key; f.vertices.resize(count);
        for (uint32 v = 0; v < count; ++v)
        {
            const uintptr_t p = vertices + size_t(v) * 52;
            std::memcpy(f.vertices[v].nonUV.data(), reinterpret_cast<const void *>(p), 40);
            std::memcpy(f.vertices[v].nonUV.data() + 40, reinterpret_cast<const void *>(p + 48), 4);
            std::memcpy(f.vertices[v].uv, reinterpret_cast<const void *>(p + 40), 8);
        }
        out.push_back(std::move(f)); return true;
    })) return false;
    std::sort(out.begin(), out.end(), [](const OverlayFragment &a, const OverlayFragment &b) { return a.key < b.key; });
    for (size_t i = 1; i < out.size(); ++i) if (out[i - 1].key == out[i].key) return false;
    return true;
}
// Brush overlays clip in the decal plane and then project back to the parent plane.
// Native lightmap UV generation clamps to the page, not to the parent allocation.
// Prove an outlier came from the declared parent's projection before correcting it.
bool CheckBrushOverlayProjection(uint32 faceIndex, const HlightNativeFace &face, const HlightNativePlacement &place,
    const HlightNativePage &page, const OverlayVertex &vertex, const double (&pixel)[2])
{
    if (face.flags & hlight::kFaceDisplacement) return false; // Displacement UVs are interpolated, not planar.
    const uintptr_t brush = Load<uintptr_t>(g.engine + kWorldBrush);
    const uintptr_t surface = Load<uintptr_t>(brush + 232) + size_t(faceIndex) * 64;
    if (Load<uint32>(surface) & 1) return false; // Native constant white UV, not a projection.
    const int index = Load<uint16>(surface + 26) >> 1;
    const uintptr_t texinfos = Load<uintptr_t>(brush + 176);
    if (!texinfos || index >= Load<int>(brush + 168)) return false;
    const uintptr_t texinfo = texinfos + size_t(index) * 88;
    if (!Readable(texinfo, 64)) return false;

    float position[3], normal[3];
    std::memcpy(position, vertex.nonUV.data(), sizeof(position));
    std::memcpy(normal, vertex.nonUV.data() + 12, sizeof(normal));
    for (int k = 0; k != 3; ++k)
    {
        if (!ShadowMap_IsFiniteFloat(position[k]) || !ShadowMap_IsFiniteFloat(normal[k])) return false;
        position[k] -= 0.1f * normal[k]; // Native adds this depth bias after calculating the UV.
    }
    for (int a = 0; a != 2; ++a)
    {
        const uintptr_t vec = texinfo + 32 + a * 16;
        const float s = face.extents[0] ?
            ((position[1] * Load<float>(vec + 4) + position[0] * Load<float>(vec)) +
             position[2] * Load<float>(vec + 8)) + Load<float>(vec + 12) - face.mins[a] + 0.5f : 0.5f;
        const double dimension = a ? page.height : page.width;
        if (!ShadowMap_IsFiniteFloat(s) || pixel[a] < 0.0 || pixel[a] > dimension) return false;
        const double expected = std::clamp(double(s) + place.origin[a], 0.0, dimension);
        // Allow float roundoff undoing the bias and scaling from/to page UV.
        if (std::abs(pixel[a] - expected) > 1.0 / 256.0) return false;
    }
    return true;
}

bool PreserveInitialOverlays(const HlightNativeAtlas &atlas)
{
    std::vector<OverlayFragment> current;
    if (!SnapshotOverlays(atlas, current)) return false;
    std::vector<PreservedFragment> saved;
    saved.reserve(current.size());
    size_t corrected = 0;
    for (const auto &fragment : current)
    {
        PreservedFragment f; f.key = fragment.key;
        const auto &place = atlas.placements[f.key.face];
        const auto &face = atlas.domain->faces[f.key.face];
        const auto *page = place.page >= 0 ? &atlas.pages[place.page] : nullptr;
        f.vertices.resize(fragment.vertices.size());
        for (size_t v = 0; v < f.vertices.size(); ++v)
        {
            auto &vertex = f.vertices[v];
            const auto &source = fragment.vertices[v];
            double pixel[2];
            bool outside[2] = {};
            for (int a = 0; a != 2; ++a)
            {
                vertex.currentUV[a] = source.uv[a];
                float uv;
                std::memcpy(&uv, &source.uv[a], sizeof(uv));
                if (!ShadowMap_IsFiniteFloat(uv)) return false;
                pixel[a] = page ? double(uv) * (a ? page->height : page->width) : 0;
                outside[a] = page && (pixel[a] < double(place.origin[a]) - 1.0 ||
                    pixel[a] >= double(place.origin[a]) + face.extents[a] + 2.0);
            }
            if (outside[0] || outside[1])
            {
                if (!CheckBrushOverlayProjection(f.key.face, face, place, *page, source, pixel)) return false;
                ++corrected;
                for (int a = 0; a != 2; ++a)
                {
                    if (!outside[a]) continue;
                    // Use the edge luxel center, not the allocation boundary: the
                    // provoking vertex must name the parent even after a repack.
                    const double first = place.origin[a] + 0.5;
                    pixel[a] = std::clamp(pixel[a], first, first + face.extents[a]);
                    const float uv = static_cast<float>(pixel[a] / (a ? page->height : page->width));
                    std::memcpy(&vertex.currentUV[a], &uv, sizeof(uv));
                }
            }
            for (int a = 0; a != 2; ++a)
                vertex.local[a] = pixel[a] - place.origin[a] - 0.5;
        }
        saved.push_back(std::move(f));
    }
    // The transaction keeps native spans stable between validation and commit.
    if (corrected && !VisitOverlays(atlas, [&](const FragmentKey &key, uintptr_t vertices, uint32 count)
    {
        const auto it = std::lower_bound(saved.begin(), saved.end(), key,
            [](const PreservedFragment &f, const FragmentKey &k) { return f.key < k; });
        if (it == saved.end() || !(it->key == key) || it->vertices.size() != count) return false;
        const auto &before = current[size_t(it - saved.begin())];
        for (uint32 v = 0; v < count; ++v)
        {
            const auto &uv = it->vertices[v].currentUV;
            if (std::memcmp(before.vertices[v].uv, uv, sizeof(uv)) != 0)
                std::memcpy(reinterpret_cast<void *>(vertices + size_t(v) * 52 + 40), uv, sizeof(uv));
        }
        return true;
    })) return false;
    if (corrected)
        Msg("High-resolution lightmaps: kept %zu projected overlay vertices inside their parent lightmaps\n", corrected);
    g.overlays.swap(saved);
    return true;
}
bool CheckPreservedInput(const std::vector<OverlayFragment> &before)
{
    if (before.size() != g.overlays.size()) return false;
    for (size_t i = 0; i < before.size(); ++i)
    {
        const auto &f = before[i]; const auto &saved = g.overlays[i];
        if (!(f.key == saved.key) || f.vertices.size() != saved.vertices.size()) return false;
        for (size_t v = 0; v < f.vertices.size(); ++v)
            if (std::memcmp(f.vertices[v].uv, saved.vertices[v].currentUV, 8)) return false;
    }
    return true;
}
bool RestoreOverlays(const HlightNativeAtlas &oldAtlas, const HlightNativeAtlas &newAtlas, const std::vector<OverlayFragment> &before)
{
    std::vector<OverlayFragment> after;
    if (!SnapshotOverlays(newAtlas, after) || after.size() != before.size() || !CheckPreservedInput(before)) return false;
    for (size_t i = 0; i < after.size(); ++i)
    {
        if (!(after[i].key == before[i].key) || after[i].vertices.size() != before[i].vertices.size()) return false;
        const uint32 face = after[i].key.face;
        if ((oldAtlas.placements[face].page < 0) != (newAtlas.placements[face].page < 0)) return false;
        for (size_t v = 0; v < after[i].vertices.size(); ++v)
            if (after[i].vertices[v].nonUV != before[i].vertices[v].nonUV) return false;
    }
    // Validate the complete transaction before writing any native UV.
    return VisitOverlays(newAtlas, [&](const FragmentKey &key, uintptr_t vertices, uint32 count)
    {
        auto it = std::lower_bound(g.overlays.begin(), g.overlays.end(), key,
            [](const PreservedFragment &f, const FragmentKey &k) { return f.key < k; });
        if (it == g.overlays.end() || !(it->key == key) || it->vertices.size() != count) return false;
        const size_t index = size_t(it - g.overlays.begin());
        const auto &oldPlace = oldAtlas.placements[key.face], &newPlace = newAtlas.placements[key.face];
        bool same = oldPlace.page == newPlace.page && oldPlace.origin[0] == newPlace.origin[0] && oldPlace.origin[1] == newPlace.origin[1];
        if (newPlace.page >= 0)
            same = same && oldAtlas.pages[oldPlace.page].width == newAtlas.pages[newPlace.page].width &&
                oldAtlas.pages[oldPlace.page].height == newAtlas.pages[newPlace.page].height;
        for (uint32 v = 0; v < count; ++v)
        {
            uint32 bits[2];
            for (int a = 0; a != 2; ++a)
            {
                if (same || newPlace.page < 0) bits[a] = before[index].vertices[v].uv[a];
                else
                {
                    const auto &page = newAtlas.pages[newPlace.page];
                    const uint32 dimension = a ? page.height : page.width;
                    const float uv = static_cast<float>((it->vertices[v].local[a] + newPlace.origin[a] + 0.5) / dimension);
                    std::memcpy(&bits[a], &uv, sizeof(uv));
                }
                it->vertices[v].currentUV[a] = bits[a];
            }
            std::memcpy(reinterpret_cast<void *>(vertices + size_t(v) * 52 + 40), bits, sizeof(bits));
        }
        return true;
    });
}
bool PublishAtlas(bool initial, const std::shared_ptr<const HlightNativeAtlas> &oldAtlas = {}, const std::vector<OverlayFragment> *before = nullptr)
{
    std::shared_ptr<HlightNativeAtlas> atlas;
    if (!SnapshotAtlas(atlas)) return false;
    if (initial)
    {
        if (!PreserveInitialOverlays(*atlas)) return false;
    }
    else if (before && oldAtlas)
    {
        if (!RestoreOverlays(*oldAtlas, *atlas, *before)) return false;
    }
    else if (g.atlas)
    {
        // A resource-only callback may replace handles, not secretly repack rectangles.
        if (atlas->placements.size() != g.atlas->placements.size() || atlas->pages.size() != g.atlas->pages.size()) return false;
        for (size_t i = 0; i < atlas->placements.size(); ++i)
        {
            const auto &a = atlas->placements[i], &b = g.atlas->placements[i];
            if (a.page != b.page || a.origin[0] != b.origin[0] || a.origin[1] != b.origin[1] || a.planeCount != b.planeCount) return false;
        }
        for (size_t i = 0; i < atlas->pages.size(); ++i)
            if (atlas->pages[i].width != g.atlas->pages[i].width || atlas->pages[i].height != g.atlas->pages[i].height) return false;
    }
    g.atlas = atlas;
    g.sink->OnNativeAtlas(std::move(atlas));
    return true;
}

bool WantedLump(uint32 lump)
{
    switch (lump)
    {
    case 1: case 2: case 3: case 6: case 7: case 8: case 12: case 13: case 14: case 15:
    case 26: case 30: case 31: case 32: case 33: case 34: case 43: case 44: case 48:
    case 53: case 54: case 58: return true;
    default: return false;
    }
}
void RecordLump(void *helper, uint32 requested, uintptr_t caller)
{
    if (!ThreadInMainThread() || !WantedLump(requested)) return;
    const bool faceConsumer = g.faceScope && caller == g.engine + 0x1018B6 && (requested == 7 || requested == 58);
    const bool lightingConsumer = caller == g.engine + 0x100039 && (requested == 8 || requested == 53);
    if (((requested == 7 || requested == 58) && !faceConsumer) ||
        ((requested == 8 || requested == 53) && !lightingConsumer)) return;
    const uintptr_t p = reinterpret_cast<uintptr_t>(helper);
    if (!Readable(p, 44) || !Readable(g.engine + kMapName, sizeof(g.observedMap))) { Failure("High-resolution lightmaps: invalid native lump helper"); return; }
    const char *name = reinterpret_cast<const char *>(g.engine + kMapName);
    if (!std::memchr(name, 0, sizeof(g.observedMap))) { Failure("High-resolution lightmaps: invalid native map name"); return; }
    if (V_stricmp(g.observedMap, name))
    {
        V_strncpy(g.observedMap, name, sizeof(g.observedMap));
        g.identities = {}; g.identityPresent = {};
        g.selectedFace = g.selectedLighting = hlight::kMissing;
        g.loadAnnounced = false;
    }
    if (!g.loadAnnounced)
    {
        // Collision loading reaches this first, seconds before the face domain: time to prepare the pak assets.
        g.loadAnnounced = true;
        g.sink->OnNativeMapLoad(name);
    }
    const int bytes = Load<int>(p), selected = Load<int>(p + 40), version = Load<int>(p + 8);
    const uintptr_t data = Load<uintptr_t>(p + 24);
    if (bytes < 0 || selected != static_cast<int>(requested) || version < 0 || !Readable(data, size_t(bytes)))
    { Failure("High-resolution lightmaps: invalid effective native lump span"); return; }
    hlight::LumpIdentity identity = {};
    identity.lump = requested; identity.version = static_cast<uint32>(version); identity.bytes = static_cast<uint32>(bytes);
    identity.crc32 = ShadowMap_CRC32(reinterpret_cast<const void *>(data), static_cast<uint32>(bytes));
    g.identities[requested] = identity; g.identityPresent[requested] = true;
    // Native loading skips vertices/triangles when effective displacement info
    // is empty. Capture proven empty sources before native shutdown clears the
    // directory; never substitute base-file bytes for an effective override.
    if (requested == LUMP_DISPINFO && !bytes)
    {
        const uintptr_t header = g.engine + kNativeBspHeader;
        if (!Readable(header, 8 + HEADER_LUMPS * 16) ||
            Load<uint32>(header) != IDBSPHEADER ||
            (Load<int>(header + 4) != 19 && Load<int>(header + 4) != 20))
        { Failure("High-resolution lightmaps: invalid native empty-lump directory"); return; }
        for (uint32 lump : { uint32(LUMP_DISP_VERTS), uint32(LUMP_DISP_TRIS) })
        {
            const uintptr_t entry = header + 8 + lump * 16;
            const uintptr_t slot = g.engine + kNativeLumpOverrides + lump * 32;
            if (!Readable(slot, 32))
            { Failure("High-resolution lightmaps: unreadable native empty-lump selection"); return; }
            const bool overridden = Load<uintptr_t>(slot) != 0;
            const int selectedBytes = Load<int>(overridden ? slot + 24 : entry + 4);
            const int selectedVersion = Load<int>(overridden ? slot + 20 : entry + 8);
            if (selectedBytes || selectedVersion < 0 || Load<uint32>(entry + 12))
                continue; // Nonempty/compressed sources still require actual helper capture.
            hlight::LumpIdentity empty = {};
            empty.lump = lump; empty.version = uint32(selectedVersion);
            empty.crc32 = ShadowMap_CRC32(nullptr, 0);
            g.identities[lump] = empty; g.identityPresent[lump] = true;
        }
    }
    if (lightingConsumer) g.selectedLighting = requested;
    if (faceConsumer) g.selectedFace = requested;
}

struct BuildScope
{
    void *surface = nullptr;
    uint64 generation = 0;
    uint32 ordinal = 0, width = 0, height = 0, mask = 0;
    bool first = false, valid = true;
};
thread_local BuildScope *t_Build = nullptr;
void CopyScratch(std::vector<float> &destination, const BuildScope &scope)
{
    const size_t count = size_t(scope.width) * scope.height;
    for (unsigned plane = 0; plane != 4; ++plane)
    {
        if (!(scope.mask & (1u << plane))) continue;
        const float *source = reinterpret_cast<const float *>(g.engine + kScratch[plane]);
        float *target = destination.data() + size_t(plane) * g.scratchLuxels * 3;
        for (size_t i = 0; i < count; ++i)
        {
            target[i * 3] = source[i * 4]; target[i * 3 + 1] = source[i * 4 + 1]; target[i * 3 + 2] = source[i * 4 + 2];
        }
    }
}
void BeforeAccumulator(void *surface, void *displacement)
{
    if (!t_Build || !t_Build->valid) return;
    if ((surface && surface != t_Build->surface) ||
        (displacement && Load<uintptr_t>(reinterpret_cast<uintptr_t>(t_Build->surface) + 28) != reinterpret_cast<uintptr_t>(displacement)))
    {
        t_Build->valid = false; Failure("High-resolution lightmaps: dynamic accumulator escaped its native surface scope"); return;
    }
    if (!t_Build->first) { CopyScratch(g.beforeRGB, *t_Build); t_Build->first = true; }
}
void AfterAccumulator()
{
    if (t_Build && t_Build->valid && t_Build->first) CopyScratch(g.finalRGB, *t_Build);
}

// A validated .light drops the selected lights from the loaded HDR worldlights and turns off the BSP's baked
// static-prop lighting (its VHVs still contain those lights), in the engine's helper buffer, after RecordLump has
// fingerprinted the lump as stored. The file itself is never written.
void PatchLump(void *helper, uint32 requested)
{
    if ((requested != LUMP_MAP_FLAGS && requested != LUMP_WORLDLIGHTS_HDR) || !ThreadInMainThread() || !g.sink) return;
    const uintptr_t p = reinterpret_cast<uintptr_t>(helper);
    if (!Readable(p, 44) || !Readable(g.engine + kMapName, sizeof(g.observedMap))) return;
    const char *name = reinterpret_cast<const char *>(g.engine + kMapName);
    if (!std::memchr(name, 0, sizeof(g.observedMap)) || Load<int>(p + 40) != static_cast<int>(requested)) return;
    HlightSidecarPatch patch;
    if (!g.sink->SidecarLumpPatch(name, patch)) return;
    const int bytes = Load<int>(p), version = Load<int>(p + 8);
    uint8 *data = reinterpret_cast<uint8 *>(Load<uintptr_t>(p + 24));
    if (bytes < 0 || !Readable(reinterpret_cast<uintptr_t>(data), size_t(bytes))) return;
    if (requested == LUMP_MAP_FLAGS)
    {
        if (bytes < int(sizeof(uint32))) return;
        uint32 flags; std::memcpy(&flags, data, sizeof(flags));
        flags &= ~uint32(LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_HDR | LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_NONHDR);
        std::memcpy(data, &flags, sizeof(flags));
        Msg("High-resolution lightmaps: %s: static props lit from the worldlights, not the BSP's VHVs\n", name);
        return;
    }
    // Version-1 worldlights (Left 4 Dead 2 BSP 21) are 100-byte records; the adapter converts them after this.
    const uint32 record = version == 1 ? 100u : uint32(sizeof(dworldlight_t));
    if (uint32(bytes) % record || ShadowMap_CRC32(data, uint32(bytes)) != patch.worldlightsCRC32)
    {
        Warning("High-resolution lightmaps: %s worldlights differ from the .light's; selected lights stay in the engine's lighting\n", name);
        return;
    }
    const uint32 count = uint32(bytes) / record;
    uint32 kept = 0; size_t removed = 0;
    for (uint32 i = 0; i < count; ++i)
    {
        if (removed < patch.removedWorldlights.size() && patch.removedWorldlights[removed] == i) { ++removed; continue; }
        if (kept != i) std::memmove(data + size_t(kept) * record, data + size_t(i) * record, record);
        ++kept;
    }
    *reinterpret_cast<int *>(p) = int(kept * record);
    Msg("High-resolution lightmaps: %s: %u of %u worldlights handed to the runtime shadow-mapped lights\n", name, count - kept, count);
}
void *LumpHook(void *helper, uint32 lump)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&LumpHook)>(g_Hooks[8].original)(helper, lump);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    void *result = reinterpret_cast<void *(*)(void *, uint32)>(g_Hooks[8].original)(helper, lump);
    OwnedWork([&] { RecordLump(helper, lump, caller); PatchLump(helper, lump); return true; });
    return result;
}
void FaceLoadHook()
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&FaceLoadHook)>(g_Hooks[0].original)();
    const bool transaction = BeginTransaction();
    if (transaction) Retire();
    g.faceScope = true; g.selectedFace = hlight::kMissing;
    reinterpret_cast<void (*)()>(g_Hooks[0].original)();
    g.faceScope = false;
    if (transaction && !OwnedWork([&] { return CaptureFaces(Load<uintptr_t>(g.engine + kLoadingBrush)); }))
        Failure("High-resolution lightmaps: effective face-domain capture failed");
    if (transaction) EndTransaction();
}
void LevelInitHook()
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&LevelInitHook)>(g_Hooks[1].original)();
    const bool transaction = BeginTransaction();
    if (transaction && !g.dataPoolsReported)
    {
        const char *failure = SizeRenderDataPools();
        if (failure)
            Warning("ShaderAPIDX12: frame render-data pools unchanged: %s\n", failure);
        else
            Msg("ShaderAPIDX12: frame render-data pools: 256 MiB x 2, committed on demand\n");
        g.dataPoolsReported = true;
    }
    if (!g.builtinArenaReported && ThreadInMainThread())
    {
        g.builtinArenaReported = true;
        // Console logging is not yet active at the first startup SetMode.
        // If it was not quiescent, this pre-draw transaction is our fallback.
        const char *startupFailure = g.builtinArenaFailure;
        if (!g.builtinArenasExpanded && transaction)
            g.builtinArenaFailure = EnlargeBuiltinCallArenas();
        if (g.builtinArenasExpanded)
            Msg("ShaderAPIDX12: queued call arenas: 8 MiB -> 32 MiB (2/2); initial commit 256 KiB each\n");
        else
            Warning("ShaderAPIDX12: queued call arenas left at 8 MiB: %s\n", g.builtinArenaFailure);
        if (startupFailure && transaction)
            DevMsg("ShaderAPIDX12: startup arena enlargement deferred: %s\n", startupFailure);
    }
    if (transaction && !g.domain && (g.pendingDomain || g.lastDomain) &&
        !OwnedWork([&] { return PublishDomain(Load<uintptr_t>(g.engine + kWorldBrush), !g.pendingDomain); }))
        Failure("High-resolution lightmaps: complete native domain publication failed");
    if (transaction) OwnedWork([&] { g.sink->OnNativeLevelInit(g.materials); return true; });
    reinterpret_cast<void (*)()>(g_Hooks[1].original)();
    if (transaction && g.capture.load(std::memory_order_acquire) && !OwnedWork([&] { return PublishAtlas(true); }))
        Failure("High-resolution lightmaps: initial native atlas or overlay identity is invalid");
    if (transaction) EndTransaction();
}
void GeometryHook(bool client)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&GeometryHook)>(g_Hooks[2].original)(client);
    const bool enhanced = g.capture.load(std::memory_order_acquire);
    const bool transaction = enhanced && BeginTransaction();
    const bool outer = transaction && g.transactionDepth == 1;
    const auto oldAtlas = g.atlas;
    std::vector<OverlayFragment> before;
    bool preserved = true;
    if (outer && oldAtlas)
        preserved = OwnedWork([&] { return SnapshotOverlays(*oldAtlas, before) && CheckPreservedInput(before); });
    reinterpret_cast<void (*)(bool)>(g_Hooks[2].original)(client);
    if (outer && g.capture.load(std::memory_order_acquire))
    {
        if (!preserved || !OwnedWork([&] { return oldAtlas ? PublishAtlas(false, oldAtlas, &before) : PublishAtlas(true); }))
            Failure("High-resolution lightmaps: rebuilt overlay identity or native atlas is invalid");
    }
    if (transaction) EndTransaction();
}
void UnloadHook(void *loader, void *model)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&UnloadHook)>(g_Hooks[3].original)(loader, model);
    const uintptr_t p = reinterpret_cast<uintptr_t>(model);
    const bool brush = Readable(p, 0x24) && Load<int>(p + 0x20) == 1;
    const bool transaction = brush && BeginTransaction();
    if (transaction) Retire();
    reinterpret_cast<void (*)(void *, void *)>(g_Hooks[3].original)(loader, model);
    if (transaction)
    {
        // Cached-domain reuse is allowed only while the native brush model survives.
        g.lastDomain.reset();
        g.identities = {}; g.identityPresent = {};
        g.observedMap[0] = 0;
        g.selectedFace = g.selectedLighting = hlight::kMissing;
        g.loadAnnounced = false;
        EndTransaction();
    }
}
void BuildHook(void *dlights, void *surface, const void *matrix, uint32 mask, bool bump, bool base)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&BuildHook)>(g_Hooks[4].original)(dlights, surface, matrix, mask, bump, base);
    using Original = void (*)(void *, void *, const void *, uint32, bool, bool);
    auto original = reinterpret_cast<Original>(g_Hooks[4].original);
    if (!g.capture.load(std::memory_order_acquire)) { original(dlights, surface, matrix, mask, bump, base); return; }
    if (g.buildBusy.test_and_set(std::memory_order_acquire))
    {
        BuildScope *outer = t_Build;
        if (outer) outer->valid = false;
        t_Build = nullptr;
        Failure("High-resolution lightmaps: overlapping native lightmap build scopes");
        original(dlights, surface, matrix, mask, bump, base);
        t_Build = outer;
        return;
    }
    BuildScope scope;
    const uintptr_t brush = Load<uintptr_t>(g.engine + kWorldBrush);
    const uintptr_t first = Load<uintptr_t>(brush + 232), p = reinterpret_cast<uintptr_t>(surface);
    if (!g.domain || p < first || (p - first) % 64 || (p - first) / 64 >= g.domain->faces.size()) scope.valid = false;
    if (scope.valid)
    {
        scope.ordinal = static_cast<uint32>((p - first) / 64);
        const auto &face = g.domain->faces[scope.ordinal];
        scope.width = face.extents[0] + 1; scope.height = face.extents[1] + 1;
        scope.valid = scope.width && scope.height && uint64(scope.width) * scope.height <= g.scratchLuxels;
        scope.surface = surface; scope.generation = g.domain->mapGeneration;
        scope.mask = (base ? 1u : 0u) | (bump ? 14u : 0u);
    }
    if (!scope.valid) Failure("High-resolution lightmaps: native dynamic face does not belong to its captured domain");
    t_Build = &scope;
    original(dlights, surface, matrix, mask, bump, base);
    t_Build = nullptr;
    if (scope.valid && g.capture.load(std::memory_order_acquire))
    {
        const float *planes[4] = {};
        if (scope.first)
        {
            const size_t values = size_t(scope.width) * scope.height * 3;
            for (unsigned plane = 0; plane != 4; ++plane)
            {
                if (!(scope.mask & (1u << plane))) continue;
                float *last = g.finalRGB.data() + size_t(plane) * g.scratchLuxels * 3;
                const float *before = g.beforeRGB.data() + size_t(plane) * g.scratchLuxels * 3;
                for (size_t i = 0; i < values; ++i) last[i] -= before[i];
                planes[plane] = last;
            }
        }
        OwnedWork([&] { g.sink->OnNativeDynamic(scope.generation, scope.ordinal, scope.first ? scope.mask : 0, scope.width, scope.height, planes); return true; });
    }
    g.buildBusy.clear(std::memory_order_release);
}
bool PlanarHook(void *light, void *surface, const float *position, float perpendicular, float radius)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&PlanarHook)>(g_Hooks[5].original)(light, surface, position, perpendicular, radius);
    BeforeAccumulator(surface, nullptr);
    const bool result = reinterpret_cast<bool (*)(void *, void *, const float *, float, float)>(g_Hooks[5].original)(light, surface, position, perpendicular, radius);
    AfterAccumulator();
    return result;
}
void BumpedHook(void *light, void *surface, const float *position, float perpendicular, float radius, const float *basis, const float *base)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&BumpedHook)>(g_Hooks[6].original)(light, surface, position, perpendicular, radius, basis, base);
    BeforeAccumulator(surface, nullptr);
    reinterpret_cast<void (*)(void *, void *, const float *, float, float, const float *, const float *)>(g_Hooks[6].original)(light, surface, position, perpendicular, radius, basis, base);
    AfterAccumulator();
}
void DisplacementHook(void *displacement, void *lights, uint32 mask)
{
    if (!g.supported.load(std::memory_order_acquire)) return reinterpret_cast<decltype(&DisplacementHook)>(g_Hooks[7].original)(displacement, lights, mask);
    BeforeAccumulator(nullptr, displacement);
    reinterpret_cast<void (*)(void *, void *, uint32)>(g_Hooks[7].original)(displacement, lights, mask);
    AfterAccumulator();
}
void ReleaseCallback()
{
    if (!g.domain || !g.capture.load(std::memory_order_acquire)) return;
    const bool transaction = BeginTransaction();
    if (transaction)
    {
        g.releasedLayoutGeneration = g.atlas ? g.atlas->layoutGeneration : 0;
        g.awaitingRestore = true;
        g.sink->OnNativeResourceRelease(g.domain->mapGeneration);
        EndTransaction();
    }
}
void RestoreCallback(int)
{
    if (!g.domain || !g.capture.load(std::memory_order_acquire)) return;
    if (g.awaitingRestore && g.atlas && g.atlas->layoutGeneration != g.releasedLayoutGeneration)
    {
        g.awaitingRestore = false;
        return; // The enclosed geometry hook already published this restore.
    }
    const bool transaction = BeginTransaction();
    if (transaction)
    {
        if (!OwnedWork([&] { return PublishAtlas(false); })) Failure("High-resolution lightmaps: resource restore changed unobserved native layout");
        g.awaitingRestore = false;
        EndTransaction();
    }
}
bool RemoveHooks()
{
    g_StudioDepthAdmissionReturn.store(0, std::memory_order_relaxed);
    bool queued = true;
    for (const auto &hook : g_Hooks)
        if (hook.created) queued = (MH_QueueDisableHook(reinterpret_cast<void *>(HookTarget(hook))) == MH_OK) && queued;
    if (!queued || MH_ApplyQueued() != MH_OK)
        return false;
    for (auto &hook : g_Hooks)
    {
        if (hook.created && MH_RemoveHook(reinterpret_cast<void *>(HookTarget(hook))) != MH_OK)
            return false;
        hook.created = false; hook.original = nullptr;
    }
    if (g.ownsMinHook && MH_Uninitialize() != MH_OK) return false;
    g.ownsMinHook = false;
    return true;
}
}

namespace HlightEngineBridge
{
static bool InstallHooks()
{
    const HMODULE engine = GetModuleHandleW(L"engine.dll"), material = GetModuleHandleW(L"materialsystem.dll");
    if (!engine || !material)
    { V_strncpy(g.compatibility, "High-resolution lightmaps: installed engine/material system unavailable", sizeof(g.compatibility)); return false; }
    if (!ModuleHash(engine, "adba182f610ae3ccac3e42430e23c3eefc3f5f3b6b36d1b7b09225ec6016c5a6") ||
        !ModuleHash(material, "dd8059fa55f22e8088a6b9ec33adc7425ea857f916b3264b8055159a4bc8d4f2"))
    { V_strncpy(g.compatibility, "High-resolution lightmaps: unsupported engine/material-system build", sizeof(g.compatibility)); return false; }
    g.engine = reinterpret_cast<uintptr_t>(engine); g.materialModule = reinterpret_cast<uintptr_t>(material);
    for (const Hook *hook = g_Hooks; hook != g_Hooks + kFirstStudioCutoutHook; ++hook)
        if (hook != &g_QueuedCallArenaInit && !EntryMatches(HookTarget(*hook), hook->bytes))
        { V_strncpy(g.compatibility, "High-resolution lightmaps: native entry signature differs from the supported profile", sizeof(g.compatibility)); return false; }
    if (!EntryMatches(g.materialModule + kNativeQueueReplay, "405356415641574883ec38488b0586fe0c004c8db17803000033db48897c2468"))
    { V_strncpy(g.compatibility, "High-resolution lightmaps: native queue replay signature differs from the supported profile", sizeof(g.compatibility)); return false; }
    const bool canHookQueueArena = EntryMatches(HookTarget(g_QueuedCallArenaInit), g_QueuedCallArenaInit.bytes) &&
        EntryMatches(g.materialModule + 0xE076, "ba00008000c74424200400000041b90000040041b800000100488bcbe8399206");
    if (!canHookQueueArena)
    { V_strncpy(g.compatibility, "High-resolution lightmaps: queued-context initializer signature differs from the supported profile", sizeof(g.compatibility)); return false; }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
    { V_strncpy(g.compatibility, "High-resolution lightmaps: entry-hook initialization failed", sizeof(g.compatibility)); return false; }
    g.ownsMinHook = init == MH_OK;
    for (Hook *entry = g_Hooks; entry != g_Hooks + kFirstStudioCutoutHook; ++entry)
    {
        Hook &hook = *entry;
        const MH_STATUS status = MH_CreateHook(reinterpret_cast<void *>(HookTarget(hook)), hook.detour, &hook.original);
        if (status != MH_OK)
        {
            RemoveHooks();
            V_snprintf(g.compatibility, sizeof(g.compatibility), "High-resolution lightmaps: entry-hook creation failed at RVA %zx (%s)", hook.rva, MH_StatusToString(status));
            return false;
        }
        hook.created = true;
    }
    bool queued = true;
    for (const auto &hook : g_Hooks)
        if (hook.created) queued = (MH_QueueEnableHook(reinterpret_cast<void *>(HookTarget(hook))) == MH_OK) && queued;
    if (!queued || MH_ApplyQueued() != MH_OK)
    { RemoveHooks(); V_strncpy(g.compatibility, "High-resolution lightmaps: atomic entry-hook activation failed", sizeof(g.compatibility)); return false; }
    return true;
}
extern "C" __declspec(dllexport) bool HlightBridge_EarlyInstall(char *error, size_t bytes)
{
    g.installed = InstallHooks();
    if (error && bytes) strncpy_s(error, bytes, g.installed ? "" : g.compatibility, _TRUNCATE);
    return g.installed;
}
// LauncherMain can fail or return before a renderer is activated.
extern "C" __declspec(dllexport) bool HlightBridge_RetirePreload()
{
    if (g.supported.load()) return false;
    return !g.ownsMinHook || RemoveHooks();
}
bool Initialize(IMaterialSystem *materials, IHlightNativeSink *sink)
{
    if (g.supported) return materials == g.materials && sink == g.sink;
    if (!materials || !sink || !ThreadInMainThread())
    { V_strncpy(g.compatibility, "High-resolution lightmaps: bridge requires main-thread material-system initialization", sizeof(g.compatibility)); return false; }
    if (!g.installed)
    {
        char loadError[160] = {};
        if (GetEnvironmentVariableA("HLIGHT_PRELOAD_ERROR", loadError, sizeof(loadError)))
            V_snprintf(g.compatibility, sizeof(g.compatibility), "High-resolution lightmaps: %s", loadError);
        return false;
    }
    g.materials = materials; g.sink = sink;
    const uintptr_t currentBrush = Load<uintptr_t>(g.engine + kWorldBrush);
    const bool lateAttach = Readable(currentBrush, 256) && Load<int>(currentBrush + 216) != 0;
    g.builtinArenaFailure = lateAttach ? "world already loaded during device initialization" : EnlargeBuiltinCallArenas();
    if (!lateAttach)
        SizeRenderDataPools();
    g.supported = true; g.compatibility[0] = 0;
    if (lateAttach) V_strncpy(g.compatibility, "High-resolution lightmaps: existing world was not observed; clean map reload required", sizeof(g.compatibility));
    return true;
}
void EnsureStudioShadowCutouts()
{
    if (g_StudioCutoutsAttempted || !g.supported) return;
    g_StudioCutoutsAttempted = true;
    const HMODULE studio = GetModuleHandleW(L"studiorender.dll");
    const uintptr_t base = reinterpret_cast<uintptr_t>(studio);
    if (!studio || !ModuleHash(studio, "4765b37069e67127a76687364da66e6bd29b4305db1cb7ae0be27beaed003cc6") ||
        !EntryMatches(base + kStudioDepthAdmission, "8b01ff90d80000004b8b0cfe488b01ff908800000084c00f85b00300004b8b0c"))
    {
        Warning("ShaderAPIDX12: studio cutout shadow admission unavailable: unsupported studiorender depth signature\n");
        return;
    }
    for (int i = kFirstStudioCutoutHook; i < ARRAYSIZE(g_Hooks); ++i)
    {
        const Hook &hook = g_Hooks[i];
        if (!EntryMatches(HookTarget(hook), hook.bytes))
        {
            Warning("ShaderAPIDX12: studio cutout shadow admission unavailable: unsupported material signature\n");
            return;
        }
    }
    MH_STATUS status = MH_OK;
    for (int i = kFirstStudioCutoutHook; i < ARRAYSIZE(g_Hooks) && status == MH_OK; ++i)
    {
        Hook &hook = g_Hooks[i];
        void *target = reinterpret_cast<void *>(HookTarget(hook));
        status = MH_CreateHook(target, hook.detour, &hook.original);
        if (status == MH_OK)
        {
            hook.created = true;
            status = MH_QueueEnableHook(target);
        }
    }
    if (status == MH_OK) status = MH_ApplyQueued();
    if (status != MH_OK)
    {
        for (int i = kFirstStudioCutoutHook; i < ARRAYSIZE(g_Hooks); ++i)
        {
            Hook &hook = g_Hooks[i];
            if (hook.created && MH_RemoveHook(reinterpret_cast<void *>(HookTarget(hook))) == MH_OK)
            {
                hook.created = false;
                hook.original = nullptr;
            }
        }
        Warning("ShaderAPIDX12: studio cutout shadow admission unavailable: %s\n", MH_StatusToString(status));
        return;
    }
    g_StudioDepthAdmissionReturn.store(base + kStudioDepthAdmissionReturn, std::memory_order_relaxed);
}
void Shutdown()
{
    if (!g.supported) return;
    const bool transaction = BeginTransaction();
    if (!transaction) Error("High-resolution lightmaps: cannot shut down native hooks without quiescing material jobs\n");
    Retire();
    if (g.callbacks)
    {
        g.materials->RemoveRestoreFunc(&RestoreCallback);
        g.materials->RemoveReleaseFunc(&ReleaseCallback);
        g.callbacks = false;
    }
    g.supported.store(false, std::memory_order_release);
    if (!RemoveHooks()) Error("High-resolution lightmaps: cannot retire native entry hooks\n");
    g.installed = false;
    EndTransaction();
    g.sink = nullptr; g.materials = nullptr;
    g.lastDomain.reset(); g.identityPresent = {}; g.observedMap[0] = 0;
    g.beforeRGB.clear(); g.finalRGB.clear(); g.scratchLuxels = 0;
    V_strncpy(g.compatibility, "High-resolution lightmaps: native bridge is shut down", sizeof(g.compatibility));
}
bool Supported() { return g.supported; }
const char *CompatibilityError() { return g.compatibility; }
void BeginClientLevelShutdown()
{
    if (g.supported && BeginTransaction()) Retire();
}
void EndClientLevelShutdown()
{
    if (g.supported) EndTransaction();
}
bool BeginClientResourceReadmission()
{
    return g.supported && g.domain && g.capture.load(std::memory_order_acquire) && BeginTransaction();
}
void EndClientResourceReadmission()
{
    EndTransaction();
}
bool BeginMaterialTransaction()
{
    return g.supported && BeginTransaction();
}
void EndMaterialTransaction()
{
    EndTransaction();
}
void DrainShadowQueueIfNeeded(IMatRenderContext *context)
{
    if (!g.supported || !ThreadInMainThread() || !context->GetCallQueue()) return;

    // Functors and their list nodes share the render-call arena. Leave half
    // free for the next complete caster submission and the commands needed
    // to suspend and reopen the producer render scope.
    // This is independent of high-resolution lightmap capture.
    const uintptr_t arena = reinterpret_cast<uintptr_t>(context) + kQueuedCallArena;
    const uintptr_t first = Load<uintptr_t>(arena + 0x18);
    const uintptr_t next = Load<uintptr_t>(arena);
    const uintptr_t end = QueuedCommitEnd(arena);
    if (first && first < end && next > first && next - first >= (end - first) / 2)
        ReplayQueuedContext(context);
}
}
}
