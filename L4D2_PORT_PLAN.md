# Left 4 Dead 2 reconstruction and x64 SDK port plan

## 1. Goal and architectural decision

Reconstruct the installed L4D2 `client.dll` and `server.dll` as native x64 game modules for this workspace's TF2-era Source SDK 2013 Multiplayer runtime. The pinned original's observable behavior is the specification, **including its bugs**.

This is a faithful reconstruction and engine-branch port, not a redesign, a bug-fix project, or mechanical recompilation of decompiler output. Preserve original class/function responsibilities, state machines, update order and recognizable code structure wherever evidence supports them. A playable slice is an implementation milestone, not permission to replace the remaining behavior with approximations.

Decisions:

- Keep the current SDK's engine-facing ABI, allocation rules and networking envelopes. Recover L4D2-specific state, rules, prediction and presentation without importing newer-branch gameplay fixes or changing behavior for convenience.
- Use the original installation as the behavioral oracle and asset source. Never overwrite its DLLs, archives, or assets.
- Use `E:/SourceEngine` to explain the TF2-side implementation; Alien Swarm and CS:GO explain L4D-lineage mechanisms. None is proof of the exact installed L4D2 implementation.
- Do not paste older interface headers or vtables into the target SDK. Even an unchanged interface version string can conceal appended methods.
- Do not assume compatibility with retail L4D2 servers, demos, saves, plugins, or matchmaking. The target engine has its own protocol and services. Preserve game-visible behavior and script/entity contracts; assess external compatibility explicitly rather than advertising a drop-in replacement.
- Prefer original content mounted without modification. Try native readers first, then a narrow, evidence-backed runtime compatibility change where needed; keep offline conversion as a fallback, not the default asset pipeline.
- Do not silently drop engine-dependent features. Investigate supported extension points and narrowly scoped hooks before concluding that conversion or another engine is necessary. This planning task does not install hooks or patch an executable; a wholesale engine replacement remains a separate scope decision.

**Status:** planning and local investigation only. No game implementation, SDK build, gameplay comparison, or asset conversion has been completed by this planning pass.

### 1.1 Fidelity contract: original behavior, including bugs

1. Pin the game update, mode, difficulty, tick interval, cvars, content order and addon state for each comparison. The installed original is the oracle; TF2, ASW, CS:GO, community fixes and personal expectations are not.
2. Preserve demonstrated quirks in movement, collision, damage, weapon timing, inventory, animation, AI, Director decisions, scripts and UI. Do not add clamps, retries, validation, sorting, cooldown adjustments or “better” AI unless the original performs them at that point.
3. Recover causes, not special cases. If a bug results from operation ordering, reproduce that ordering rather than hard-coding the recorded input or map name. Preserve RNG calls and their order, even when a seemingly redundant call or failed action affects later outcomes.
4. For every confirmed bug, retain a small original/port reproduction: preconditions, input/tick sequence, observed outcome and related function evidence. The regression expects the original result, not a corrected one. A port-only defect must still be fixed.
5. Preserve field widths, overflow/rounding behavior, comparison boundaries and ownership transitions that affect observable results. Do not reproduce x86 undefined behavior by blindly copying unsafe pointer arithmetic into x64 code; reproduce a stable observed result with explicit semantics where possible.
6. A platform-dependent fault, race or crash that cannot yet be reproduced reliably is an unresolved fidelity case, not permission to silently fix or simulate it. Record the exact original result and the x64 limitation. Since 2026-10-22 the user has delegated these decisions to the main (coordinating) agent only: workers pass each case to it, and it chooses the option closest to original L4D2 behavior that is also easy to implement, without asking the user, and records the decision in `L4D2_LEDGER.md`.
7. Do not introduce a “legacy versus fixed” mode, configurable compatibility framework or bug-fix switchboard. There is one intended behavior: the pinned original. Fixes can be a separately requested project after parity.

### 1.2 Keep the implementation small and Valve-shaped

Use the existing game DLLs, VPC build, Source interfaces and engine facilities. Do not add an ECS, service container, generic event bus, replacement serializer, universal hook manager, cross-engine abstraction or code-generation system to support this port. Add a helper only for an actual repeated operation or required ownership boundary.

Keep one baseline manifest and one small Markdown evidence ledger. The ledger holds linked function/subsystem evidence plus feature rows; M1 capability results are a section of those rows, and M8 closes the same record rather than creating another report. Milestone “Keep” lists describe inputs/captures or updates to these records, not a new document per step. Raw captures stay local. No tracking database or custom test/replay platform; expand evidence only for the next end-to-end behavior.

## 2. Evidence and reference roles

Paths below are local evidence anchors, not portable dependencies to embed in production code.

| Source | Role | Established constraints |
|---|---|---|
| Current workspace | Build and runtime contract; reusable licensed game/framework code | `README.md:1-33` identifies the HL2/HL2DM/TF2 SDK and Windows build requirements. This is not an L4D2 project. |
| `I:/SteamLibrary/steamapps/common/Left 4 Dead 2/` | Authoritative installed gameplay, binaries, scripts, and assets | Pin the exact client/server and content revision before deriving behavior. Do not mix observations from different updates. |
| `E:/SourceEngine` | Older same-tree TF2 engine implementation reference | Full engine/tool/game reference, but older contracts and incomplete evidence of a whole-engine x64 build. |
| `I:/SteamLibrary/steamapps/common/Alien Swarm/sdk_src` | Older L4D-lineage SDK public/game interfaces, prediction, split-screen, movement | Its `engine` directory contains only `iblackbox.h`; no full engine implementation or server NextBot tree was found. |
| `F:/cstrike15_src` | Later L4D-lineage engine implementations, networking, loaders, NextBot | Its README describes leaked source. Treat as conceptual reference; local possession is not a redistribution license. CS:GO gameplay and protocol are not L4D2's. |
| `F:/source-engine-tools` | Asset inspection and conversion utilities | Validate actual tool versions, supported formats, and target compiler selection before conversion. |
| `E:/SteamLibrary/steamapps/common/Source SDK Base 2013 Multiplayer` | Installed target-runtime candidate | `bin/x64/engine.dll` is AMD64; x64 engine/tool files are present. Successful loading of this project's game modules remains untested. |

### Original binary baseline observed

The following files are under `I:/SteamLibrary/steamapps/common/Left 4 Dead 2/left4dead2/bin/`. They are **PE32/x86, machine 0x014C**, not x64:

| File | SHA-256 |
|---|---|
| `client.dll` | `c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f` |
| `server.dll` | `a93ec916a8f26fbea85688c321d2ba8dd346d435fab09a0b926dd950c2e1ea12` |
| `matchmaking.dll` | `b405c7a2b786775e1940eccebcd6756335ceb20b583b43a05df7a457f9fe0e61` |
| `matchmaking_ds.dll` | `0e8586798f54d290143394469812d5af44fd8805cbd3119dcfd7d8f01f992e55` |

`left4dead2/steam.inf` reports patch `2.2.4.3`, network version `2.1.0.0`, and app ID `550`. Static strings anchor `VEngineServer022` / `ServerGameDLL005` and `VEngineClient013` / `VClient016`. The installed launcher and engine are also x86. The x64 server DLL under `addons/metamod/bin/win64/` is a third-party loader, not a Valve x64 game server.

Client/server CodeView records identify PDBs, but matching PDB files were not found. Plan for stripped-binary analysis; do not rely on a nearby PDB merely because its filename matches. The installed VScript module is x86 and contains `Squirrel 3.0.4 stable` and `VScriptManager010`; sharing the manager token with the SDK does not establish binding or VM compatibility.

Original VPK indexes are version 1. The additional bounded header probes below establish actual BSP, model, texture and nav samples; reference-branch constants alone are not treated as measurements of original assets.

There is no requirement to locate an x64 retail L4D2 engine for this plan: the original x86 runtime supplies baseline behavior, and the installed x64 SDK runtime is the port host. Neither original DLL can be loaded directly into that x64 process.

### Asset/header and target-loader probes

These checks read original bytes without extracting files to disk or changing them. They do **not** establish successful gameplay loading.

| Input | Observed result | Consequence |
|---|---|---|
| `maps/c1m1_hotel.bsp` | VBSP 21; lump entries use `version, fileofs, filelen, fourCC`; static-prop game lump `sprp` version 9, detail props `dprp` version 4 | A version-only bypass still leaves the older reader interpreting different fields and potentially different lump structures. |
| Base-VPK `models/c1_chargerexit/car_camjoint.mdl`, `car_escapesparks.mdl`, `common_male_carexit.mdl` | All three sampled headers are `IDST`, version 49 | Establish the real v49 reader/consumer differences; changing 49 to 48 is not proof that the data is usable. |
| Base-VPK `materials/ads/burger.vtf`, `film.vtf`, `subwayhorizontal_ref.vtf` | All three sampled textures are VTF 7.4, header size 104 | Test these unchanged first. Do not infer that all L4D2 textures need downgrading; this is a small sample, not coverage of every texture. |
| `maps/c1m1_hotel.nav` | Magic `0xFEEDFACE`, main version 16, subversion 14, stored BSP size 32,807,960 | Main version agreement is insufficient; recover L4D2 custom metadata and preserve the relationship to the original BSP. |
| Installed SDK Base `bin/x64/engine.dll` | Decompiled map-header/validation paths accept only versions 19–20 | A raw BSP 21 reaches a rejection in those inspected paths; runtime support must address readers and semantics, not just mounting. |

