#ifndef L4D2_BSP21_ADAPTER_H
#define L4D2_BSP21_ADAPTER_H
#pragma once

// Sole process owner: call on the engine main thread during server DLLInit,
// after interfaces exist but BEFORE addon/mission reads. The pinned engine
// initializes the server before the client. Never call from DllMain.
// Guards the engine/filesystem ABI, corrects VPK v1 embedded-data offsets, and
// admits mounted BSP21 without rewriting source bytes. Converted layouts and
// their references are validated; unchanged geometry retains SDK v20 trust.
// A later DLLInit failure must remove the hooks before returning false.
bool L4D2_BSP21Install();
// Call from that same thread after level/prop users stop, while filesystem,
// material system, tier libraries and the caller's module reference are live.
// Internally pauses/drains native producers, disables entries, drains every
// wrapper/native/trampoline call, and verifies owner instruction windows.
// Checks retained BSP raw-file objects after that barrier, before owner release.
// false retains the owner module but is NOT a recoverable running state: a
// DLLShutdown/Unload caller MUST use Plat_FatalError, never Host_Error.
bool L4D2_BSP21Remove();

#endif
