# L4D2 x64 port on Source SDK 2013 Multiplayer

The L4D2 client, server and game-owned shaders run on the SDK 2013 Multiplayer
x64 engine. The underlying SDK also contains Half-Life 2, HL2: DM and TF2 code.
Use the L4D2 instructions below for this port; the generic SDK instructions are
not its pinned build recipe. Milestone status and accepted engine limitations
remain in `L4D2_LEDGER.md`; a successful build is not a completed fidelity gate.

## Build instructions

Clone the repository using the following command:

`git clone https://github.com/ValveSoftware/source-sdk-2013`

### Upstream SDK Windows build (not the pinned L4D2 recipe)

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

### L4D2 in-place deployment (Windows)

The L4D2 port runs from `game/mod_l4d2`. The client starts through the repo-built
mod launcher `game/mod_l4d2_win64.exe`, which loads the SDK Base 2013
Multiplayer x64 engine and redirects `shaderapidx9` to the DX12 renderer in
`game/bin/x64`; pass `-dx9` to keep the stock DirectX 9 renderer. The installed
`hl2_win64.exe` still runs the mod on DX9 (`srcds_win64.exe` for dedicated
hosting). Original-game tests run from the real Left 4 Dead 2 Steam install.
Do not create staged game roots, mirrors, junction trees or bulk runtime-asset
copies. Startup files are provisioned locally from the user's retail install.

```bat
cd /d game
mod_l4d2_win64.exe -game "%CD%\mod_l4d2" -novid -windowed +map c1m1_hotel
```

The DX12 renderer needs its native shader pack published into the mod once
(and again after DX12 shader changes): `cd src` then
`devtools\bin\buildshaders_dx12.bat -game ..\game\mod_l4d2`. Its optional
upscaler, frame-generation and denoiser runtimes (DLSS, FSR, XeSS, Streamline,
Open Image Denoise) are copied beside the renderer into `game/bin/x64`; they are
not part of the repository.

L4D2's render-target owner initializes and shuts down the shared DX12 motion,
upscaler, GTAO, screen-space and post-processing resources. The native DX12
stack owns tone mapping and bloom; TerrorEnginePost then applies L4D2's remaining
effects in display-sRGB space and returns linear scRGB to the DX12 framebuffer.
Film grain retains its strength control and luminance weighting (more in shadows,
less in highlights), rather than being amplified by adding it directly in linear
space. Its downsampled local-contrast luminance uses the same display-sRGB domain.
L4D2's precache-register system also primes the post-processing materials and
their variable tokens before queued rendering. It replaces the SDK client-effect
precache system, so it must retain that system's post-process preparation step;
otherwise histogram queries never start and automatic exposure remains at unity.
Native DX12 SpriteCard uses retail's zero alpha-test reference; the SDK's
0.01 reference cuts faint particle glow falloff into a visible hard-edged disc.
The renderer's fast pipeline memo renews each reused PSO's retirement fence, so
cache reclamation cannot release it while recorded or GPU work still references it.

DX12 is the mod's default renderer; `-dx9` remains supported. The game-owned
shaders in `game_shader_generic_l4d2` (LightmappedGeneric, WorldVertexTransition,
VertexLitGeneric, UnlitGeneric, UnlitTwoTexture, Water, the flashlight families,
Infected, SplineRope and the Terror post shaders) override the stock shaders by
name on both renderers. On DX12 with `mat_pbr_override 1`, LightmappedGeneric,
WorldVertexTransition and VertexLitGeneric fall back to `PBR_LightmappedGeneric`,
`PBR_WorldVertexTransition` and `PBR_VertexLitGeneric` (`l4d2_pbr_route.h`), so
world and model materials get PBR shading, high-resolution lightmaps and
shadow-map receivers. The PBR shaders carry the L4D2 material features (decals,
`$vertexcolor`, `$ssbumpmathfix`, blend tint, Phong tint/boost/Fresnel ranges,
self-illumination masks, `$flashlightnolambert`, tree sway, `$diffuseexp` and the
detail blend modes). Materials using a feature the PBR shaders do not reproduce
(lightwarp, Phong warp, rim light, `$shinyblood`, seamless mapping, distance
alpha, `$selfillumfresnel`, vertex fog, wound/cloak/flesh passes) keep the
owned shader. The other owned shaders, and everything under `-dx9`, run their
DX9 VCS (on DX12 through the renderer's legacy-VCS translation; `-dx12shaderlog`
lists them from `shaders/fxc`). The shader clock is
`FLOAT_RENDERPARM_L4D2_CURRENT_TIME`, which follows
the DX12 float render parameters. Post-processing ownership on DX12: the native
stack owns the final tone curve, bloom, lens dirt, temporal AA/upscaling (SMAA
T2x on GPUs without DLSS), GTAO and motion blur. L4D2 keeps histogram exposure,
env_tonemap_controller, depth of field and the TerrorEnginePost effects. The
item/teammate glow remains after temporal AA/upscaling in native order; on DX12
its native halo samples a full-resolution R8 stencil-coverage mask through a
history-free 3x3 tent filter, smoothing only the inner edge without filtering the
scene or retaining moving teammates. DX9 and `mat_postfx_dx12 0` keep the original
glow composite unchanged.
Motion blur on both renderers is the SDK/DX12 `DoImageSpaceMotionBlur` (DX12
velocity blur, SDK camera blur on DX9); the former Terror motion blur and its
shader were removed.