The inspected engine SHA-256 is `adba182f610ae3ccac3e42430e23c3eefc3f5f3b6b36d1b7b09225ec6016c5a6`; the map SHA-256 is `a3a9053692a1aa45787dfd25de9bce8144c7dde76681994402070710d1337172`. Target-engine preferred image base: `0x180000000`. Version checks were observed at functions with RVAs `0x0FEBF0`, `0x104C30`, `0x0FA130`, `0x163A60` and `0x163D60`; roles are inferred from their map-load/validation diagnostic strings. These are evidence anchors for this build, **not approved hook addresses or a complete inventory of map readers**.

Concrete layout check: the original entity-lump words are `(0, 1607896, 791714, 0)`. Reading the original layout reaches entity text at offset 1,607,896; reading the older `fileofs, filelen, version` layout instead selects offset zero. Both naive offset/length bounds checks happened to pass, demonstrating why structural and semantic checks are required.

### Build environment observed

- `vswhere` reports Visual Studio Community 2026 and 2017. The installed `14.44.35207` x64 compiler runs and reports MSVC `19.44.35229`; MSBuild reports `18.10.1.42706`.
- Python 3.14 is installed and selectable through `py`; `python` on PATH currently resolves to 3.12.12. The README requires Python 3.13 or later. Select the intended interpreter explicitly in the build environment.
- Installed Windows SDK include directories are `10.0.26100.0` and `10.0.10240.0`. The README names 22621 or 19041 and VS2022/v143. The newer IDE/SDK combination needs an actual baseline build; compiler presence is not proof that VPC and every dependency work with it.
- No language server is configured. The IDA integration responds and can maintain analysis databases; a successful full L4D2 decompilation has not been established.
- Targeted decompiler smoke succeeded on the exact hash-identified original DLLs: IDA produced pseudocode for the `CreateInterface` forwarding entry in client RVA `0x003D92D0` and server RVA `0x00501C80` (both preferred image base `0x10000000`). This verifies analysis/decompiler access, not recovered gameplay, correct function types, or whole-binary decompilation. No raw pseudocode was added to the repository.
- Executed `src/devtools/bin/vpc.exe /h` from `src`: it reports build `Mar 8 2025`, **Target Platform: win64**, and **Generating for Visual Studio 2022**, then prints help. This established the local default without generating projects or building the SDK.
- Actual AMD64 hosts are the installed SDK Base app-root `hl2_win64.exe` and `srcds_win64.exe`, alongside `bin/x64/engine.dll` and `dedicated.dll`. Do not use the app-root x86 `hl2.exe`. The installed `bin/x64/GameConfig.txt` still points its game-launch entries at that x86 executable, and checked-in VPC debugger templates do not establish an x64 startup binding. Configure and verify the launcher explicitly in M0.

### Workspace integration anchors

- `src/createallprojects.bat` invokes `devtools\bin\vpc.exe /hl2mp /tf /define:SOURCESDK +everything /mksln everything.sln`. It does not define an L4D2 target or explicitly select Win64; the executable's local help probe selected win64, but still inspect generated project configurations before building.
- `src/vpc_scripts/projects.vgc:16-19,51-54` defines the HL2MP/TF client/server targets. `src/game/client/client_base.vpc` and `src/game/server/server_base.vpc` provide the shared output/dependency patterns; TF wrappers choose `mod_tf` and add `TF_CLIENT_DLL` / `TF_DLL`. Do not inherit those gameplay defines for an L4D2 target.
- `src/vpc_scripts/platform_dirs.vpc:1-5` maps WIN64 to `x64`; `source_dll_win32_base.vpc:36-52` establishes Win64 defines and `/MACHINE:X64`. Client/server base projects select `steam_api64` under WIN64. This is build-system x64 support, not a completed x64 L4D2 project.
- The inspected workspace `src/engine` contains only `audio/public/sound.h`, not a complete engine implementation. `E:/SourceEngine/engine/engine.vpc` belongs to the separate historical reference, not this build.

### Confirmed branch differences

| Contract | Current SDK / older TF2 reference | L4D-lineage references | Porting consequence |
|---|---|---|---|
| Server DLL interface | SDK `ServerGameDLL012`; older TF2 `ServerGameDLL010` | ASW and CS:GO headers advertise `ServerGameDLL005` | Recover the original contract for analysis, but export the target contract in replacement modules. |
| Client DLL interface | SDK and older TF2 both advertise `VClient017`, but the SDK appends chat/voice virtual methods | ASW `VClient016`; CS:GO `VClient018` | Version strings alone are not sufficient ABI evidence. |
| Commands / transport | Follow target `CUserCmd`, delta codec, and engine message envelopes | CS:GO includes `aimdirection` and protobuf messages; ASW has different fields and numeric messages | Translate gameplay semantics, not raw C++ object layouts or another branch's packets. |
| Format constants | SDK/older TF2 headers: BSP 20, studio model 48, VTF 7.4 | Original probes: BSP 21, sampled models 49, sampled textures 7.4 | Actual target map-reader checks reject 21 and original lump layout differs. Prefer native mounting and proven runtime reader support before an offline rebuild; assess each format independently. |
| Scripting | SDK exposes `VScriptManager010`, game-side bindings, and an installed `bin/x64/vscript.dll` | Original VM identifies Squirrel 3.0.4; L4D2 bindings and scheduling need recovery | Record the target VM version, script encoding and callback behavior. A matching manager token or working VM alone is not script compatibility. |
| Split-screen | Older TF2 `CBaseClient::IsSplitScreenUser` defaults to false; SDK fragments do not establish end-to-end support | ASW/CS:GO have per-slot player/prediction state and connection machinery | Probe early; do not defer this potential engine blocker until UI work. |

Evidence: workspace `src/public/eiface.h:463-468,647-649`, `src/public/cdll_int.h:795-800`, `src/public/vscript/ivscript.h:127-155`, `src/game/server/gameinterface.cpp:625-630`; older TF2 `public/eiface.h:455-458`, `engine/baseclient.h:123-129`; ASW/CS:GO `public/cdll_int.h`, `public/eiface.h`, `game/shared/usercmd.*`, `public/bspfile.h`, and `public/studio.h`.

Useful engine reference paths under `E:/SourceEngine`:

- DLL loading: `engine/sys_dll.cpp:1124-1168`, `engine/cdll_engine_int.cpp:1714-1740`.
- Command dispatch and replication: `engine/sv_client.cpp:174-218,924-968`, `engine/sv_main.cpp:765-786,936-941`, `engine/cl_ents_parse.cpp:570`.
- Model/BSP loading: `engine/ModelInfo.cpp`, `engine/cmodel_bsp.cpp`, `studiorender/`.
- Textures, particles, and sound: `vtf/vtf.cpp:999-1025`, `particles/particles.cpp`, `engine/EngineSoundClient.cpp`, `engine/EngineSoundServer.cpp`.

Reuse the current SDK's NextBot/nav framework plumbing, not unverified newer-branch behavior. Compare locomotion, path selection/failure, vision, body behavior, update budgets and RNG order against the recovered original for each actor. Put differences in L4D2-owned subclasses/implementations where supported; if a non-overridable shared seam requires change, isolate the L4D2 path and preserve other games' behavior. Do not silently import a later NextBot fix or globally change TF2's AI. CS:GO's `NextBotGroundLocomotion.cpp` documents L4D ancestry, but ancestry is not parity evidence.

There are partial L4D-era hooks in the current framework, not a hidden complete game: `src/game/server/NextBot/NextBot.cpp:15-16,428-436`, `NextBotManager.cpp:10-13`, and `nav_mesh_factory.cpp:15-16,32-33` reference TERROR/ZombieBot/TerrorNav paths, while the corresponding complete target and referenced definitions were not found. Do not simply enable `TERROR` and assume L4D2 will build.

## 3. Reconstruction method

### 3.1 Freeze a reproducible baseline

Record SHA-256, size, PE machine, image base, timestamp/version metadata, and exports for the original client/server. Also record loaded engine/support-module identities, Steam build metadata, DLC/VPK inventory and search-path precedence. A DLL hash identifies the binary; a PE timestamp or version label alone does not.

The installation contains third-party addon/Metamod content. Use an authorized staging copy of the original installation for the clean baseline: retain stock engine/game/DLC data and shipped configuration, exclude identified third-party addon binaries/manifests and user startup overrides, and launch/configure only that staged game tree. Do not delete/reconfigure the user's installation or bypass normal Steam ownership/authentication. Verify effective search paths, loaded module paths and plugin listings prove no third-party runtime was imported; if launch redirects to the original install, the baseline gate fails. Original `gameinfo.txt:42-50` orders content as `update`, DLC3, DLC2, DLC1, base game, then `hl2`.

