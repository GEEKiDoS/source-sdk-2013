# Source SDK 2013

Source code for Source SDK 2013.

Contains the game code for Half-Life 2, HL2: DM and TF2.

**Now including Team Fortress 2! ✨**

## Build instructions

Clone the repository using the following command:

`git clone https://github.com/ValveSoftware/source-sdk-2013`

### Windows

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - Visual Studio 2022 with the following workload and components:
   - Desktop development with C++:
     - MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)
     - Windows 11 SDK (10.0.22621.0) or Windows 10 SDK (10.0.19041.1)
 - Python 3.13 or later

Inside the cloned directory, navigate to `src`, run:
```bat
createallprojects.bat
```
This will generate the Visual Studio project `everything.sln` which will be used to build your mod. The generated game projects are the Episodic ones (`client_win64_episodic`, `server_win64_episodic`, `launcher_main_win64_episodic`); to target TF2 or HL2: DM instead, replace `/episodic` in `createallprojects.bat` (and `buildallprojects`) with `/tf` and/or `/hl2mp`.

Then, on the menu bar, go to `Build > Build Solution`, and wait for everything to build.

You can then select the `Client (Mod Name)` project you wish to run, right click and select `Set as Startup Project` and hit the big green `> Local Windows Debugger` button on the tool bar in order to launch your mod.

The default launch options should be already filled in for the `Release` configuration.

### Episodic mod (Windows x64)

`game\mod_episodic` runs Episode One/Two game code on the x64 Source SDK Base 2013 Multiplayer engine. Its `gameinfo.txt` mounts content from three Steam installs, which must all be present:
 - Source SDK Base 2013 Multiplayer (243750): the engine and its `hl2` content, whose shaders match the engine.
 - Source SDK Base 2013 Singleplayer (243730): the Episode One/Two VPKs this SDK's game code and UI were made for.
 - Half-Life 2 (220): only for the loose episode maps. This engine does not mount the embedded pakfile (patched water materials, cubemaps) of a map read from a VPK.