L4D2's **Options > Video > Graphics (DX12)...** opens a native options screen
with Display, Lighting, Post-processing and Frame generation sections. It uses
the mounted Video screen's hybrid buttons, flyouts, sliders, fonts and footer;
`graphics_options [section]` opens the same screen. Its bindings and choices
come from the shared graphics row table, while rows with missing, hidden or
development-only ConVars are omitted (including DX12-only rows under `-dx9`).
Changes take effect immediately and Done / Escape / controller B returns to
Video, saving through `mat_savechanges` and `host_writeconfig`. Other SDK games
retain their four-tab GameUI advanced graphics dialog.

Dedicated hosting uses `srcds_win64.exe -console -game <absolute mod_l4d2 path>
+sv_pure 0 +mp_gamemode coop +map c5m1_waterfront`. Preserve its native console
input handle when redirecting output. The BSP/VPK readers select `dedicated.dll`
for filesystem hooks; static-prop tint rendering hooks and material-system
quiescence are client-host-only and are not installed on a dedicated server.

Configure the local mod once at deployment time, not once per harness run:

```bat
py -3 scripts/l4d2/prepare_deployment.py --original-install "C:/Program Files (x86)/Steam/steamapps/common/Left 4 Dead 2" --sdk-install "C:/Program Files (x86)/Steam/steamapps/common/Source SDK Base 2013 Multiplayer"
```

When binaries are not available yet, `--gameinfo-only` renders and validates
local gameinfo/startup inputs without checking or copying DLL/PDB/VCS artifacts:

```bat
py -3 scripts/l4d2/prepare_deployment.py --gameinfo-only
```

The tool renders tracked `gameinfo_template.txt` into the gitignored local
`gameinfo.txt`. It mounts the original update, DLC3, DLC2, DLC1, and base archives
and their real loose-content directories in that order, with SDK base game content
below them. The read-only original platform directory follows the SDK platform
archive and loose directory, supplying missing VGUI localization while preserving
the SDK GameUI and ServerBrowser resource layouts. No bulk assets are copied
and no content views are created. Mod configuration, caches, and logs use the
normal in-place game write paths. Prepared HUD resources retain authored empty
strings, including the console's deliberately empty frame icons.

Deployment copies `cfg/valve.rc` and `cfg/config_default.cfg` from the original
installation's `left4dead2` directory into ignored local files, preserving the
original `joystick.cfg`, optional user `autoexec.cfg`, `stuffcmds` order and
first-run defaults. The original PC install has no `scripts/kb_def.lst`, so
deployment generates that ignored file from the original default cfg's bind
pairs in their existing order, using the SDK list format. Missing original
startup files/bind commands fail deployment; no HL2MP bindings are substituted.
Do not distribute these retail-derived local files, copy a user's `config.cfg`
or `autoexec.cfg`, or force defaults over an existing user configuration.