Installed SourceMod gamedata, including `addons/sourcemod/gamedata/left4dhooks.l4d2.txt`, can seed names/signature/offset hypotheses without running the plugins. Label this provenance as third-party gamedata; validate signature uniqueness, target function and offsets against the pinned module hashes before promoting a hypothesis to observed evidence. Plugin presence is not evidence that it was active during gameplay.

Keep raw binaries, IDA databases, extracted content and decompiled listings outside tracked source. In the evidence section of the single ledger, record module hash + RVA, proposed symbol, confidence, evidence source, unresolved alternatives and the linked verification scenario. Track reconstructed behavior against these entries rather than counting pseudocode lines or maintaining a separate function-tracking database.

Capture original offline/local behavior before implementing it: console/cvar values, entity classes and keyvalues, game events, script API calls, network-visible state, frame/tick timing, representative videos/screenshots, and scenario outcomes. Use normal development/offline facilities; do not bypass Steam authentication or anti-cheat.

### 3.2 Recover contracts before algorithms

Start with `CreateInterface`, exported lifecycle entry points, interface registrations, strings, RTTI where present, vtable references, entity factories, send/receive tables, datamaps, prediction descriptors, ConVars/commands, usermessages, game events, and script registration tables. These constrain many functions more reliably than unaided pseudocode.

Build separate inventories for:

1. Engine-required interfaces and their method ordering.
2. Entity/class names, keyvalues, inputs/outputs, save fields, and class hierarchies.
3. Replicated fields, quantization/ranges, send proxies, recipient filters, events, and prediction ownership.
4. User-command fields and explicit serialization; do not treat `sizeof(CUserCmd)` as a wire definition.
5. Script classes/globals/callbacks, mission/mode/weapon data, sound and response rules.
6. Model sequences, animation events, activities, attachment names, collision/hitbox dependencies, and navigation extensions.

Start the map-facing entity/keyvalue/I/O inventory from the installed `bin/left4dead2.fgd` and `bin/base.fgd`, then cross-check against `server.dll` factories, datamaps and keyvalue handlers. FGD declarations describe the Hammer surface and may omit internal or undocumented contracts.

Use reference-tree comparison to propose names and structure, then validate against the installed L4D2 binary and observed behavior. ASW's marine control and CS:GO's competitive rules are not substitutes for survivor or infected logic.

### 3.3 Reconstruct dependency-sized behavior

Decompile focused call paths, assign types, and examine assembly at ambiguous conditions, signedness, floating-point operations, virtual calls and ownership transitions. Record each conclusion as **observed**, **supported inference**, or **unresolved**. Keep evidence-supported original names and structure; never present an invented name or a reference-branch implementation as recovered L4D2 source.

Recover shared definitions first, then the client/server portions of each behavior together. For example, a firearm includes command consumption, predicted cooldown/ammo state, authoritative trace/damage, replication/correction, animation/audio, and HUD feedback—not merely a server-side firing function.

A complete behavioral implementation need not reproduce dead compiler artifacts, but apparently redundant operations must remain until their effect on RNG, timing, side effects and failure paths is understood. Every reachable gameplay path and externally observable contract, including original bugs, needs an implementation or an explicit unresolved fidelity item. Fake success returns, hard-coded scenario outcomes and placeholder AI do not satisfy a milestone.

### 3.4 Separate legacy evidence from native x64 types

Document original object layouts for reverse engineering without imposing their pointer sizes on native classes. Audit pointer-to-integer casts, handles, packed structures, vtable assumptions, varargs, callback signatures, inline assembly, serialization widths, format strings, custom allocation, and cross-module ownership.

Keep on-disk/network fixed-width fields distinct from `size_t` and pointers. Do not mechanically double every field or replace every `int` with a 64-bit integer. Both replacement modules must use the same target headers, send/receive schemas, shared code, build defines, and allocator conventions.

### 3.5 Valve code style and native SDK facilities

- Follow neighboring Valve files: tabs/indentation, brace placement, spacing, include order, prefixes, member naming and declaration layout. Use recognizable `C...` classes, `m_...` members and existing helper naming where the surrounding subsystem does; do not run a global restyle or impose modern C++ conventions.
- Use suitable Valve containers instead of introducing STL equivalents: `CUtlVector`, `CUtlMap`, `CUtlDict`, `CUtlLinkedList`, `CUtlString`, `CUtlBuffer` and related existing types. Keep fixed-size arrays or existing fixed buffers where they match the original. Do not replace a stable external API's required type merely to remove an existing STL use, but introduce no parallel STL container layer for this port.
- Container choice is behavioral: retain insertion/traversal order, case comparison, duplicate handling, index/handle stability and removal semantics. `CUtlVector::FastRemove` swaps the last element into the removed slot; use it only where that reordering matches the original. Do not sort collections, change dictionary comparison, or exchange indexed storage for a tree/hash just because lookup is convenient.
- Reuse `DECLARE_CLASS`, entity/datadesc, send/receive table, prediction and script-binding patterns already used by the SDK. Keep authoritative and predicted fields defined consistently in shared code. Use `CHandle`/`EHANDLE` for entity lifetime tracking where the original pattern calls for it, not a new smart-pointer ownership scheme.
- Keep Valve strings/math/serialization, `KeyValues`, existing console diagnostics, interfaces and allocation conventions. Respect the local `cbase.h`/PCH and `tier0/memdbgon.h` include placement where present. Do not add STL streams, exceptions, RTTI-based dispatch or another utility/logging layer to solve an already-supported operation.
- Honor original ownership, reference counts, precache/unload behavior and allocator boundaries. A `CUtlVector<T *>` does not establish ownership of `T`; follow the relevant delete/purge conventions and never free engine-owned memory through a different allocator.
- Avoid new per-tick allocations, copies, string construction and scans. Reserve/reuse storage where the original lifecycle supports it; do not add caches, background workers, batching or reordered work that change when the game observes state.
- Implement first in the owning class/subsystem. Extract shared code only after both client/server or another real caller need exactly that behavior. No catch-all `compat` subsystem: one narrow loader adapter is justified only by a verified reader mismatch.

Local examples to follow: `src/game/shared/Sprite.*` and `basecombatweapon_shared.*` for network/prediction/datadesc integration, `src/game/server/NextBot/NextBotManager.cpp` for list mutation and scheduling, and `src/public/tier1/utl*.h` for exact container semantics. `CUtlMap` uses comparator order and ordinary insertion need not enforce unique keys; `CUtlDict` defaults to case-insensitive comparison. `CUtlString` is not a blanket runtime-string replacement—the local header warns about runtime suitability. Choose the established type and behavior for each use, not simply any type beginning with `CUtl`.

### 3.6 Repeatable unit of reconstruction

For each behavior, in order:

1. Specify one reproducible original action and its expected transitions, including cancellation/failure paths and known bugs.
2. Identify its entity factory, owning class, inputs, fields, callers, callbacks and client/server counterparts from metadata and xrefs.
3. Recover the smallest full call path needed for that action; annotate layout/type hypotheses and resolve ambiguities in assembly or an original run.
4. Implement that path in the corresponding Valve-style class using the existing framework. Add only the fields, bindings and data needed by this behavior, not speculative future interfaces.
5. Run it through real input, authoritative simulation, replication/prediction and presentation. Compare against the original at the same conditions.
6. Add focused regression coverage for uncertain boundaries or a reproduced discrepancy; preserve original bugs as expected results. Extend the evidence note and proceed to the next behavior only when this one has no unexplained difference.

## 4. Implementation organization and engine boundary

### 4.1 Code ownership

Proposed additions, created only after the baseline build succeeds:

- `src/game/shared/l4d2/`: shared gameplay state/enums, player movement, weapons, predicted state, shared rules/data and animation contracts.
- `src/game/server/l4d2/`: survivor/infected behavior, authoritative combat, bots, Director, map entities, mission/mode control and server script bindings.
- `src/game/client/l4d2/`: presentation, HUD/UI, camera, client effects, animation, prediction integration and client script bindings.
- L4D2-specific VPC targets following existing client/server project conventions; a separate `game/mod_l4d2/` content/configuration root.

These are proposed locations, not files already implemented. Do not fork the entire TF2 client/server or import another branch wholesale. Retain existing games; extend shared SDK code only when a demonstrated contract requires it. Choose the smallest existing buildable multiplayer target as the integration starting point, then replace its game-specific rules rather than retaining TF2/HL2DM behavior as hidden fallback.

Early capability probes must exercise the **installed target engine**, not just compile headers:

- Load/unload the x64 client/server and exercise map startup, shutdown, and reconnect.
- Replicate an entity with target send/receive tables; process commands and prediction across a real local connection.
- Create the VM supplied by SDK Base `bin/x64/vscript.dll`; record its Squirrel version and execute a representative L4D2 binding/callback lifecycle. Inventory `.nut`/`.nuc` members and inspect their actual encoding before choosing text extraction, supported decoding, or behavior reconstruction; filename extension alone does not establish bytecode or encryption.
- Load representative original BSP, MDL, VTF/VMT, particles, and sounds; capture exact loader errors and rendered/physical behavior.
- Establish split-screen local slots, input, prediction, rendering, and server connection capability.
- Exercise a client main-menu override through `IGameUI::SetMainMenuOverride` and target Steam lobby/server-discovery APIs via `steam_api64`, without claiming access to original L4D2 services. The TF client demonstrates the override in `src/game/client/tf/clientmode_tf.cpp` and `tf_hud_mainmenuoverride.cpp`; original `matchmaking.dll` / `matchmaking_ds.dll` are analysis dependencies, not modules to load into the x64 target.
- Build and load one x64 custom shader module through the existing stdshaders VPC/buildshaders pipeline, including its compiled shader bytecode. This proves an extension path for required L4D2 material effects; a successful texture conversion cannot substitute for it.
- Exercise game-side navigation extensions, custom particle-operator registration, and multiple survivor flashlights. Distinguish game-DLL limits from fixed engine readers/render capabilities before declaring an engine blocker.

