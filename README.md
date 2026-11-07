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
This will generate the Visual Studio project `everything.sln` which will be used to build your mod.

Then, on the menu bar, go to `Build > Build Solution`, and wait for everything to build.

You can then select the `Client (Mod Name)` project you wish to run, right click and select `Set as Startup Project` and hit the big green `> Local Windows Debugger` button on the tool bar in order to launch your mod.

The default launch options should be already filled in for the `Release` configuration.

### L4D2 in-place deployment (Windows)

The L4D2 port runs from `game/mod_l4d2` with the installed Source SDK 2013
Multiplayer **x64** executable, `hl2_win64.exe` (`srcds_win64.exe` for dedicated
hosting). Original-game tests run from the real Left 4 Dead 2 Steam install.
Do not create staged game roots, mirrors, junction trees or bulk runtime-asset
copies. Startup files are provisioned locally from the user's retail install.

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
its reusable command files are in `C:/l4d2_work/build/` (`build95_*.bat`, whose
historical name does not select an older source revision). Build roles
separately and serially: L4D2 client, L4D2 server, both stock HL2MP regression
roles, then the game shader DLL. Stock HL2MP outputs are not L4D2 payload.

Use VS18 Community at `C:/Program Files/Microsoft Visual Studio/18/Community`,
MSVC `14.44.35207` and Windows SDK `10.0.26100.0`. VS18 registers the MSBuild
toolset as `v145`, but both `vcvars64.bat -vcvars_ver=14.44` and
`VCToolsVersion=14.44.35207` select the required MSVC14.44 compiler; do not
substitute compiler14.51. The current `py -3` is Python3.15.

Generate from `src`:

```bat
devtools\bin\vpc.exe /l4d2 /define:SOURCESDK +game +game_shader_l4d2 /mksln l4d2.sln
devtools\bin\vpc.exe /hl2mp /define:SOURCESDK +gamedlls /mksln hl2mp_regression.sln
```

Initialize the compiler environment once, then run separate role invocations
from `src` (with the VS18 MSBuild directory on `PATH`):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
set CL=/MP2
MSBuild.exe l4d2.sln /t:client_win64_l4d2 /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=true /m:2
MSBuild.exe l4d2.sln /t:server_win64_l4d2 /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=true /m:2
MSBuild.exe hl2mp_regression.sln /t:client_win64_hl2mp /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=true /m:2
MSBuild.exe hl2mp_regression.sln /t:server_win64_hl2mp /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=true /m:2
MSBuild.exe l4d2.sln /t:game_shader_l4d2_win64_l4d2 /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=true /p:SelectedFiles=skip_custombuild /p:L4D2ShaderGameDir=C:\Users\Administrator\Documents\repo\source-sdk-l4d2\game\mod_l4d2 /m:2
```

Normal post-build publishing copies DLL/PDB pairs into the mod; do not pass
the historical `PostBuildEventUseInBuild=false` override. For deliberate
artifact deployment, take the pairs directly from
`src/game/client/Release_mod_l4d2`, `src/game/server/Release_mod_l4d2` and
`src/materialsystem/stdshaders/Release_dx9_mod_l4d2`, rather than trusting an
already-published mod-bin directory that another setup may have overwritten.
Arrange them as `dlls/` beside `vcs/` containing the committed 45 catalog VCS,
then use `prepare_deployment.py --artifact-root` as described above.
An artifact folder is not a second game root.

**Never Clean or Rebuild the shader project.** Normal shader-DLL builds use
`SelectedFiles=skip_custombuild` and retain the committed `.inc`/45 VCS inputs.
Only a shader-source/catalog change justifies explicit full regeneration with
`buildl4d2shaders.bat` / `build_l4d2_ps20.py` (or the local
`build95_shader_full.bat`); these compile port sources, never original VCS.
That path is separate from the normal shader-DLL build and must preserve the
catalog-defined 45-file closure.

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
./mod_tf
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