The quick-message radial menus load their authored `scripts/RadialMenu.txt` and
`resource/UI/RadialMenu.res` through these mounted original search paths.
`CRadialMenu` inherits the standard VGUI frame layout, as in the native client;
opening the menu restores its alpha and only its selection/cancel fade reduces
it. Layout must not reset the panel alpha, which also hides every item and label.
Scene-dispatched survivor speech uses the original caption dictionaries and the
same server hash/repeat lookup as sound-emitter captions. Its `CloseCaption`
message carries the token CRC32, 15-bit duration, and one player flag bit, not the
SDK's token-string packet. Client asynchronous caption blocks remain associated
with their originating dictionary before any token directory entry is indexed.
The SDK scheme loader also processes resolution-suffixed keys that the original
L4D2 loader preserves. At normal resolutions this renames the original
`CloseCaption_Normal` font definition to `CloseCaption`. Caption font lookup
accepts either name for that same authored definition, including cached schemes
that survive a video-mode change; it does not substitute another font style.

Survivor first-person arms are a separate, bone-merged viewmodel attachment,
selected from the viewmodel owner's character and weapon script, including an
in-eye observer's survivor target. The attachment is initialized to render with
viewmodels, as in the native client; it is not a world-model attachment.

Server viewmodel animation layers advance at the original unit playback rate,
independently of the main sequence's playback rate. In particular, a stopped
main idle sequence must not freeze the chainsaw startup layer and prevent its
normal transition to continuous contact damage and fuel consumption.

L4D2 ladder movement uses the native shared climb-speed virtual: ghosts climb at
200 units per second; every non-ghost, including survivors, climbs at 100.
Both prediction and the server use this policy instead of the SDK's fixed 200.

L4D2 movement initializes each command's maximum from `sv_maxspeed`, then
applies the native ability/client speed limits. It must not first clamp to the
player's ordinary class speed: that truncates a human Charger's charge from
500 to 250 units per second.

Jockey victims and incapacitated survivors use the original
`CTraceFilterNoNPCsOrPlayer` movement filter, not a plain single-entity filter.
This allows a ridden survivor to move without colliding with the attached
Jockey while retaining world and prop collision.

L4D2 stuck-check eligibility is separate from acceleration eligibility. Pinned
actors skip stuck recovery, but a Jockey victim still uses the CS acceleration
policy so the server's blended ride commands can move the survivor. Prediction
uses the same separate gates; it does not invent rider commands on the client.

Tank claw sweeps retain the original mode distinction: player-controlled
infected modes and Survival allow one swing to hit multiple survivors.
Realism does not enable that rule. The same Survival predicate controls the
second-victim hit notification; the client sweep gate and server damage path
must agree.

The L4D2 DLLs compile their own `tier1/convar.cpp` object: native string setters
deliver callbacks even when the value is unchanged; stock HL2MP still uses
the unmodified shared-library behavior. The SDK lacks the original engine's
`ApplyGameSettings` / `ExecGameTypeCfg` call. For a new game the server makes
that settings request once at pre-entity level initialization, after startup/config.
`GameInit` arms this delivery, so native
`changelevel` / `changelevel2` transitions do not reset customized rule values.
For Solo UI launches this deliberately repeats the UI's already-applied
fresh-game settings. That idempotent SDK adaptation adds one mode-adjustment
log line, without persistent delivery markers or inferred session/reservation state.
Background maps retain the native co-op / Normal / zero-start-players branch.
The same-mode string setter therefore runs after all rule variables are
registered, without per-rule repairs or gameplay-time settings reapplication.

World precache initializes the Director scripts immediately after the pre-entity
game systems create the script VM, before standard entities and map items.
Mutation health, loadout and item-conversion rules therefore exist during their
first spawn, and the post-entity commentary system can load authored mutation
placements such as Dash waypoints and ShootZones.

Scripted use models forward their native `Use` input to the attached
`point_script_use_target` after handling the model itself. This is required for
authored Holdout resource purchases and other scripted build interactions.

For an already-built payload laid out as `dlls/` and `vcs/`, add
`--artifact-root <payload-directory>` to deploy the three DLL/PDB pairs to
`game/mod_l4d2/bin/x64` and the tracked-catalog-defined VCS files to `shaders/fxc`.
The expected logical `.vcs` filenames come from
`src/materialsystem/stdshaders/l4d2_shaders_dx9_20.txt` and
`l4d2_shaders_dx9_20b.txt`: an FXC stem ending in `_ps2x` becomes `_ps20` or
`_ps20b`, respectively; other stems are unchanged. Blank and `//` comment lines
are ignored. Both artifact and local shader sets must match those catalogs
exactly, without case-insensitive duplicate filenames.
`--check-only` checks the existing layout, parses gameinfo and compares the
three startup files with their installed-source projection without writing
files, computing hashes, loading DLLs or launching a game. It is not a runtime
or native-ABI check. Normal deployment generates local gameinfo/startup inputs
and optionally copies ordinary DLL/PDB/VCS build outputs; it creates no
evidence files or reports.