### 4.2 Can hooks avoid offline asset conversion?

**Yes in principle, for bounded reader differences; not by forcing acceptance of arbitrary newer assets.** Mounting and parsing are separate operations. A hook can route original bytes to a correct parser or normalize a known layout in memory, leaving every installed asset unchanged. That avoids decompile/recompile and its fidelity losses. It is still runtime format adaptation, not proof that the old engine natively understands the original game.

Decision order:

1. Mount and load supported original content unchanged.
2. Implement missing game-owned behavior through existing Source APIs: nav extensions, material proxies, particle operators, animation/game logic and script bindings.
3. For a proven engine-owned reader gap, evaluate the smallest build-specific loader adaptation, including hooks only where no supported extension point exists.
4. Use Crowbar/VTFEdit/map rebuilding only for a demonstrated remaining need. If the fallback changes behavior or fidelity, obtain approval rather than silently accepting it.

An engine hook cannot create a missing shader, decode an unknown animation format, repair collision semantics or supply L4D2 entities by merely skipping an error. If correct support requires substantial engine replacement, stop and expose that dependency rather than accumulating a general-purpose patching framework.

### 4.3 Mount original content without importing the original runtime

1. Keep the mod's `gameinfo.txt`, native `GAMEBIN`, executable/module paths, configuration and writable root under target/mod control. Do not use L4D2's entire `gameinfo.txt` or add its root indiscriminately: ordinary `Game` entries can derive `GameBin` paths, and loose addon/config discovery is separate from module search.
2. Test the installed x64 filesystem's public `IFileSystem::AddSearchPath` support for an explicit original VPK directory archive. The historical filesystem routes `.vpk` paths through packed-store support, but its old WIN64 build condition is not evidence of what the installed modern binary supports. `AddPackFile` in that reference is a ZIP path, not a substitute VPK API.
3. Mount inventoried stock archives under the path ID requested by actual loaders, normally `GAME`. Preserve original update/DLC3/DLC2/DLC1/base/HL2 precedence and within-layer loose-versus-archive order. When inserting at the head, add lower-priority layers first. Verify a known overridden file resolves to the same original payload before diagnosing formats.
4. Inventory archive members too: `AddSearchPath` has no per-member denylist, so an archive is not automatically a config/plugin sandbox. Keep the original loose `bin`, `addons` and user `cfg` trees out of the mount set. A custom path ID does not help unless every consumer uses it.
5. Handle loose stock maps/nav/sounds/scenes with a content-only search layout proven not to expose those unwanted trees. If existing path controls cannot express it safely, use a small staging directory of unchanged loose files with original relative paths; that is copying, not asset conversion. Avoid junctions that could redirect nav generation or other writes into the retail installation. Do not build a virtual filesystem just to avoid a small staging copy.
6. Keep generated nav, logs, caches, saves and any fallback outputs in the mod write path; verify effective writes do not land in mounted originals. Preserve the engine's own active-BSP embedded-pack mount/unmount and precedence, rather than permanently mounting the map pak yourself.
7. Verify one original texture/material, one complete model family and then the map/nav pair. Record resolved archive/member or loose path and payload identity. Do not load original x86 DLLs, switch the mod to app ID 550 merely to force mounting, or substitute an unverified `MountSteamContent` call for controlled resolution.

Source anchors: `src/public/filesystem.h:253-262,549-602`; original `left4dead2/gameinfo.txt:20-22,44-49`; `E:/SourceEngine/filesystem/basefilesystem.cpp:792-848,1393-1411`; `engine/sv_main.cpp:2577-2579` and `engine/modelloader.cpp:3586-3588,5523-5524`.

### 4.4 First runtime compatibility experiment: one original BSP

This is a proposed implementation experiment, **not a hook already installed or proven**.

1. Use the pinned engine and `c1m1_hotel.bsp`/`.nav` identities from section 2. First separate known version rejection from content-path failures and missing game entities. The actual binary comparisons and header probe already prove that a global “accept 21” change is insufficient.
2. Trace the map-validation, disk/memory header-read, collision, world-model, game-lump and checksum paths actually used by client/listen/dedicated startup and changelevel. The five observed version-check functions are starting evidence, not the complete hook list or permission to patch all of them.
3. Build a small read-only format probe that interprets the original header explicitly, checks offset/length arithmetic, identifies every used lump/version and compression flag, and parses the game-lump directory. The initial header probe is already done; extend it only as the next consumer requires. Do not cast file bytes to a native x64 C++ structure.
4. Identify the narrowest correct representation boundary. A same-size header normalization in a private buffer may preserve all payload offsets, but cannot by itself repair differing payload records. Prefer a reader-local implementation over intercepting every filesystem read. If no common boundary covers the consumers, list the necessary individual paths and their ownership before writing hooks.
5. Implement the original lump-directory order, compressed/uncompressed-size interpretation and bounds, plus required payload readers. Preserve absolute game-lump offsets, embedded pak access and file/checksum semantics. Do not move payloads without updating every dependent offset, relabel unknown lump IDs, truncate fields or discard unsupported data to make a map load.
6. Inspect actual target handling of `sprp` 9. The older TF2 reference handles 4/5/6/7/10 but has no 9 dispatch; that is a concrete research lead, not a verified property of the newer target reader. If support is missing, implement the correct static-prop fields and rendering/collision integration. `dprp` 4 has a client-game reader in the reference, but still depends on the adapted game-lump API and correct data.
7. Establish consistent source identity and checksum behavior for both peers and all readers; avoid one path seeing normalized bytes while another hashes or seeks an incompatible representation. Keep the original BSP size/identity relationship used by nav validation unless the original contract requires a documented mapping. Do not fake checksum success.
8. Exercise dedicated loading/collision first, then client rendering, static props, traces, visibility/lighting, embedded materials and nav relationships. Repeat map unload/reload/changelevel and a native target-format map. Distinguish a loader failure from an as-yet-unimplemented L4D2 map entity.

**Pass:** the selected original map is consumed correctly without modifying/rebuilding its files, and native-target content and lifecycle still work. Expand to additional campaigns/compressed lumps only after that case passes. Header acceptance or a recognizable screenshot alone is not a pass.

### 4.5 Hook lifecycle and x64 limits

- The reference startup calls server `CServerGameDLL::DLLInit` before normal map validation on listen/dedicated paths; `GameInit` and `LevelInit` are too late for the early validation. Treat `DLLInit` as a candidate mount/hook entry, not proof for every target client-only, connection, tool or map-browser path. Verify actual target order before choosing the final entry.
- One process owner installs a necessary detour once, after dependencies exist and before the first affected read. Do not duplicate installation in both game DLLs, patch from `DllMain` under the loader lock, or modify code while another thread can execute the affected instructions. If an earlier bootstrap is genuinely required, justify that additional component explicitly; do not pre-build an injector/launcher framework.
- Bind support to the exact module hashes and verified interfaces. Resolve ASLR correctly; validate instructions, calling convention, registers/stack, relocation and trampoline behavior. Choose a small proven x64 detour facility only when M1 demonstrates it is necessary; treat any imported facility as a new third-party dependency with reviewed license/provenance and normal VPC integration. Do not hand-roll relocation or add a generic plugin/hook ecosystem. If no hook is needed, add no detour dependency.
- Preserve engine allocator/ownership rules and async-read lifetimes. Keep any normalized buffer alive as long as its consumers require; balance cache/map unload before removal. No pointers into a temporary `CUtlBuffer`, per-frame translation, unbounded duplicate model caches or concurrent hook removal.
- Keep original bounds/identity/checksum checks meaningful. Unknown engine builds, ambiguous targets and unsupported source features must stop with a precise diagnosis, not guessed offsets, broad signature matches, auto-retries or a silent fallback asset. A new Steam build requires revalidation; do not make the installation immutable by interfering with Steam updates.
- Prove client and dedicated behavior, native-format pass-through, unload/reload and rejection before patching an unsupported build. For a controlled negative probe, run the same preflight validator against a deliberately hash-mismatched private module copy; require a precise rejection before any load/detour attempt. Do not execute that altered copy or alter installed binaries. This tests the guard, not runtime compatibility with an arbitrary build. Dedicated execution must not depend on client UI/render initialization.
- Prototype only in isolated local development sessions. Do not bypass VAC, Steam authentication, server trust or integrity enforcement; do not deploy unvalidated hooks into public play. Original binaries on disk remain untouched.

