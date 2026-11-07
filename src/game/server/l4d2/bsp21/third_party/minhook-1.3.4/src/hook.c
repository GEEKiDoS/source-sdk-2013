/*
 *  MinHook - The Minimalistic API Hooking Library for x64/x86
 *  Copyright (C) 2009-2017 Tsuda Kageyu.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 *  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <limits.h>

#include "../include/MinHook.h"
#include "buffer.h"
#include "trampoline.h"

#ifndef ARRAYSIZE
    #define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

// Initial capacity of the HOOK_ENTRY buffer.
#define INITIAL_HOOK_CAPACITY   32

// Initial capacity of the thread IDs buffer.
#define INITIAL_THREAD_CAPACITY 128

// Special hook position values.
#define INVALID_HOOK_POS UINT_MAX
#define ALL_HOOKS_POS    UINT_MAX

// Freeze() action argument defines.
#define ACTION_DISABLE      0
#define ACTION_ENABLE       1
#define ACTION_APPLY_QUEUED 2

// Thread access rights for suspending/resuming threads.
#define THREAD_ACCESS \
    (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SET_CONTEXT | SYNCHRONIZE)

// Hook information.
typedef struct _HOOK_ENTRY
{
    LPVOID pTarget;             // Address of the target function.
    LPVOID pDetour;             // Address of the detour or relay function.
    LPVOID pTrampoline;         // Address of the trampoline function.
    UINT8  backup[8];           // Original prologue of the target function.

    UINT8  patchAbove  : 1;     // Uses the hot patch area.
    UINT8  isEnabled   : 1;     // Enabled.
    UINT8  queueEnable : 1;     // Queued for enabling/disabling when != isEnabled.

    UINT   nIP : 4;             // Count of the instruction boundaries.
    UINT8  oldIPs[8];           // Instruction boundaries of the target function.
    UINT8  newIPs[8];           // Instruction boundaries of the trampoline function.
} HOOK_ENTRY, *PHOOK_ENTRY;

// BSP21 local hardening: retain the exact opened thread handles and original
// contexts until the transaction commits or rolls back.
typedef struct _FROZEN_THREAD
{
    DWORD id;
    HANDLE handle;
    CONTEXT context;
    BOOL suspended;
    BOOL changed;
} FROZEN_THREAD;
typedef struct _FROZEN_THREADS
{
    FROZEN_THREAD *pItems;
    UINT capacity;
    UINT size;
} FROZEN_THREADS, *PFROZEN_THREADS;

//-------------------------------------------------------------------------
// Global Variables:
//-------------------------------------------------------------------------

// Spin lock flag for EnterSpinLock()/LeaveSpinLock().
volatile LONG g_isLocked = FALSE;

// Private heap handle. If not NULL, this library is initialized.
HANDLE g_hHeap = NULL;

// Hook entries.
struct
{
    PHOOK_ENTRY pItems;     // Data heap
    UINT        capacity;   // Size of allocated data heap, items
    UINT        size;       // Actual number of data items
} g_hooks;

static ULONG_PTR g_bspOwner;
static SIZE_T g_bspOwnerBytes;
static volatile LONG *g_bspActive;
static const char *g_bspFailure = "none";
static DWORD g_bspFailureThread, g_bspFailureError;

VOID WINAPI MH_BSP21SetOwner(LPVOID image, SIZE_T bytes, volatile LONG *active)
{
    g_bspOwner = (ULONG_PTR)image;
    g_bspOwnerBytes = bytes;
    g_bspActive = active;
}

const char *WINAPI MH_BSP21Failure(DWORD *threadId, DWORD *win32Error)
{
    *threadId = g_bspFailureThread;
    *win32Error = g_bspFailureError;
    return g_bspFailure;
}

static MH_STATUS ThreadFailure(MH_STATUS status, const char *operation, DWORD id, DWORD error)
{
    g_bspFailure = operation;
    g_bspFailureThread = id;
    g_bspFailureError = error;
    return status;
}

static __declspec(noreturn) VOID ThreadRestoreFatal(const char *operation, DWORD id)
{
    ThreadFailure(MH_ERROR_THREAD_CONTEXT, operation, id, GetLastError());
    // Continuing while a context or suspend count cannot be restored is not
    // safe. Avoid allocator/logger/loader locks held by a suspended thread.
    OutputDebugStringA(operation);
    RaiseFailFastException(NULL, NULL, 0);
    TerminateProcess(GetCurrentProcess(), ERROR_INVALID_STATE);
    for (;;) Sleep(INFINITE);
}

//-------------------------------------------------------------------------
// Returns INVALID_HOOK_POS if not found.
static UINT FindHookEntry(LPVOID pTarget)
{
    UINT i;
    for (i = 0; i < g_hooks.size; ++i)
    {
        if ((ULONG_PTR)pTarget == (ULONG_PTR)g_hooks.pItems[i].pTarget)
            return i;
    }

    return INVALID_HOOK_POS;
}

//-------------------------------------------------------------------------
static PHOOK_ENTRY AddHookEntry()
{
    if (g_hooks.pItems == NULL)
    {
        g_hooks.capacity = INITIAL_HOOK_CAPACITY;
        g_hooks.pItems = (PHOOK_ENTRY)HeapAlloc(
            g_hHeap, 0, g_hooks.capacity * sizeof(HOOK_ENTRY));
        if (g_hooks.pItems == NULL)
            return NULL;
    }
    else if (g_hooks.size >= g_hooks.capacity)
    {
        PHOOK_ENTRY p = (PHOOK_ENTRY)HeapReAlloc(
            g_hHeap, 0, g_hooks.pItems, (g_hooks.capacity * 2) * sizeof(HOOK_ENTRY));
        if (p == NULL)
            return NULL;

        g_hooks.capacity *= 2;
        g_hooks.pItems = p;
    }

    return &g_hooks.pItems[g_hooks.size++];
}

//-------------------------------------------------------------------------
static VOID DeleteHookEntry(UINT pos)
{
    if (pos < g_hooks.size - 1)
        g_hooks.pItems[pos] = g_hooks.pItems[g_hooks.size - 1];

    g_hooks.size--;

    if (g_hooks.capacity / 2 >= INITIAL_HOOK_CAPACITY && g_hooks.capacity / 2 >= g_hooks.size)
    {
        PHOOK_ENTRY p = (PHOOK_ENTRY)HeapReAlloc(
            g_hHeap, 0, g_hooks.pItems, (g_hooks.capacity / 2) * sizeof(HOOK_ENTRY));
        if (p == NULL)
            return;

        g_hooks.capacity /= 2;
        g_hooks.pItems = p;
    }
}

//-------------------------------------------------------------------------
static DWORD_PTR FindOldIP(PHOOK_ENTRY pHook, DWORD_PTR ip)
{
    UINT i;

    if (pHook->patchAbove && ip == ((DWORD_PTR)pHook->pTarget - sizeof(JMP_REL)))
        return (DWORD_PTR)pHook->pTarget;

    for (i = 0; i < pHook->nIP; ++i)
    {
        if (ip == ((DWORD_PTR)pHook->pTrampoline + pHook->newIPs[i]))
            return (DWORD_PTR)pHook->pTarget + pHook->oldIPs[i];
    }

#if defined(_M_X64) || defined(__x86_64__)
    // Check relay function.
    if (ip == (DWORD_PTR)pHook->pDetour)
        return (DWORD_PTR)pHook->pTarget;
#endif

    return 0;
}

//-------------------------------------------------------------------------
static DWORD_PTR FindNewIP(PHOOK_ENTRY pHook, DWORD_PTR ip)
{
    UINT i;
    for (i = 0; i < pHook->nIP; ++i)
    {
        if (ip == ((DWORD_PTR)pHook->pTarget + pHook->oldIPs[i]))
            return (DWORD_PTR)pHook->pTrampoline + pHook->newIPs[i];
    }

    return 0;
}

//-------------------------------------------------------------------------
static MH_STATUS ProcessThreadIPs(FROZEN_THREAD *thread, UINT pos, UINT action)
{
    CONTEXT c = thread->context;
#if defined(_M_X64) || defined(__x86_64__)
    DWORD64 *pIP = &c.Rip;
#else
    DWORD *pIP = &c.Eip;
#endif
    UINT count;
    BOOL changed = FALSE;
    if (pos == ALL_HOOKS_POS) { pos = 0; count = g_hooks.size; }
    else count = pos + 1;
    for (; pos < count; ++pos)
    {
        PHOOK_ENTRY hook = &g_hooks.pItems[pos];
        BOOL enable = action == ACTION_ENABLE ? TRUE :
            action == ACTION_DISABLE ? FALSE : hook->queueEnable;
        DWORD_PTR ip;
        if (hook->isEnabled == enable) continue;
        ip = enable ? FindNewIP(hook, *pIP) : FindOldIP(hook, *pIP);
        if (!ip) continue;
        // Never migrate an uncounted pre-install native invocation into a
        // trampoline whose lifetime is subsequently protected by wrapper counts.
        if (enable)
            return ThreadFailure(MH_ERROR_IN_FLIGHT, "native patch entry active", thread->id, 0);
        *pIP = ip;
        changed = TRUE;
    }
    if (changed)
    {
        thread->changed = TRUE;
        if (!SetThreadContext(thread->handle, &c))
            return ThreadFailure(MH_ERROR_THREAD_CONTEXT, "SetThreadContext", thread->id, GetLastError());
    }
    return MH_OK;
}

static BOOL EnumerateThreads(PFROZEN_THREADS threads)
{
    BOOL succeeded = FALSE;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (Thread32First(snapshot, &te))
        {
            succeeded = TRUE;
            do
            {
                if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD) &&
                    te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId())
                {
                    if (threads->size == threads->capacity)
                    {
                        UINT capacity = threads->capacity ? threads->capacity * 2 : INITIAL_THREAD_CAPACITY;
                        FROZEN_THREAD *items = threads->pItems ?
                            (FROZEN_THREAD *)HeapReAlloc(g_hHeap, 0, threads->pItems, capacity * sizeof(*items)) :
                            (FROZEN_THREAD *)HeapAlloc(g_hHeap, 0, capacity * sizeof(*items));
                        if (!items) { succeeded = FALSE; break; }
                        threads->pItems = items;
                        threads->capacity = capacity;
                    }
                    ZeroMemory(&threads->pItems[threads->size], sizeof(FROZEN_THREAD));
                    threads->pItems[threads->size++].id = te.th32ThreadID;
                }
                te.dwSize = sizeof(te);
            } while (Thread32Next(snapshot, &te));
            if (succeeded && GetLastError() != ERROR_NO_MORE_FILES) succeeded = FALSE;
        }
        CloseHandle(snapshot);
    }
    return succeeded;
}

static VOID Unfreeze(PFROZEN_THREADS threads)
{
    UINT i;
    for (i = threads->size; i > 0; --i)
    {
        FROZEN_THREAD *thread = &threads->pItems[i - 1];
        if (thread->suspended && ResumeThread(thread->handle) == (DWORD)-1)
            ThreadRestoreFatal("BSP21 ResumeThread failed; process cannot continue", thread->id);
        if (thread->handle) CloseHandle(thread->handle);
    }
    if (threads->pItems) HeapFree(g_hHeap, 0, threads->pItems);
    threads->pItems = NULL;
    threads->size = threads->capacity = 0;
}

// NtGetNextThread enumerates kernel thread objects without allocating a
// Toolhelp snapshot while other threads may hold process-heap locks.
typedef LONG (NTAPI *BSP_NT_GET_NEXT_THREAD)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
typedef BOOL (WINAPI *BSP_COMPARE_OBJECT_HANDLES)(HANDLE, HANDLE);
static MH_STATUS CheckFrozenRoster(PFROZEN_THREADS threads, BSP_NT_GET_NEXT_THREAD nextThread,
    BSP_COMPARE_OBJECT_HANDLES sameObject)
{
    HANDLE cursor = NULL, next = NULL;
    MH_STATUS status = MH_OK;
    LONG result;
    for (;;)
    {
        DWORD id, wait;
        UINT i;
        result = nextThread(GetCurrentProcess(), cursor, THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, 0, 0, &next);
        if (cursor) CloseHandle(cursor);
        cursor = NULL;
        if (result == (LONG)0x8000001A) break; // STATUS_NO_MORE_ENTRIES
        if (result < 0)
        {
            status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "NtGetNextThread", 0, (DWORD)result);
            break;
        }
        cursor = next;
        id = GetThreadId(cursor);
        if (!id)
        {
            status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "GetThreadId", 0, GetLastError());
            break;
        }
        if (id == GetCurrentThreadId()) continue;
        // Kernel enumeration also exposes terminated objects retained by a
        // handle, which Toolhelp omits. A signaled thread cannot execute code.
        wait = WaitForSingleObject(cursor, 0);
        if (wait == WAIT_OBJECT_0) continue;
        if (wait != WAIT_TIMEOUT)
        {
            status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "thread termination wait", id,
                wait == WAIT_FAILED ? GetLastError() : wait);
            break;
        }
        for (i = 0; i < threads->size; ++i)
            if (threads->pItems[i].suspended && threads->pItems[i].id == id &&
                sameObject(threads->pItems[i].handle, cursor)) break;
        if (i == threads->size)
        {
            status = ThreadFailure(MH_ERROR_THREAD_CHANGED, "thread created during freeze", id, 0);
            break;
        }
    }
    if (cursor) CloseHandle(cursor);
    return status;
}

static MH_STATUS FreezeAttempt(PFROZEN_THREADS threads, UINT pos, UINT action, BOOL *retry)
{
    MH_STATUS status = MH_OK;
    UINT i;
    BSP_NT_GET_NEXT_THREAD nextThread = (BSP_NT_GET_NEXT_THREAD)
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtGetNextThread");
    BSP_COMPARE_OBJECT_HANDLES sameObject;
    DWORD error = nextThread ? ERROR_SUCCESS : GetLastError();
    ZeroMemory(threads, sizeof(*threads));
    *retry = FALSE;
    if (!nextThread)
        return ThreadFailure(MH_ERROR_THREAD_FREEZE, "NtGetNextThread unavailable", 0, error);
    // Resolve before suspending anyone; do not enter the loader while frozen.
    sameObject = (BSP_COMPARE_OBJECT_HANDLES)GetProcAddress(
        GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles");
    if (!sameObject)
        return ThreadFailure(MH_ERROR_THREAD_FREEZE, "CompareObjectHandles unavailable", 0, GetLastError());
    if (!EnumerateThreads(threads))
        status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "EnumerateThreads", 0, GetLastError());
    for (i = 0; status == MH_OK && i < threads->size; ++i)
    {
        DWORD owner;
        FROZEN_THREAD *thread = &threads->pItems[i];
        thread->handle = OpenThread(THREAD_ACCESS, FALSE, thread->id);
        if (!thread->handle)
        {
            error = GetLastError();
            // A stale Toolhelp ID is not proof of death. Abandon this attempt.
            *retry = error == ERROR_INVALID_PARAMETER;
            status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "OpenThread", thread->id, error);
        }
        else if (!(owner = GetProcessIdOfThread(thread->handle)))
            status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "GetProcessIdOfThread", thread->id, GetLastError());
        else if (owner != GetCurrentProcessId())
        {
            // Do not suspend an unrelated process after an ID-reuse race.
            status = ThreadFailure(MH_ERROR_THREAD_CHANGED, "thread owner changed", thread->id, 0);
            *retry = TRUE;
        }
        else if (SuspendThread(thread->handle) == (DWORD)-1)
        {
            DWORD wait;
            error = GetLastError(); // preserve SuspendThread's error across the wait
            wait = WaitForSingleObject(thread->handle, 0);
            if (wait == WAIT_OBJECT_0)
                continue; // this exact retained object has exited; never resume it
            if (wait == WAIT_TIMEOUT)
            {
                // Still unsignaled: NEVER count it as frozen or omit it.
                *retry = error == ERROR_ACCESS_DENIED;
                status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "SuspendThread; thread unsignaled", thread->id, error);
            }
            else
                status = ThreadFailure(MH_ERROR_THREAD_FREEZE, "SuspendThread liveness wait", thread->id,
                    wait == WAIT_FAILED ? GetLastError() : wait);
        }
        else thread->suspended = TRUE;
    }
    if (status == MH_OK)
    {
        status = CheckFrozenRoster(threads, nextThread, sameObject);
        *retry = status == MH_ERROR_THREAD_CHANGED;
    }
    // Capture every context before altering any one of them.
    for (i = 0; status == MH_OK && i < threads->size; ++i)
    {
        FROZEN_THREAD *thread = &threads->pItems[i];
        if (!thread->suspended) continue;
        thread->context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(thread->handle, &thread->context))
            status = ThreadFailure(MH_ERROR_THREAD_CONTEXT, "GetThreadContext", thread->id, GetLastError());
    }
    for (i = 0; status == MH_OK && i < threads->size; ++i)
        if (threads->pItems[i].suspended)
            status = ProcessThreadIPs(&threads->pItems[i], pos, action);
    if (status != MH_OK)
    {
        // No patch has been written. Restore every possibly altered context.
        for (i = 0; i < threads->size; ++i)
            if (threads->pItems[i].changed &&
                !SetThreadContext(threads->pItems[i].handle, &threads->pItems[i].context))
                ThreadRestoreFatal("BSP21 context rollback failed; process cannot continue", threads->pItems[i].id);
        Unfreeze(threads);
    }
    return status;
}

// Retry only pre-patch roster acquisition. Every failed attempt has already
// restored contexts, balanced our suspensions and closed its exact handles.
// Identity, context, in-flight, patch and restoration failures are not retried.
static MH_STATUS Freeze(PFROZEN_THREADS threads, UINT pos, UINT action)
{
    MH_STATUS status;
    UINT attempt;
    for (attempt = 0; attempt < 3; ++attempt)
    {
        BOOL retry;
        status = FreezeAttempt(threads, pos, action, &retry);
        if (status == MH_OK || !retry || attempt == 2)
            return status;
        Sleep(0); // no other thread remains suspended by this transaction
    }
    return status;
}

static MH_STATUS OwnerIdle(PFROZEN_THREADS threads)
{
    UINT i;
    if (g_bspActive && InterlockedCompareExchange(g_bspActive, 0, 0))
        return ThreadFailure(MH_ERROR_IN_FLIGHT, "active hook/native/trampoline calls", 0, 0);
    for (i = 0; i < threads->size; ++i)
    {
#if defined(_M_X64) || defined(__x86_64__)
        ULONG_PTR ip = (ULONG_PTR)threads->pItems[i].context.Rip;
#else
        ULONG_PTR ip = (ULONG_PTR)threads->pItems[i].context.Eip;
#endif
        if (threads->pItems[i].suspended &&
            g_bspOwner && ip >= g_bspOwner && ip - g_bspOwner < g_bspOwnerBytes)
            return ThreadFailure(MH_ERROR_IN_FLIGHT, "owner entry/exit instruction active", threads->pItems[i].id, 0);
    }
    return MH_OK;
}

//-------------------------------------------------------------------------
static MH_STATUS EnableHookLL(UINT pos, BOOL enable)
{
    PHOOK_ENTRY pHook = &g_hooks.pItems[pos];
    DWORD  oldProtect;
    SIZE_T patchSize    = sizeof(JMP_REL);
    LPBYTE pPatchTarget = (LPBYTE)pHook->pTarget;

    if (pHook->patchAbove)
    {
        pPatchTarget -= sizeof(JMP_REL);
        patchSize    += sizeof(JMP_REL_SHORT);
    }

    if (!VirtualProtect(pPatchTarget, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        ThreadRestoreFatal("BSP21 frozen patch transaction failed; contexts/resources retained", GetCurrentThreadId());

    if (enable)
    {
        PJMP_REL pJmp = (PJMP_REL)pPatchTarget;
        pJmp->opcode = 0xE9;
        pJmp->operand = (UINT32)((LPBYTE)pHook->pDetour - (pPatchTarget + sizeof(JMP_REL)));

        if (pHook->patchAbove)
        {
            PJMP_REL_SHORT pShortJmp = (PJMP_REL_SHORT)pHook->pTarget;
            pShortJmp->opcode = 0xEB;
            pShortJmp->operand = (UINT8)(0 - (sizeof(JMP_REL_SHORT) + sizeof(JMP_REL)));
        }
    }
    else
    {
        if (pHook->patchAbove)
            memcpy(pPatchTarget, pHook->backup, sizeof(JMP_REL) + sizeof(JMP_REL_SHORT));
        else
            memcpy(pPatchTarget, pHook->backup, sizeof(JMP_REL));
    }

    if (!VirtualProtect(pPatchTarget, patchSize, oldProtect, &oldProtect))
        ThreadRestoreFatal("BSP21 patched page protection restore failed", GetCurrentThreadId());
    if (!FlushInstructionCache(GetCurrentProcess(), pPatchTarget, patchSize))
        ThreadRestoreFatal("BSP21 patched instruction cache flush failed", GetCurrentThreadId());

    pHook->isEnabled   = enable;
    pHook->queueEnable = enable;

    return MH_OK;
}

//-------------------------------------------------------------------------
static MH_STATUS EnableAllHooksLL(BOOL enable)
{
    MH_STATUS status = MH_OK;
    UINT i, first = INVALID_HOOK_POS;

    for (i = 0; i < g_hooks.size; ++i)
    {
        if (g_hooks.pItems[i].isEnabled != enable)
        {
            first = i;
            break;
        }
    }

    if (first != INVALID_HOOK_POS)
    {
        FROZEN_THREADS threads;
        status = Freeze(&threads, ALL_HOOKS_POS, enable ? ACTION_ENABLE : ACTION_DISABLE);
        if (status == MH_OK)
        {
            for (i = first; i < g_hooks.size; ++i)
            {
                if (g_hooks.pItems[i].isEnabled != enable)
                {
                    status = EnableHookLL(i, enable);
                    if (status != MH_OK)
                        break;
                }
            }

            Unfreeze(&threads);
        }
    }

    return status;
}

//-------------------------------------------------------------------------
static VOID EnterSpinLock(VOID)
{
    SIZE_T spinCount = 0;

    // Wait until the flag is FALSE.
    while (InterlockedCompareExchange(&g_isLocked, TRUE, FALSE) != FALSE)
    {
        // No need to generate a memory barrier here, since InterlockedCompareExchange()
        // generates a full memory barrier itself.

        // Prevent the loop from being too busy.
        if (spinCount < 32)
            Sleep(0);
        else
            Sleep(1);

        spinCount++;
    }
}

//-------------------------------------------------------------------------
static VOID LeaveSpinLock(VOID)
{
    // No need to generate a memory barrier here, since InterlockedExchange()
    // generates a full memory barrier itself.

    InterlockedExchange(&g_isLocked, FALSE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_Initialize(VOID)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap == NULL)
    {
        g_hHeap = HeapCreate(0, 0, 0);
        if (g_hHeap != NULL)
        {
            // Initialize the internal function buffer.
            InitializeBuffer();
        }
        else
        {
            status = MH_ERROR_MEMORY_ALLOC;
        }
    }
    else
    {
        status = MH_ERROR_ALREADY_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_Uninitialize(VOID)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        status = EnableAllHooksLL(FALSE);
        if (status == MH_OK)
        {
            FROZEN_THREADS threads;
            status = Freeze(&threads, ALL_HOOKS_POS, ACTION_DISABLE);
            if (status == MH_OK)
            {
                status = OwnerIdle(&threads);
                if (status == MH_OK)
                {
                    UninitializeBuffer();
                    HeapFree(g_hHeap, 0, g_hooks.pItems);
                    g_hooks.pItems = NULL;
                    g_hooks.capacity = g_hooks.size = 0;
                }
                Unfreeze(&threads);
                if (status == MH_OK)
                {
                    HeapDestroy(g_hHeap);
                    g_hHeap = NULL;
                    g_bspOwner = g_bspOwnerBytes = 0;
                    g_bspActive = NULL;
                }
            }
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID *ppOriginal)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        if (IsExecutableAddress(pTarget) && IsExecutableAddress(pDetour))
        {
            UINT pos = FindHookEntry(pTarget);
            if (pos == INVALID_HOOK_POS)
            {
                LPVOID pBuffer = AllocateBuffer(pTarget);
                if (pBuffer != NULL)
                {
                    TRAMPOLINE ct;

                    ct.pTarget     = pTarget;
                    ct.pDetour     = pDetour;
                    ct.pTrampoline = pBuffer;
                    if (CreateTrampolineFunction(&ct))
                    {
                        PHOOK_ENTRY pHook = AddHookEntry();
                        if (pHook != NULL)
                        {
                            pHook->pTarget     = ct.pTarget;
#if defined(_M_X64) || defined(__x86_64__)
                            pHook->pDetour     = ct.pRelay;
#else
                            pHook->pDetour     = ct.pDetour;
#endif
                            pHook->pTrampoline = ct.pTrampoline;
                            pHook->patchAbove  = ct.patchAbove;
                            pHook->isEnabled   = FALSE;
                            pHook->queueEnable = FALSE;
                            pHook->nIP         = ct.nIP;
                            memcpy(pHook->oldIPs, ct.oldIPs, ARRAYSIZE(ct.oldIPs));
                            memcpy(pHook->newIPs, ct.newIPs, ARRAYSIZE(ct.newIPs));

                            // Back up the target function.

                            if (ct.patchAbove)
                            {
                                memcpy(
                                    pHook->backup,
                                    (LPBYTE)pTarget - sizeof(JMP_REL),
                                    sizeof(JMP_REL) + sizeof(JMP_REL_SHORT));
                            }
                            else
                            {
                                memcpy(pHook->backup, pTarget, sizeof(JMP_REL));
                            }

                            if (ppOriginal != NULL)
                                *ppOriginal = pHook->pTrampoline;
                        }
                        else
                        {
                            status = MH_ERROR_MEMORY_ALLOC;
                        }
                    }
                    else
                    {
                        status = MH_ERROR_UNSUPPORTED_FUNCTION;
                    }

                    if (status != MH_OK)
                    {
                        FreeBuffer(pBuffer);
                    }
                }
                else
                {
                    status = MH_ERROR_MEMORY_ALLOC;
                }
            }
            else
            {
                status = MH_ERROR_ALREADY_CREATED;
            }
        }
        else
        {
            status = MH_ERROR_NOT_EXECUTABLE;
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_RemoveHook(LPVOID pTarget)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        UINT pos = FindHookEntry(pTarget);
        if (pos != INVALID_HOOK_POS)
        {
            if (g_hooks.pItems[pos].isEnabled)
            {
                FROZEN_THREADS threads;
                status = Freeze(&threads, pos, ACTION_DISABLE);
                if (status == MH_OK)
                {
                    status = EnableHookLL(pos, FALSE);

                    Unfreeze(&threads);
                }
            }

            if (status == MH_OK)
            {
                FreeBuffer(g_hooks.pItems[pos].pTrampoline);
                DeleteHookEntry(pos);
            }
        }
        else
        {
            status = MH_ERROR_NOT_CREATED;
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
static MH_STATUS EnableHook(LPVOID pTarget, BOOL enable)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        if (pTarget == MH_ALL_HOOKS)
        {
            status = EnableAllHooksLL(enable);
        }
        else
        {
            UINT pos = FindHookEntry(pTarget);
            if (pos != INVALID_HOOK_POS)
            {
                if (g_hooks.pItems[pos].isEnabled != enable)
                {
                    FROZEN_THREADS threads;
                    status = Freeze(&threads, pos, enable ? ACTION_ENABLE : ACTION_DISABLE);
                    if (status == MH_OK)
                    {
                        status = EnableHookLL(pos, enable);

                        Unfreeze(&threads);
                    }
                }
                else
                {
                    status = enable ? MH_ERROR_ENABLED : MH_ERROR_DISABLED;
                }
            }
            else
            {
                status = MH_ERROR_NOT_CREATED;
            }
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget)
{
    return EnableHook(pTarget, TRUE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_DisableHook(LPVOID pTarget)
{
    return EnableHook(pTarget, FALSE);
}

//-------------------------------------------------------------------------
static MH_STATUS QueueHook(LPVOID pTarget, BOOL queueEnable)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        if (pTarget == MH_ALL_HOOKS)
        {
            UINT i;
            for (i = 0; i < g_hooks.size; ++i)
                g_hooks.pItems[i].queueEnable = queueEnable;
        }
        else
        {
            UINT pos = FindHookEntry(pTarget);
            if (pos != INVALID_HOOK_POS)
            {
                g_hooks.pItems[pos].queueEnable = queueEnable;
            }
            else
            {
                status = MH_ERROR_NOT_CREATED;
            }
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID pTarget)
{
    return QueueHook(pTarget, TRUE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID pTarget)
{
    return QueueHook(pTarget, FALSE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_ApplyQueued(VOID)
{
    MH_STATUS status = MH_OK;
    UINT i, first = INVALID_HOOK_POS;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        for (i = 0; i < g_hooks.size; ++i)
        {
            if (g_hooks.pItems[i].isEnabled != g_hooks.pItems[i].queueEnable)
            {
                first = i;
                break;
            }
        }

        if (first != INVALID_HOOK_POS)
        {
            FROZEN_THREADS threads;
            status = Freeze(&threads, ALL_HOOKS_POS, ACTION_APPLY_QUEUED);
            if (status == MH_OK)
            {
                for (i = first; i < g_hooks.size; ++i)
                {
                    PHOOK_ENTRY pHook = &g_hooks.pItems[i];
                    if (pHook->isEnabled != pHook->queueEnable)
                    {
                        status = EnableHookLL(i, pHook->queueEnable);
                        if (status != MH_OK)
                            break;
                    }
                }

                Unfreeze(&threads);
            }
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_CreateHookApiEx(
    LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour,
    LPVOID *ppOriginal, LPVOID *ppTarget)
{
    HMODULE hModule;
    LPVOID  pTarget;

    hModule = GetModuleHandleW(pszModule);
    if (hModule == NULL)
        return MH_ERROR_MODULE_NOT_FOUND;

    pTarget = (LPVOID)GetProcAddress(hModule, pszProcName);
    if (pTarget == NULL)
        return MH_ERROR_FUNCTION_NOT_FOUND;

    if (ppTarget != NULL)
        *ppTarget = pTarget;

    return MH_CreateHook(pTarget, pDetour, ppOriginal);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_CreateHookApi(
    LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID *ppOriginal)
{
    return MH_CreateHookApiEx(pszModule, pszProcName, pDetour, ppOriginal, NULL);
}

//-------------------------------------------------------------------------
const char *WINAPI MH_StatusToString(MH_STATUS status)
{
#define MH_ST2STR(x)    \
    case x:             \
        return #x;

    switch (status) {
        MH_ST2STR(MH_UNKNOWN)
        MH_ST2STR(MH_OK)
        MH_ST2STR(MH_ERROR_ALREADY_INITIALIZED)
        MH_ST2STR(MH_ERROR_NOT_INITIALIZED)
        MH_ST2STR(MH_ERROR_ALREADY_CREATED)
        MH_ST2STR(MH_ERROR_NOT_CREATED)
        MH_ST2STR(MH_ERROR_ENABLED)
        MH_ST2STR(MH_ERROR_DISABLED)
        MH_ST2STR(MH_ERROR_NOT_EXECUTABLE)
        MH_ST2STR(MH_ERROR_UNSUPPORTED_FUNCTION)
        MH_ST2STR(MH_ERROR_MEMORY_ALLOC)
        MH_ST2STR(MH_ERROR_MEMORY_PROTECT)
        MH_ST2STR(MH_ERROR_MODULE_NOT_FOUND)
        MH_ST2STR(MH_ERROR_FUNCTION_NOT_FOUND)
        MH_ST2STR(MH_ERROR_THREAD_FREEZE)
        MH_ST2STR(MH_ERROR_THREAD_CONTEXT)
        MH_ST2STR(MH_ERROR_THREAD_CHANGED)
        MH_ST2STR(MH_ERROR_IN_FLIGHT)
    }

#undef MH_ST2STR

    return "(unknown)";
}