#### Pinned L4D2 build

The current checkout is `C:/Users/Administrator/Documents/repo/source-sdk-l4d2`.
The session-local `local://BuildRecipe.md` records the current machine recipe;
its reusable command files are in `C:/l4d2_work/build/`
(`merge_build_target.bat <solution target>` builds one target). Build roles
separately and serially.

Use VS18 Community at `C:/Program Files/Microsoft Visual Studio/18/Community`,
MSVC `14.44.35207` and Windows SDK `10.0.26100.0`. VS18 registers the MSBuild
toolset as `v145`, but both `vcvars64.bat -vcvars_ver=14.44` and
`VCToolsVersion=14.44.35207` select the required MSVC14.44 compiler; do not
substitute compiler14.51. The current `py -3` is Python3.15.

Project generation (`createallprojects.bat`, or the command below from `src`)
selects only the L4D2 game DLLs and shader DLL, the DX12 renderer
(`shaderapidx12`, `stdshader_dx12` and their build tools), the L4D2 mod
launcher and the ReSTIR lightmap baker. The HL2MP, TF and Episodic game
projects and the stock SDK tools keep their scripts in `projects.vgc` but are
not selected by the `everything` group.

```bat
devtools\bin\vpc.exe /l4d2 /define:SOURCESDK +everything /mksln l4d2.sln
```