The client sets `cl_localnetworkbackdoor 0` on x64 because the engine crashes with the singleplayer network backdoor ([#610](https://github.com/ValveSoftware/source-sdk-2013/issues/610)).

**Skill configuration:** Use Source's `//` comment syntax and cvars registered by the SDK game DLLs. The shipped `game\mod_episodic\cfg\skill.cfg` converts the copied retail HL2 file's 37 `#` comment lines and removes its unsupported `sk_npc_dmg_strider_to_plr`, `sk_npc_dmg_strider`, and `sk_max_strider` assignments. Game rules re-execute the skill manifest on each map spawn. During a connected level transition, unknown tokens are forwarded as client string commands: the old file consumed all 40 commands in the engine's default quota, then the normal `unpause` command triggered `#GameUI_Disconnect_TooManyCommands`. Valid skill values, per-map refresh, and the quota remain unchanged. A physical `d1_trainstation_01` → `d1_trainstation_02` landmark transition was verified with zero invalid skill commands and the client still connected.

**Save compatibility:** 64-bit physics saves now use format 7. Older format-5/6 saves contain truncated pointer identities and, for pointer lists/arrays, missing entries; they cannot be safely recovered. Loading rejects them before restoring entities. Start a map and create a new save with this build; 32-bit saves retain format 5.

The full-width save/restore bridge is certified for the Windows x64 SDK Base 2013 Multiplayer `vphysics.dll` with PE timestamp `0x67b40ef3` and image size `0x15d000`. It uses game-side serialization and the provider's existing object helpers; it does not patch Steam files, executable code, or vtables. An uncertified 64-bit provider visibly refuses saving/loading rather than writing or restoring truncated pointers. Engine updates require recertifying the profile in `src/game/shared/physics_saverestore.cpp`.

DX12 rebinding no longer calls the previous borrowed material during shutdown or level changes. It caches page identity from the live incoming material, advances the selected material on same-page binds, and clears both caches on device reset/shutdown.

The DX12 renderer needs the native shader pack published into the mod once (and after shader changes):
```bat
cd src
devtools\bin\buildshaders_dx12.bat -game ..\game\mod_episodic
```
Without it every DX12 draw fails and the screen stays black. Launch with `game\mod_episodic_win64.exe`, or `game\start_ep2.bat` (extra arguments such as `-dx9` or `+map ep2_outland_01` are passed through).

**PBR materials:** `PBR_Metalness` / `PBR_Specular` and the `PBR_*` adapters are DX12 material shaders; `mat_pbr_override 1` switches LightmappedGeneric, VertexLitGeneric and WorldVertexTransition to the PBR path at runtime without removing the old shaders (details: `src/materialsystem/stdshaders_dx12/README.md`, "PBR shader set"). The DX12 renderer appends `-forceallmips` to the engine command line (`CShaderDeviceMgrDX12::Init`, before `CMaterialSystem::Init` reads it) so cubemap mip tails exist for rough reflections.

**`buildcubemaps` on enhanced maps:** the engine renders every face of every cubemap sample inside one client frame. Each face used to allocate a new receiver view with its own 4096² sun targets, so captures ran out of GPU memory after a few samples (`Shadowmaps: insufficient shadow residency`, `ReadPixels ... 0x8007000e`) and the rest were black. Completed views are now reused while `building_cubemaps` is set. Faces captured from inside a moving brush entity (e.g. a train in `d1_trainstation_01`) are still black; that is the scene, not the renderer.

**Parallax-corrected cubemaps:** load `game\bin\pbr_cubemaps.fgd` after the game's base FGD in Hammer. Make a box brush entity `parallax_obb` (may be rotated) with a `targetname`, and set the `env_cubemap`'s `parallaxobb` key to it. VBSP removes the entity and writes `$envmapparallaxobb1..3` / `$envmaporigin` into the patched materials of that cubemap (only VMTs that literally contain `$envmap env_cubemap` are patched); PBR shaders then intersect the reflection ray with the box.

DX12 occlusion queries retain one logical result across command-list submissions. Submission ends and resolves each active native segment before closing its list, then resumes on the next list; completed segment counts are accumulated with fenced readback reuse. Query destruction and shutdown also balance native intervals and retain their GPU objects. RTX 4090 and WARP regression runs verified exact visible/hidden/reused counts and sampler-descriptor rollover without debug-layer errors. This fixes the reproduced cross-list query violation; the exact native API that failed in the original queued-submission dump was not established.

Streamline SDK logging is off by default, with no SDK callback forwarding into the game console. `-dx12framegenlog` enables verbose plugin-directory file logging without console forwarding. Renderer errors, DLSS-G API diagnostics and NVIDIA signature validation remain enabled. The signed interposer still prints its own unconditional signature-success notices to process stdout; no signed DLL modification or process-wide stdout filter is used.

Optional Counter-Strike: Source content mounts below the required episodic/HL2 content. Keep that precedence: particle initialization reads the first `GAME` particle manifest, so putting CSS first hides Episode Two's Vortigaunt effects. Hand-glow and charge-token callbacks also check for a missing particle definition before assigning its control-point entity; successful effects retain their normal attachment.

Viewmodel sway bounds its facing interpolation factor to `[0, 1]`, preventing long frames from extrapolating the stored facing past the camera without changing normal interpolation.

DX12 shadow-caster notifications do not sample world bounds synchronously. Entity transform setters notify before committing their local values; a following actor's bound query could recompute its viewmodel parent's absolute transform from the old angles and clear the dirty flag. Bounds refresh at the ordinary leaf flush or shadow query instead, with a separate pending bit preserving pose-only silhouette changes and both old/new affected light volumes. Ineligible casters, including viewmodels, are not sampled. An owned `ep2_outland_06` pickup-ready/closed/ready-again and friendly-NPC pistol lower/raise run completed normally: all 2,760 sampled local/absolute rotations matched, and the open gravity gun and lowered pistol were visually verified. Temporary pose/stack instrumentation was removed afterward.

### DX12 Unicode title DLL (Windows x64)

The `unicode` project builds a source-compatible replacement for the installed x64 `unicode.dll`. It exports the same `CreateInterface` factory and `VENGINEUNICODE001` methods; unless `-dx9` (or `-gl`/`-vulkan`/`-noshaderapi`) is passed, an x64 launch changes an incoming `Direct3D 9` window-title substring to `Direct3D 12`. Do not overwrite the Steam SDK installation. Generate and build the isolated project from `src`:

```bat
cd /d F:\repo\source-sdk-2013\src
devtools\bin\vpc.exe /hl2mp /win64 /define:SOURCESDK +unicode /mksln unicode
msbuild unicode.sln /m /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0
```

The VPC post-build step publishes the build artifact directly to the mod launcher's effective lookup path, `game\bin\x64\unicode.dll`. Launch from the `game` directory so the Source loader resolves this artifact before the SDK installation's PATH fallback; do not overwrite the Steam SDK installation.

Run the x64 launcher from `game` and inspect `MainWindowTitle` while each process is running:

```bat
cd /d F:\repo\source-sdk-2013\game
mod_hl2mp_win64.exe -game mod_hl2mp -dx9 -windowed -novid
mod_hl2mp_win64.exe -game mod_hl2mp -windowed -novid
```

Expected result: the `-dx9` launch retains `My First HL2MP Mod - Direct3D 9 - 64 Bit`, while the default launch (native DX12) displays `My First HL2MP Mod - Direct3D 12 - 64 Bit`. Both launches should load `F:\repo\source-sdk-2013\game\bin\x64\unicode.dll`; the read-only Steam SDK `bin\x64\unicode.dll` remains untouched.

### Linux

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - podman

Inside the cloned directory, navigate to `src`, run:
```bash
./buildallprojects
```

This will build all the projects related to the SDK and your mods automatically against the Steam Runtime.

You can then, in the root of the cloned directory, you can navigate to `game` and run your mod by launching the build launcher for your mod project, eg:
```bash
./mod_episodic
```

*Mods that are distributed on Steam MUST be built against the Steam Runtime, which the above steps will automatically do for you.*

## Distributing your Mod

There is guidance on distributing your mod both on and off Steam available at the following link:

https://partner.steamgames.com/doc/sdk/uploading/distributing_source_engine

## Additional Resources

- [Valve Developer Wiki](https://developer.valvesoftware.com/wiki/Source_SDK_2013)

## License

The SDK is licensed to users on a non-commercial basis under the [SOURCE 1 SDK LICENSE](LICENSE), which is contained in the [LICENSE](LICENSE) file in the root of the repository.

For more information, see [Distributing your Mod](#markdown-header-distributing-your-mod).