Lifecycle references: `src/game/server/gameinterface.cpp:569-606,855-857,957-959`; `src/game/client/cdll_client_int.cpp:873-934`; `E:/SourceEngine/engine/host.cpp:4176-4204,4455-4462,4664-4691`; `engine/host_state.cpp:318-326`. Reader references: `engine/modelloader.cpp:424-470,539-572,2791-2838,5339-5421`, `engine/checksum_engine.cpp:165-170,251-256`, `engine/staticpropmgr.cpp:1319-1348`, and `F:/cstrike15_src/engine/staticpropmgr.cpp:1538-1604`. Historical source explains likely structure; the pinned target binary remains authoritative for hooks.

### 4.6 Other game/engine boundaries

Lobby/campaign-selection behavior will be reconstructed in the replacement client and paired server rules using target-supported UI/Steam services. No target counterpart to the original matchmaking modules was found in the surveyed SDK. Successful menu rendering alone does not establish lobby lifecycle, permissions, connection handoff or dedicated-server coordination.

Game-owned seams: `src/game/server/nav_file.cpp` provides custom-version/data hooks for server-side navigation; `src/public/particles/particles.h:881-914` exposes particle-operator registration; `src/game/client/clientshadowmgr.cpp:1294` contains a game-build shadowed-flashlight cap of one. Implement and validate these game-side behaviors rather than treating them as immutable engine limits. PCF/DMX reader compatibility and actual renderer resource limits remain separate probes.

## 5. Asset compatibility and conversion

### Installed tools and exercised checks

| Tool | Observed version / location | Planning-pass check |
|---|---|---|
| Crowbar++ | `F:/source-engine-tools/Crowbar++.exe`, `0.74.0.0` | File version inspected; GUI, decompilation and automation interface not exercised. |
| VTFEdit++ | `F:/source-engine-tools/VTFEdit++.exe`, `4.0.1` | File version inspected; no GUI launch or conversion. |
| VTFCmd++ | `F:/source-engine-tools/VTFCmd.exe`, `2.0.0` | Executed without input; printed usage and exited 2 for missing `-file`/`-folder`. Supports explicit output version/format/mipmap options. This was not a conversion test. |
| Original VPK utility | `I:/SteamLibrary/steamapps/common/Left 4 Dead 2/bin/vpk.exe` | Executed `vpk l` against `left4dead2/pak01_dir.vpk`; listed content without extraction. |
| Target asset compilers | `E:/SteamLibrary/steamapps/common/Source SDK Base 2013 Multiplayer/bin/x64/` | `studiomdl.exe`, `vbsp.exe`, `vvis.exe`, `vrad.exe`, `vtex.exe`, `vpk.exe` found; output compatibility not tested. Use these target-branch tools rather than defaulting Crowbar to the original L4D2 compilers. |
| SDK shader pipeline | `src/materialsystem/stdshaders/buildshaders.bat`, `src/devtools/bin/ShaderCompile2.exe` | Existing script inspected; shader compilation not run. |

VTFCmd++ documents `-compress` as requiring VTF 7.6. Do not enable that newer compression for a target whose source headers declare VTF 7.4 without proving actual loader support.

The base VPK index and chunks `pak01_000.vpk` through `pak01_040.vpk` are present. Indexed starting samples (listed via `vpk l`, not extracted or validated) include the `models/brokenglass_piece` MDL/VVD/DX90-VTX set, `models/c1_chargerexit/car_escapesparks.ani`, and `models/c3_plantation/gate_expl_1.phy`; resolve each sample's complete companion/dependency set before conversion. Loose originals include `maps/c1m1_hotel.bsp` and `sound/weapons/rifle/gunother/rifle_clip_in_1.wav`. Do not mistake loose custom-logo/addon assets for representative shipped content.

The captured base-VPK extension summary counted 182 `.nuc` entries. Exact script member paths and payload encodings were not inspected; the summary does not establish `.nut` absence. Resolve those inputs before treating shipped scripts as readable source or deciding a decoding path.

### Policy

Default to original bytes through native mounting or a validated runtime reader. Keep the installation unchanged and preserve paths, search order and dependent data. Runtime normalization is acceptable as an implementation technique only when it preserves the observed original behavior; it is not a license to simplify unsupported content.

For any required offline fallback, write to a separate derived-content directory and mount it deliberately ahead of originals. Record input archive/member and hash, the demonstrated incompatibility, tool/version/options, target revision, output hashes and measured fidelity differences. Tool availability does not justify converting a compatible asset. Offline compiler bitness is secondary to the output format; conversion that changes behavior requires explicit approval.

### Per-format workflow

| Asset | Owner / smallest useful boundary | Preferred path; fallback only if needed | Required validation |
|---|---|---|---|
| VPK archives / loose content | Target filesystem and controlled search paths | Mount inventoried stock VPKs through the supported filesystem; stage unchanged loose files only when needed for safe path isolation. No generic filesystem wrapper. | Effective update/DLC/loose/archive/embedded-pak precedence, original payload identity, no imported x86 modules/plugins and no writes into originals. |
| MDL/VVD/VTX/PHY/ANI | Model cache, game/engine animation consumers, physics | Try complete original families first. Inspect actual cache/parser behavior and add only demonstrated missing reader/animation support. Use Crowbar plus target `studiomdl` only for a justified fallback. | Companion checksums, offsets, included models, bones/activities, animation decoding/IK/events, hitboxes, attachments, skins/LODs, ragdoll mass/joints and collision. Load success alone is insufficient. |
| VTF | Texture reader/resource decoder | Sampled 7.4 textures are direct-load candidates. For other versions, inspect actual header/resources/image encoding; normalize only demonstrably equivalent layout or implement the missing reader. VTFEdit/VTFCmd/target `vtex` are selective fallbacks. | Alpha, mips, normal channels, frames/cubemaps/depth, color treatment, compression and appearance. No global version downgrade or new VTF 7.6 compression. |
| VMT / shaders | Game material proxy factory; supported shader module; renderer | Preserve original VMT behavior. Implement missing proxies and shader behavior at their separate supported seams; a proxy cannot supply a missing shader/render pass. | Skin/infected/gore/wetness, translucency, lighting/fog/flashlights and original parameter/proxy semantics. Do not replace a shader with a “close enough” stock one without approval. |
| BSP | Engine header/lump readers, static props, collision and checksum consumers | Follow the bounded original-map experiment in section 4. Native reader support or runtime adaptation comes before a L4D2-aware offline rebuild with target `vbsp`/`vvis`/`vrad`. | Layout/compression, prop versions, absolute offsets, embedded pak, collision, vis/lighting, materials, entity I/O and nav identity. Never merely relabel BSP 21 or discard lumps. |
| NAV | Server DLL nav parser/custom-data hooks and AI | Read original nav and implement L4D2 subversion metadata/consumers in server code. No engine hook or nav regeneration merely because it is L4D2 data. | Area IDs/topology, flags/flow, traversal, spawn rules, path-selection/failure bugs and BSP identity. Main-version equality is not sufficient. |
| PCF | Particle reader/DMX plus game-registered operators | Register original operator semantics before loading data when the existing API can express them; separately investigate incompatible PCF/DMX structure. | Control points, attachments, emission, timing, render/state behavior, reload/lifetime and network triggers. A recognized operator name with changed behavior is not parity. |
| Sounds/scenes/scripts/response rules | Engine VM/audio/scene services and game-owned bindings/scheduling | Consume original data and implement its consumers; inspect actual script encoding and bindings before choosing a decode path. Convert only a proven unsupported format. | Codec/playback/spatial behavior, dialogue/music selection, callback ordering/errors, timing, reset and lifetime—including original quirks. |

Shader clock contract: native `UpdateGameTime` (`engine` `0x10116320`) publishes the rendered client clock through the material context to the shader API; it is tick-based client time plus interpolation, not a per-draw wall clock. The L4D2 client publishes `gpGlobals->curtime` at `FRAME_RENDER_START` in float render parameter `FLOAT_RENDERPARM_L4D2_CURRENT_TIME` (slot 0, independent of water vector 11 and int 12). While paused, only the published shader value is advanced to the native tick-end clamp (`curtime + interval_per_tick - 0.00001f`); gameplay globals remain unchanged. Owned generic tree sway, water animation and EnginePost time consume that value; preserve each shader's existing time scaling/wrapping. Base and additive flashlight draws must use identical tree-sway time/wind and retain native depth equality. Native VertexLitGeneric also retains its authored alpha test/reference in the flashlight pass; Phong has a different native alpha-state contract and is not changed with it. Publication occurs on every render start, whereas native publishes only with an active game view.

Model-specific caution: the older TF2 `datacache/mdlcache.cpp` calls a header converter that can stamp non-48 models as 48; therefore a version-49 MDL is not automatically rejected there. ASW's v49 uses fields/flags such as IK zero-frame data and `STUDIO_FRAMEANIM`, while CS:GO's v49 also has different bone limits. Neither the same version number nor a permissive header rewrite establishes feature compatibility. The loader lives behind `IMDLCache` in the separate datacache component; `SetCacheNotify` is a notification callback, not a public parser-replacement API. Trace actual consumers before deciding that an `engine.dll` hook is the solution.