Initialize the compiler environment once, then run separate role invocations
from `src` (with the VS18 MSBuild directory on `PATH`). Every role uses the same
switches; the game shader DLL adds `SelectedFiles=skip_custombuild`:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
set CL=/MP2
set ROLE=/p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=true /m:2
MSBuild.exe l4d2.sln /t:client_win64_l4d2 %ROLE%
MSBuild.exe l4d2.sln /t:server_win64_l4d2 %ROLE%
MSBuild.exe l4d2.sln /t:game_shader_l4d2_win64_l4d2 %ROLE% /p:SelectedFiles=skip_custombuild /p:L4D2ShaderGameDir=C:\Users\Administrator\Documents\repo\source-sdk-l4d2\game\mod_l4d2
MSBuild.exe l4d2.sln /t:shaderapidx12_win64 %ROLE%
MSBuild.exe l4d2.sln /t:nvngx_forwarder_dx12_win64 %ROLE%
MSBuild.exe l4d2.sln /t:unicode_win64 %ROLE%
MSBuild.exe l4d2.sln /t:nativeshaderpack_dx12_win64 %ROLE%
rem publish the DX12 shader pack (devtools\bin\buildshaders_dx12.bat -game ..\game\mod_l4d2) before stdshader_dx12
MSBuild.exe l4d2.sln /t:stdshader_dx12_win64 %ROLE%
MSBuild.exe l4d2.sln /t:launcher_main_win64_l4d2 %ROLE%
MSBuild.exe l4d2.sln /t:vrad_restir_dll_win64 %ROLE%
MSBuild.exe l4d2.sln /t:vrad_restir_launcher_win64 %ROLE%
```

Normal post-build publishing copies DLL/PDB pairs into the mod; do not pass
the historical `PostBuildEventUseInBuild=false` override. For deliberate
artifact deployment, take the pairs directly from
`src/game/client/Release_mod_l4d2`, `src/game/server/Release_mod_l4d2` and
`src/materialsystem/stdshaders/Release_dx9_mod_l4d2`, rather than trusting an
already-published mod-bin directory that another setup may have overwritten.
Arrange them as `dlls/` beside `vcs/` containing the committed 43 catalog VCS,
then use `prepare_deployment.py --artifact-root` as described above.
An artifact folder is not a second game root.

**Never Clean or Rebuild the shader project.** Normal shader-DLL builds use
`SelectedFiles=skip_custombuild` and retain the committed `.inc`/43 VCS inputs.
Only a shader-source/catalog change justifies explicit full regeneration with
`buildl4d2shaders.bat` / `build_l4d2_ps20.py` (or the local
`build95_shader_full.bat`); these compile port sources, never original VCS.
That path is separate from the normal shader-DLL build and must preserve the
catalog-defined 43-file closure.

The game-owned `UnlitTwoTexture` retains the native translucent pixel-shader
combo: both texture alphas multiply material modulation rather than becoming
opaque alpha one as in the SDK shader. The translucent material flag selects
that combo; vertex alpha alone does not. This preserves the infected cooldown ring
without changing the shipped material, HUD geometry, or ability timers.

`prepare_deployment.py` is the in-place deployment operator. It imports only
the shared, side-effect-free `LAYERS`/`require` helpers in `verify_inputs.py`.
The obsolete build55 manifest profiles, host/bootstrap admission, original-stage
guard and source-freeze/snapshot operators have been removed. They are not
part of this pipeline; do not recreate their manifests or receipts.

Temporary artifact/harness/analysis output belongs under `C:/l4d2_scratch` and
is deleted when its job finishes. Reusable external scripts stay under
`C:/l4d2_work`. Do not create identity manifests, qualification receipts,
build-log copies or source snapshot archives for the current pipeline.

### L4D2 source layout

Game-owned sources remain in `src/game/{client,server,shared}/l4d2`, grouped
by their existing subsystem: `player` (including `player/bots`), `weapons`,
`infected` (`common`, `special`, `boss`), `director`, `nav`, `gamerules`,
`entities`, `effects`, `hud`, `ui` (`basemodui`, `instructor`), `vscript`,
`sound`, `stats`, `demo`, and `content`, where each tree has that owner.
Generic server bot vision is in `server/l4d2/ai`; the existing
`server/l4d2/bsp21` subtree and its bundled MinHook sources remain intact.
DLL glue and genuinely cross-subsystem definitions stay at the L4D2 root.

Includes use game-root-qualified paths such as
`l4d2/player/c_terrorplayer.h` and `l4d2/weapons/terror_weapon.h`, not flat
L4D2 include-search directories or forwarding headers. The explicit source
selection in `client_l4d2.vpc`, `server_l4d2.vpc` and the inherited
`server_base.vpc` script entry remains authoritative. Physical directories do
not change DLL ownership, VPC virtual groups, source order, or which files
compile; regenerate the projects after changing source paths.

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

**Realtime light editor (DX12, enhanced maps):** `deferred_lighteditor_toggle` (or Tab) opens a port of the swarm-deferred light editor: right mouse + WASD flies the editor camera, X/B/V/C select / add / translate / rotate, click or drag-box to select, Delete removes, the Properties frame edits origin, angles, colour, intensity, radius, cone angles, spot, shadow and cookie live; Save / Reload write and read `maps/<map>_lights.txt` (KeyValues, loaded automatically at map start). Console: `deferred_light_add` (light at the crosshair), `deferred_lights_save`, `deferred_lights_reload`. Editor lights are unbaked dynamic lights in the per-view shadow light list and tiles, lighting PBR and legacy materials alike; the `r_editor_lights_max_shadowed` (default 8) nearest get realtime shadow maps, the rest are unshadowed, and they do not count against `r_shadowmap_max_realtime_lights`. Spot lights with a cookie go through the projected-texture path (shared limit of 8 projected lights per view, flashlight included). Point/spot editor lights need an enhanced (`vrad_restir -restir_shadowmaps`) map; cookie spots work on any map.

**Advanced graphics dialog (DX12):** Options > Video > Advanced… opens a client dialog (`src/game/client/vgui_graphics_options.cpp`, `resource/graphicsoptions*.res|txt`) instead of the stock GameUI one; the loose `resource/OptionsSubVideo.res` hides the stock button and the client attaches its own in the same place. Tabs: display/quality (stock rows plus PBR materials, bicubic lightmaps, detail prop distance), lighting and shadows (`r_shadowmap_enable`, `r_shadowmap_resolution` 0–3, `r_shadowmap_filter`, `r_csm_distance`, max shadowed lights, GTAO presets), post-processing (`mat_postfx_bloom_quality`, bloom intensity, `mat_postfx_lensdirt_enable`, lens flare, `mat_motion_blur_enabled` / `mat_motion_blur_quality`, exposure, sharpening, HDR output) and upscaling (`r_upscaler`, `r_framegen`, `r_reflex`). Apply writes the changed cvars, then `mat_savechanges` and `host_writeconfig`.

**SSR resolution (DX12):** the lighting tab exposes `r_ssr_resolution` independently of SSR quality. The archived default 1 preserves the quality profile's existing half/full-resolution tracing; 0.5 and 0.25 reduce each trace axis to half or quarter without lowering ray steps, roughness/cone filtering or temporal accumulation quality. At balanced quality and 1080p the trace is 960×540 / 480×270 / 240×135. Depth-plane/normal-aware ray reuse reconstructs full-resolution receivers, and the composite/history remain full resolution; lower settings trade spatial softness for cost rather than disabling reflections.

**Velocity motion blur (DX12):** the default remains `mat_motion_blur_quality 2` (High, 16 taps), at full resolution. Existing quality values 0/1/2 still select 8/12/16 taps; -1 adds Very low (4 taps), used by the low-tier adapter preset. There is no half-resolution option. CPU-certified zero-motion frames bypass the frame copy, tile compute and full-screen blur without changing velocity output, including queued material rendering. Certification requires the current resolved main-view pass, a stationary effective view-projection, matching current/previous model, bone and view-projection transforms for every emitted motion draw, and a camera-inversion/half-float residual bound proving the existing tile earlyout. Unknown/stale passes, unproven residuals and force-mode/custom writers (`r_motionvectors >= 2`) retain the conservative reference path; no delayed GPU readback is used. Zero exposure (zero strength/shutter or hitch suppression) also bypasses those passes. Cheat-only `mat_motion_blur_static_skip` defaults to 1; setting it to 0 retains the reference copy/tile/gather path for screenshot and timing comparisons. Moving frames retain the HDR copy because the sampled scene must remain separate while preserving the bound output-target identity. The default reachable-vector neighbor search is retained: scalar tile bounds alone cannot replace its directional reachability test.

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

For the L4D2 port, require the user's authorized retail installation and SDK
runtime; mount their content in place rather than distributing either install.
The bounded, hash-pinned BSP21/format readers are runtime adaptations, not
native SDK-engine format support. Only the port's DLLs, their game-owned
compiled shaders and cleared mod sources belong in a port payload.
Include `LICENSE` and `thirdpartylegalnotices.txt` with SDK-derived source or
binaries; keep the matching notice aggregation under `game/` and the bundled
source licenses. MinHook1.3.4 includes Tsuda's MinHook and Patkov's HDE64, with
local owner/thread-lifetime hardening; its full bundled terms are reproduced
in both notice aggregations.

`.gitignore` excludes local gameinfo, runtime state, retail-format payloads and
local reference/decompilation trees, but ignore rules are not a license grant
or a substitute for reviewing tracked files. No retail executable/module,
VPK, model, texture or audio is added by the port's tracked-file delta from
the upstream SDK. The inherited SDK map/material examples are not L4D2
payload. Retail startup/default-binding texts are local installation-derived
inputs, not tracked source or redistributable payload.

The four `terror_{polygon_button,radial_menu}.{cpp,h}` files retain Turtle
Rock2006 copyrights and a public Alien Swarm SDK widget foundation, compared
against [the clean public SDK import](https://github.com/ReactiveDrop/reactivedrop_public_src/tree/9231d40422194dfecca8b698698df4af7e0d99f1/src/game/client/swarm/vgui).
Polygon hotspot parsing/member structure and radial directions/cursor/bounds/
fade/resource interfaces establish that ancestry. L4D2 numeric behavior and
Frame/viewport/input ownership are reconstructed, not ASW HUD/camera code.
Per the project decision these SDK-derived widgets follow the root SDK license
and preserve their original copyrights; the mirror's README is not a separate
Turtle Rock grant. See `L4D2_BASELINE.md` and plan section9 for the provenance
boundary. Do not publish uncleared reference code, decompiled listings or
extracted/converted retail content, and do not archive the deployed game
directory as a release.

## Additional Resources

- [Valve Developer Wiki](https://developer.valvesoftware.com/wiki/Source_SDK_2013)

## License

The SDK is licensed to users on a non-commercial basis under the [SOURCE 1 SDK LICENSE](LICENSE), which is contained in the [LICENSE](LICENSE) file in the root of the repository.

For more information, see [Distributing your Mod](#markdown-header-distributing-your-mod).