Format/source anchors: `E:/SourceEngine/datacache/mdlcache.cpp:2067-2080,2585-2625,2820-2836`; `E:/SourceEngine/public/studio.h:3008-3070`; ASW `sdk_src/public/studio.h:760-815,3097-3205`; CS:GO `public/studio.h:955-1010,3582-3690`; E `vtf/vtf.cpp:199-234,912-1009`; `src/public/datacache/imdlcache.h:67-175`, `materialsystem/imaterialproxyfactory.h:14-23`, and `particles/particles.h:327-339,533-539,572-885`. These identify implementation candidates; actual original payload features and installed target acceptance must be measured per case.

Smoke corpus: a world material/texture, transparent material, normal-mapped material, static prop, survivor model, common infected, special infected, animated weapon/viewmodel, ragdoll, representative particle, sound/scene, original map segment with its nav data, and a mission script. Identify actual archive members before loading them unchanged or justifying any fallback conversion.

Crowbar decompilation may lose authoring information; recompiled visuals can be close while gameplay hitboxes or animations are wrong. Keep original/converted comparisons and do not overwrite the source as a repair strategy.

## 6. Full behavioral scope

The feature section of the evidence ledger records original evidence, target implementation location, dependencies, verification scenario, parity status and blockers. M1 capability results and eventual completion status update these same rows. The final scope includes:

- **Survivors:** movement/collision, ladders and traversal, health/temporary health, incap/ledge/death, rescue/revive/defibrillation, inventory, interaction, spectator transitions, survivor bots and player/bot takeover.
- **Combat:** firearms, melee, shove and fatigue, ammunition/reload, throwables, healing/buffs, damage/hitgroups/friendly fire, explosives/fire and other environmental hazards, mounted weapons and usable objects.
- **Infected:** common/uncommon behavior and population, all special infected attacks and pin/release transitions, Tank and Witch, player-controlled infected, spawning/ghost restrictions, death/ragdolls/gore and associated animation/audio.
- **Director and navigation:** flow/distance, intensity/pacing, population budgets, encounters and item placement, panic events, crescendo/gauntlet/finale stages, safe-room and chapter transitions, bot coordination and navigation extensions.
- **World and data contracts:** map entity classes, keyvalues and I/O; mission/mode/weapon scripts; VScript APIs/callbacks; sound/response/scene rules; original shipped campaign and DLC content dependencies.
- **Client:** view/camera, animation and attachments, prediction/reconciliation, HUD/crosshair/inventory/scoreboard, menus/lobby/voting, chat/voice integration, localized text, audio/music/dialogue, particles and screen effects, input/controller and split-screen behavior.
- **Modes and lifecycle:** campaign/co-op, single-player with bots, realism, versus and realism-versus, survival, scavenge, mutations, custom-map/add-on loading contracts, round/scoring/team changes, local/listen/dedicated hosting, joining/reconnecting, map transitions and supported persistence.

External binary plugins and original engine demos/saves require separate compatibility evidence; matching gameplay does not imply their binary formats work. Do not bypass unavailable online services or substitute invented service responses. Record any service-dependent limitations explicitly.

## 7. Detailed implementation sequence and exit gates

The fidelity and coding rules in sections 1 and 3 apply to every step. Keep one integration owner for shared fields, VPC targets and engine boundaries. After the first complete slice, independent combat, AI, presentation and content work can proceed together; paired client/server changes must land together.

### M0 — Pin the original and establish a working x64 baseline

**Depends on:** no reconstructed gameplay.

1. Record original module/content hashes and search order, target modules, toolchain and launch configuration. Create and verify the clean staged original installation described in section 3.1; retain proof of actual file/module/plugin resolution without changing the user's installation.
2. Record one small original scene: survivor movement, a stock rifle, a common infected, damage/death and a reload boundary. Capture inputs, relevant cvars, difficulty, mode and tick interval, plus an observed quirk if the scene exposes one. This is the first comparison case, not a new universal replay system.
3. Select installed v143 `14.44.35207`, Windows SDK `10.0.26100.0` and Python 3.14 for the first build attempt; record those exact selections. From `src`, run `createallprojects.bat` and inspect Win64 outputs. If a demonstrated SDK-specific failure requires the README's 22621 SDK, install/select it deliberately and record the change; do not preemptively retarget everything or infer that the newer SDK works without building.
4. Build the smallest existing multiplayer client/server pair with its existing game data. Fix only demonstrated build failures; do not create L4D2 targets, modernize the SDK or change gameplay yet.
5. Configure `hl2_win64.exe` and `srcds_win64.exe` from the installed SDK Base app root, an isolated writable game directory and the actual output DLLs. Confirm the process and loaded modules are AMD64, not the neighboring x86 launcher.
6. Start a stock-compatible map, connect a client to listen and dedicated hosts, disconnect/reconnect, change/reload the map and shut down. Record the first actual failure rather than replacing it with a dummy callback or suppressing diagnostics.

**Keep:** the baseline manifest, staged-original path/module/plugin proof, original scene capture, selected build configuration/log and actual loaded paths.

**Exit:** both host types run the existing game pair through that lifecycle. Compiler availability or generated projects alone do not close M0.

### M1 — Prove capabilities and original-content access separately

**Depends on:** M0. Maintain per-capability results: unprobed, working, failed, or blocked. An unfinished original-map reader must not block unrelated test-map gameplay.

**Probe code home:** use a disposable local copy/worktree of the now-proven sample target, with experiments confined to its game-specific files or temporary probes. Do not alter shared SDK behavior or create a permanent probe framework. At M2, move only validated production mounting/loader code into its real owning subsystem; remove probe entities, diagnostics, temporary project edits and the disposable copy once their evidence is retained.

1. Establish content-only mounting with the original update/DLC precedence and an isolated write path. Verify actual file resolution and prevent original DLLs, addon plugins or user configurations from entering the target process; follow the mount sequence in section 4.
2. Test the section 5 corpus unchanged. Separate a missing file/dependency from a format rejection, accepted-but-misparsed data, unsupported shader/operator and missing game behavior. Retain the exact module/diagnostic and first bad consumer.
3. Follow the narrow loader-investigation sequence in section 4 for each demonstrated mismatch. Prefer an existing game/module extension; where a hook is necessary, prove its startup timing, data/ABI contract and coverage before treating it as a supported capability.
4. Validate accepted assets beyond file-open success: model sequences/hitboxes/collision, texture/material appearance, BSP collision/visibility/props, nav semantics, particles, scenes and script callbacks. Prove both client and dedicated-server paths where applicable.
5. Exercise real commands, replication and prediction; VM creation/bindings/lifetime; split-screen slot/input/render/connection ownership; UI override and target-supported lobby/server handoff. Missing engine or service support stays an explicit capability blocker.
6. Compile and load the smallest required shader extension and exercise particle registration, nav custom data and survivor flashlight behavior. Distinguish mutable game-side limits from fixed renderer/reader limits.
7. Consider offline conversion only after native reading and bounded runtime support have been assessed. Record why it is needed, complete dependencies, measured losses and behavior differences. Do not accept a fidelity-changing fallback without approval.

**Keep:** capability rows in the existing ledger, content resolution, native/adapted/fallback results and exact blockers. No separate capability database or permanent probe target.

**Exit gates:** (a) M2: x64 module load/unload, a real probe entity's replication, and command/prediction execution across a local connection on M0's stock-compatible map; (b) M3: the selected original player/weapon/infected model, texture, animation, sound and effect families consumed correctly; (c) M6: original campaign BSP/nav and related data work through the selected content path. Prove each separately. Header acceptance is not the campaign gate, and an open asset-family gate blocks M3 completion even if M2 can continue.

### M2 — Add the minimum real L4D2 module contracts

**Depends on:** M0 and M1's module/command/replication gate on the stock-compatible map. M2 creates its own test map in step 5; original campaign loading is not required.

1. Recover only the first scene's player, game-rules, rifle and common-infected factories, lifecycle, datamaps, fields, events and script/data dependencies. Start from registration tables, FGD and xrefs; record function RVAs and uncertainty.
2. Establish field ownership, units, widths, ranges and prediction rules. Recover update and event order before choosing containers or moving operations between shared/client/server code.
3. Add L4D2-specific VPC wrappers and `game/mod_l4d2` using current SDK conventions. Integrate only validated production code from M1; remove its temporary scaffolding. Keep common Source code and remove inherited sample-game rules as replaced. During this and M3 review, check changed headers/container choices against Valve equivalents: no new STL container/stream/smart-pointer layer where existing Source facilities serve the need, except types already required by a stable external API.
4. Implement actual player/rules creation, spawn/despawn, input consumption, shared movement state, send/receive fields and connection cleanup. Use existing macros and handles; no speculative schema generator or placeholder entity hierarchy.
5. Build a small target-format test map with spawn, collision, a target/route and the interaction geometry needed by the first scene. Keep its authoring source and compiler settings so it is reproducible.
6. Connect two clients, move, observe authoritative state and corrections, disconnect/reconnect, then unload the map. Resolve ownership/lifetime faults before adding more entities.

**Keep:** focused contract notes, paired shared/network/prediction definitions, project/output configuration and test-map source.

**Exit:** actual input reaches the reconstructed player/rules and produces visible server state on both clients; teardown does not introduce stale entities or state. This gate does not claim full combat or AI.

### M3 — Finish one complete gameplay slice

**Depends on:** M2 and the native/adapted content needed by the selected scene. Use the target-compiled map; do not wait for a shipped BSP.

1. Expand the original rifle scene to include moving fire, empty fire, cooldown boundaries, reload interruption, target attack, hit/damage and death. Record observed oddities and the order of ammo, animation and damage changes.
2. Recover and implement the whole weapon path: usercmd consumption, predicted ammo/timers, authoritative firing/trace/damage, reconciliation and received effects. Preserve recoil/spread/RNG order and lag-compensation dependencies as observed.
3. Implement the common infected's actual perception, approach, attack, damage and death path using the existing NextBot/nav framework. Add only the animation/traversal contracts required by this encounter; a stationary target does not satisfy the infected slice.
4. Connect original player/weapon/infected assets, animation activities/events, sound, impact/death effects, viewmodel/camera and HUD. A correct server trace with missing or fabricated client feedback is incomplete.
5. Compare the original and port with one client and then two. Exercise recorded latency/loss, correction, simultaneous fire/reload boundaries, death, reconnect and map reload.
6. Correct port-introduced differences at the responsible call path. Preserve confirmed original bugs and add narrowly focused regression cases for uncertain ordering or boundary behavior.

**Keep:** paired scenario captures, relevant call-path evidence, ammo/health/timer transitions and visible/audio results.

**Exit:** moving clients and the server-controlled infected complete the interaction with matching state and presentation, including the recorded quirks. No placeholder AI, hard-coded input outcome or debug-only effect counts as completion.

**M3 integration evidence (2026-10-21; not an exit pass):** the post-reload
client crash was a retained brass-effect ownership error; the viewmodel now uses
the native invalidating `HPARTICLEFFECT`, matching original client `0x03A3C0`.
Weapon-info particle-name precaching and the real original `particles/error.pcf`
are integrated; the latest one-client S1 capture has no repeated `"error"` lookup.
Original VFONT decoding, condition-resolved in-memory resources and the pinned,
reversible client-only surface IAT acceptance adaptation are registered and
exercised. Native `Open` readback and genuine GDI face/SFNT-head checks succeeded;
shutdown restored the IAT. This is runtime adaptation, not native VFONT support.
Continuation evidence and the per-gate stop disposition are in
`E:/l4d2_work/m3_integrator3_final_20261021.txt`; prior fixes remain documented in
`E:/l4d2_work/m3_integrator2_final_20261021.txt`. Fresh captures exercise the rifle
HUD glyph, the original rich `player_hurt` producer/staging, one-client S1 with
lag/loss, and listen changelevel plus disconnect/map. Private loose and
VPK-adjacent sound-cache ownership is now exercised. Unique per-run JPEG names
and completion checks replace a stale asynchronous-screenshot capture race.
The native studio material-list seam now reaches material preparation and binds
the typed Infected state. Subsequent shader intermediates prove palette albedo,
normals, diffuse/specular lighting and pre-fog color were valid: disabled scene
fog was instead converting the target's unrigged vertex-fog constants to full
black fog. A native scene-fog combo fixes that consumer mismatch; fresh
same-model/skin/body and framebuffer captures show colored Infected, with
enabled/disabled-fog controls. This is a bounded color/fog correction, not full
blood/corpse/flashlight parity (`m3/integrator4_infected_path.txt`).

Original healthy survivor fire/reload/deploy gesture selectors, custom feet and
head/body aim, and native seven-slot ownership are integrated. Color Random now
has its full scalar/SIMD producer, native/direct-parent tint caches, output-22
storage and renderer transport; original512-row sampling uses proven even rows
of the native1024-row table. Both features compile; their live acceptance must
use the corresponding run manifest, not shader-only correction captures.
An integrated startup dump exposed a nested DMX-context lifetime error in the
new renderer; its local context was removed at the responsible owner, retaining
the native particle parser's context and transient-element cleanup.

Original native-layer observation now validates the actual loaded ImageBase
and relocated executable bytes. Supplemental retained active-layer crossings
explain that selected model's nonuniform `.6/.8s` hits; historical S1 still lacks
its model/overlay/seed, and no timer was tuned to force `.7s`. Two-client
combat/reconnect, full armed/shove presentation and the remaining §7 reload,
penetration, selection/history and once-only effect gates remain separate open
acceptance rows. Terror NAV integration is built, not a production C++ corpus
reader pass. Exact continuation evidence is in
`E:/l4d2_work/m3_integrator4_final_20261021.txt`. M3 is not an exit pass.

### M4 — Complete survivor and combat behavior

**Depends on:** M3; each interaction also needs its M1 content/runtime capabilities.

1. Complete movement and collision one transition at a time: standing/crouching, acceleration/friction, jumping/landing, ladders/traversal, use interactions and view changes. Compare edge geometry, tick boundaries and original movement bugs rather than “improving” them.
2. Implement inventory acquisition/drop/swap, weapon selection, usable objects, spectator state and player/bot takeover. Preserve entity handles, ownership changes, interruption and disconnect outcomes.
3. Complete permanent/temporary health, incap, ledge, death, revive, rescue and defibrillation. For each, recover entry/exit conditions, timers, competing users, cancellation and simultaneous damage.
4. Finish all firearms, reload/ammunition rules, recoil/spread/penetration where present, melee hit scheduling, shove/fatigue, throwables, healing/buffs and mounted weapons. Recover each family rather than reskinning one weapon with different numbers.
5. Complete damage/hitgroups, friendly fire, fire/explosives and other hazards; integrate animation, sound, HUD and effects for every state. Preserve original ordering of damage, pain, incap/death and events.
6. Compare interrupted and competing actions, transitions exactly at timer thresholds, network corrections, reconnect and chapter cleanup. Treat known original failures as expected cases, not defects to remove.

**Keep:** per-interaction contract/evidence notes and boundary scenarios, not a separate testing framework per weapon.

**Exit:** every survivor-state and combat-family row is exercised end-to-end. No weapon/item silently uses sample-game behavior; no new port-only state, damage or prediction discrepancy remains in those cases.

### M5 — Complete navigation, survivor bots and infected

**Depends on:** M3, the M4 contracts consumed by each actor, and M1 nav/model/animation capabilities. Independent actor slices can overlap M4.

1. Recover original nav main/subversions, area flags, connections, ladders/traversals and L4D2 metadata. Implement their reader and consumers in server code. Keep original area IDs, topology and flow/spawn annotations; generic nav regeneration is not an equivalent substitute.
2. Implement survivor-bot goals, target selection, item use, combat, revive/rescue, coordination, obstacle handling and human takeover against the real survivor rules. Recover priority changes and failure handling rather than inventing a cleaner planner.
3. Finish common/uncommon infected populations, perception, group response, traversal, attacks and death. Preserve scheduling, update budgets and random-selection order where they affect outcomes.
4. Implement Smoker, Boomer, Hunter, Spitter, Jockey and Charger as complete attack-state slices; implement Tank and Witch behavior separately. Include victim ownership, pin/release, interrupted attacks, actor/victim death, animation and audio.
5. Complete human-controlled infected, ghost/spawn restrictions, target legality and team/spectator transitions. Use the same authoritative attack rules where the original does; do not assume bot and human paths are identical without evidence.
6. Compare climbs, doors/obstacles, navigation failures, simultaneous attacks and competing bot objectives on controlled geometry and supported original areas. Preserve observed stuck/path-selection bugs instead of silently applying newer NextBot fixes.

**Keep:** nav field/semantic notes, per-actor transitions, original/port route/spawn cases and stated nondeterminism.

**Exit:** each actor's complete lifecycle matches its recorded original scenarios, including failures and release/death cases. A bot that never gets stuck is not “more correct” if it changes the original behavior.

### M6 — Complete Director, scripting and shipped campaigns

**Depends on:** the relevant M4/M5 state and AI, plus M1's proven content path for each chapter. Director sub-slices may start on controlled maps earlier.

1. Recover flow/distance, intensity/pacing, population budgets, encounter selection and item-placement decisions from focused original call paths and scenarios. Identify inputs, state lifetime, timers and RNG usage before implementing the decisions.
2. Integrate those decisions with real survivor/infected/nav state. Preserve boundary behavior, cooldowns and original placement/pacing bugs; do not compensate for incomplete AI by adjusting Director constants.
3. Inventory each chapter's entity classes, keyvalues, I/O chains and mission/mode/weapon data. Implement missing consumers and original callback order; parsing a field without using it is not support.
4. Complete shipped script payload handling, globals/classes, callbacks, think/event scheduling, VM lifetime and script error behavior. Integrate response rules, dialogue, scenes and music consumed by campaign events.
5. Implement panic/crescendo/gauntlet events, safe rooms, rescue, chapter transitions, supported persistence and each finale stage. Check cancellation/restart, survivor death and human/bot changes at transitions.
6. Run every shipped campaign/DLC chapter through its final outcome with four survivor slots and human/bot takeover. Validate geometry, collision, static props, visibility, lighting, nav and gameplay separately; no visual approximation is allowed to hide a changed route or hitbox.

**Keep:** per-chapter dependencies/progression, Director/script/I/O observations, content-reader provenance and any explicitly approved exceptions.

**Exit:** campaigns complete under conditions where the original completes, with no additional port-introduced blockers. Separate cases reproduce known original softlocks or progression bugs. One working campaign does not close the others.

### M7 — Complete modes, client presentation and session lifecycle

**Depends on:** relevant M4–M6 systems and M1 UI/Steam, input/split-screen and presentation capabilities.

1. Complete campaign/co-op and single-player with bots, realism, versus, realism-versus, survival, scavenge and mutations. Recover actual rules, infected/Director changes, scoring, win/end conditions and round transitions; a menu toggle is not a mode.
2. Complete teams, voting, spectator changes and interrupted/abandoned rounds. Compare map changes and disconnects at state boundaries, including original scoring/session quirks.
3. Reconstruct menus, lobby/campaign selection, permissions, membership, discovery, join/leave and server handoff using target-supported UI/Steam facilities. Analyze original matchmaking modules as references; do not load x86 modules or invent service responses.
4. Finish local/listen/dedicated lifecycle, reconnect, takeover, changelevel, shutdown and supported persistence. Verify server-only execution has no accidental dependency on a client UI, renderer or client-installed hook.
5. Finish HUD/crosshair/inventory/scoreboard, localization, chat/voice, camera, animation, particles/screen effects and music/dialogue. Verify alive/incapped/dead/spectator and infected-player states, not just the default survivor view.
6. Complete keyboard/mouse/controller and split-screen slot ownership through input, prediction, rendering, UI and connection lifecycle. A second camera without an independently functioning local player does not satisfy split-screen.
7. Validate custom-map/addon selection, mounting and precedence, mission/script/entity contracts and dependency-failure behavior. Restore supported addon semantics deliberately; keep third-party native plugin compatibility separate from content compatibility.

**Keep:** mode-by-mode rule/scoring and lifecycle scenarios, real service results, input/split-screen captures and addon-resolution cases.

**Exit:** each listed mode completes its real selection/hosting/match flow; every required local/listen/dedicated and split-screen path is demonstrated. Unresolved services or engine limits remain blockers unless explicitly rescoped.

### M8 — Close fidelity and release requirements

**Depends on:** required M0–M7 capabilities, with any accepted exceptions explicitly approved.

1. Close each feature row against original evidence, owning implementation and a running scenario. Resolve conflicting observations; original bugs stay expected results, while unapproved port differences stay open.
2. Run integrated cross-campaign/mode cases, including takeover, reconnect, changelevel, cleanup, interruption and bug-compatible boundaries. Reuse focused cases; do not substitute a huge generic test suite for gameplay observation.
3. Exercise sustained horde/nav/combat/network loads. Attribute frame-time, memory or correctness regressions to concrete changes; do not reduce populations, change think frequency or alter rules just to match a performance number.
4. Reproduce the x64 build and isolated deployment from pinned inputs. Verify original-content resolution, any version-bound loader support, fail behavior on an unsupported engine build and an unmodified native-target asset.
5. Audit package/provenance, any detour dependency and new include/container choices against section 3.5; remove throwaway debugging/conversion scaffolds. Keep SDK/third-party notices; distribute neither retail binaries/assets nor uncleared reference code. No unapproved STL container/stream layer or behavior substitution. Describe runtime adaptation as adaptation, not native engine support.

**Keep:** the final state of the existing ledger and baseline manifest, measured comparison/load captures, reproducible build/deployment instructions and approved exception decisions—not a second completion tracker.

**Exit:** a fresh isolated deployment completes the required scenarios with no unexplained fidelity difference. Compiling DLLs, opening one campaign, or bypassing a parser check does not establish completion.

### Dependency and stop rules

- M1 is not an all-or-nothing barrier. Its module/command/replication gate on M0's stock map unlocks M2; M2 supplies the gameplay test map. Unresolved first-scene model/animation/texture/sound/effect consumption blocks M3 completion. Original campaign-map loading blocks M6, while split-screen/service gaps block their relevant M7/final gates.
- Stop only the affected slice when evidence is ambiguous, a binary/content revision changes, a loader cannot supply missing semantics, or a required service is unavailable. Continue independent work without silently shrinking final scope.
- Reject unknown hook targets, mismatched layouts and unsupported inputs with a clear diagnosis; do not disable validation globally, guess an offset or conceal failure behind a placeholder asset.
- If offline conversion or a workaround changes gameplay, original bugs, hitboxes, timing, geometry or other required fidelity, retain the original and request approval before accepting the difference.
- Do not move shared-schema, engine-lifecycle and VPC edits between competing owners. Agree on the small shared contract, integrate paired client/server changes, then run the affected scenario.

## 8. Verification protocol and immediate work order

### Original-versus-port comparison

1. **Specify conditions before implementation.** Record original/target build identities, map/content hashes, mode, difficulty, tick interval, relevant cvars, starting state, actions and seed only where controllable. Include timing boundaries and cancellation/failure cases relevant to the behavior.
2. **Observe the original first.** Use existing console/events and focused inspection, supplemented by temporary diagnostics only when needed. Record authoritative state transitions separately from animation/audio/UI timing. Preserve captures of bugs rather than rewriting the expected result.
3. **Exercise the real port path.** Drive input through client commands, server simulation and replication/prediction. Use actual rendering/audio or dedicated-server behavior as appropriate; calling an isolated method is insufficient when the contract includes the engine lifecycle.
4. **Compare the first divergence.** Check state, call/event order, RNG consumption, timers, trace/hit results and presentation. Normalize only irrelevant identities such as pointer addresses; do not normalize away event order, entity-selection ties or failure behavior. Retail demos and network packets are not automatically replayable through the target protocol.
5. **Handle numeric differences deliberately.** Preserve observed float/integer widths, conversion points, tick/time rounding and branch boundaries. Investigate x87/SSE behavior, accumulation order and compiler floating-point settings where evidence indicates a difference; do not globally change precision or introduce epsilon comparisons as a repair. A tolerance must be measured, documented and must not hide a different gameplay decision.
6. **Handle nondeterminism honestly.** Control inputs/seeds/scheduling where the original permits. For inherently variable cases, retain original semantics and compare relevant invariants and repeated-run distributions. Similar averages alone cannot establish that the Director/AI algorithm is faithful.
7. **Classify the result.** Matching original success, failure or bug is a pass for that case. A port-introduced discrepancy is a defect. A proven original bug is not. Unresolved evidence, unavailable engine capability and deliberately changed behavior remain distinct open items.
8. **Keep only useful regressions.** Preserve deterministic cases for actual discrepancies, boundaries and ownership transitions. Remove temporary tracing and throwaway probes after their evidence is captured; do not add production telemetry or source-text/mock-echo tests to claim parity.

For loaders/hooks, additionally observe loaded module identities, reader entry points, resolved content, buffer lifetime, map unload/reload, native-target-asset behavior and rejected unsupported builds. Correct original-file hashes must remain unchanged. A header accepted by a patched check is not a smoke result; the dependent rendering, collision, AI or gameplay scenario is.

### Evidence retained by this planning revision

- Read-only original VPK/member, BSP and nav header probes established the sample facts in section 2.
- Target-engine decompilation located actual BSP validation/header-reader comparisons and confirmed their 19–20 range in the pinned AMD64 binary.
- Reference-source comparison informs the proposed mount/loader ownership and Valve-style rules; it does not prove that a runtime hook works.
- No hook was installed, no game DLL was implemented or built, no asset was recompiled, and no game-runtime loading or gameplay comparison was performed.

### Next implementation batch

Execute M0, then M1's content-only mounting and per-format probes. Start with the sampled VTF 7.4 materials, the sampled v49 model families, and a complete survivor/common-infected/rifle dependency set required by M3. Investigate `c1m1_hotel.bsp`/`.nav` as the separate bounded campaign-map case. M2 can progress on a target-format test map while readers are unresolved, but M3 cannot claim completion without its actual asset families. Do not begin a bulk asset rebuild or whole-binary pseudocode dump.

Use the actual generated project/output names and installed x64 launchers. Hook targets, startup timing, optional detour dependency and any runtime translation must be justified before implementation; no guessed command-line switches, unverified offsets or automatic fallback assets.

## 9. Provenance, distribution, and completion limits

The workspace `LICENSE:9-14` permits SDK use/modification/distribution subject to its noncommercial/free-distribution and notice requirements. That does not establish rights to copy every supplied reference tree or redistribute L4D2 assets. Preserve provenance per imported component; default uncleared reference code to conceptual use and write new implementation code from documented contracts/behavior. This is not a formal clean-room claim.

Keep retail assets/binaries local; a distributable mod should derive or locate necessary content from an authorized user installation rather than bundling it. Validate applicable permissions before publishing decompiled material, converted assets, or reused reference code. Preserve SDK and third-party notices.

Completion means the feature matrix and milestone gates are satisfied, not that both DLLs compile or one map runs. Engine-only, licensing, toolchain, asset-fidelity and external-service gaps remain visible blockers until resolved or explicitly accepted as a scope change.
