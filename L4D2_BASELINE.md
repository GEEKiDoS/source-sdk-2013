# L4D2 port baseline manifest

Pinned original/target identities for original-versus-port comparisons. Current builds and runs use the checkout and installed Steam roots below, in place. Temporary output belongs under `C:/l4d2_scratch`; reusable external tools stay under `C:/l4d2_work`. Historical E:/F:/I: staging/capture paths in `L4D2_LEDGER.md` are past evidence, not current deployment instructions. Do not create another manifest, receipt or source-snapshot system.

## Original (oracle)

Steam app 550, build ID `23990068`, depots 551 (manifest `314878501352141828`) and 552 (manifest `8555994540167139077`). `left4dead2/steam.inf`: patch `2.2.4.3`; in-game `status` reports `2.2.4.3 10097`. Steam UI language `schinese`; the engine adds `*_schinese` search paths that do not exist on disk, so content resolves to the English/base files.

| Module | Machine | Image base | PE timestamp | SHA-256 |
|---|---|---|---|---|
| `left4dead2.exe` | 0x14C | 0x400000 | 1729294129 | `c1007187e05ed681173b1c44c043980edf72dfcb2183ad90ec8e6d874d1b14e3` |
| `bin/engine.dll` | 0x14C | 0x10000000 | 1782845142 | `8a6d468059394bf4cfce32f11dcba12ef45de88da1373084d5a2916d0bf7ba86` |
| `bin/filesystem_stdio.dll` | 0x14C | 0x10000000 | 1782844838 | `d0296dda2b65d86cfd7b63b970b2c60ddb40b459b022afc2a70364e3234afdc9` |
| `bin/materialsystem.dll` | 0x14C | 0x10000000 | 1782844863 | `09fd324cd977d5d4e2a216ee831c07ff8b882efd024137c27de4155fbdfcf0ba` |
| `bin/datacache.dll` | 0x14C | 0x10000000 | 1782844845 | `520aa669f89d3177c50098542a6df063cc168c4125cafc654f6b78dbcf25a4b9` |
| `bin/studiorender.dll` | 0x14C | 0x10000000 | 1782844886 | `3f5f5b0f539e8ad22bcfc4381be41571257c0c29e8061057682f9b8525ca7b85` |
| `bin/vscript.dll` | 0x14C | 0x10000000 | 1782844894 | `1d603257c0fd63561895495e2d119f501632f12e1ca74c22240eb7abfdef7852` |
| `bin/vphysics.dll` | 0x14C | 0x10000000 | 1782844872 | `45aa54cd8907b62ba1e7798ed3b3a27575ba761b500968338dcdfbf3ee93327d` |
| `bin/shaderapidx9.dll` | 0x14C | 0x10000000 | 1782844938 | `0db402cc58f86aa4318d5cdb59c8f081a77374448e5d0214a3618910568c3aa6` |
| `bin/stdshader_dx9.dll` | 0x14C | 0x10000000 | 1782844930 | `6a3915991277cec448bfdf8ed9dc7cc299662af09db220e2039e4df2d964d7ee` |
| `bin/soundemittersystem.dll` | 0x14C | 0x10000000 | 1782844904 | `5e55d6bfaca222253c875ad6d8c3fa6bb928227aa748dd05c5f1ea39d6d65222` |
| `bin/scenefilecache.dll` | 0x14C | 0x10000000 | 1782844928 | `6c745e0bc630abde8a8e1987763408519a9139485a5b9dab38fefcbc1e1bbaec` |
| `bin/inputsystem.dll` | 0x14C | 0x10000000 | 1782845009 | `f8a6284832659402ed2a627de9064b273cc529eec983616e32c31e002e2f85a3` |
| `bin/tier0.dll` | 0x14C | 0x10000000 | 1782844810 | `63d4bc489dfd43045d8b2cc8feada92736736cdcae20c3de0ce1e491c1358d9c` |
| `bin/vstdlib.dll` | 0x14C | 0x10000000 | 1782844827 | `37d0c98ddfe7fa199d741dfc24526eb435f71f66d2d0e3d0500c4c1be33cb6c5` |
| `bin/launcher.dll` | 0x14C | 0x10000000 | 1782845000 | `ec74b61e127c0f942a7561aad09ed596b1cd85513827f6a2a50a1609a95333cf` |
| `left4dead2/bin/client.dll` | 0x14C | 0x10000000 | 1782845135 | `c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f` |
| `left4dead2/bin/server.dll` | 0x14C | 0x10000000 | 1782845011 | `a93ec916a8f26fbea85688c321d2ba8dd346d435fab09a0b926dd950c2e1ea12` |
| `left4dead2/bin/matchmaking.dll` | 0x14C | 0x10000000 | 1782845006 | `b405c7a2b786775e1940eccebcd6756335ceb20b583b43a05df7a457f9fe0e61` |
| `left4dead2/bin/matchmaking_ds.dll` | 0x14C | 0x10000000 | 1782844989 | `0e8586798f54d290143394469812d5af44fd8805cbd3119dcfd7d8f01f992e55` |

Content roots (VPK directory SHA-256 / file count / total VPK bytes):

| Root | `pak01_dir.vpk` SHA-256 | VPK files | VPK bytes |
|---|---|---|---|
| `update` | `47f31ef57a94f88463da8b2132a55cfed14bae127f35e4cfb68948e840ed8b52` | 16 | 683,889,344 |
| `left4dead2_dlc3` | `97fd476159c8cc9b12d41ea132e7a0239c6975285639eea5f1ae3b330b8d5f39` | 10 | 907,353,361 |
| `left4dead2_dlc2` | `013b4e5331af837a39c03d1164b9e59658f4a29233d37a8488edebf4db14dbff` | 6 | 588,117,366 |
| `left4dead2_dlc1` | `e24b54158e39b292bccab1b38f430f667dd1fca53846963876d45509dd7209f6` | 8 | 262,678,891 |
| `left4dead2` | `51cc2957bbc5fd545357fd6b8da0fd5c387243756cd6f82e067d5e256e48fcdd` | 42 | 2,635,404,607 |

`left4dead2/gameinfo.txt` SHA-256 `e1d1bae00e7de0d3ec6fd2b335ec5377be0ef1b94a17f6a2df6d2496403ee887`; `left4dead2/steam.inf` `d43eb3363ecba1a3c6aa552d19bb6984b89cea170ded9e881733406f173f1f4f`.

### Original installation and comparison role

The current authorized original install is
`C:/Program Files (x86)/Steam/steamapps/common/Left 4 Dead 2`.
Run the original `left4dead2.exe` there for oracle comparisons. Do not recreate
the historical `I:/l4d2_stage` hardlink/copy tree, deny-write ACLs, loose-content
views or staged client roots. No original executable, module, VPK, media, model
or texture is part of the port payload.

The port mounts the real update → DLC3 → DLC2 → DLC1 → base VPK/loose pairs,
then SDK `hl2` and `platform` fallbacks, then the original platform directory
for missing localization. The template uses read-only `game`/`platform` search
path roles for these sources and keeps the mod's write/download paths local.
Original binaries and shaders are not selected as port modules or copied into
the mod. Original-content access is not native SDK support for every format:
the server owns a bounded hash-pinned BSP21/VPK reader adaptation; other
game-owned format adaptations and engine limitations remain in the ledger.

`prepare_deployment.py` copies `left4dead2/cfg/valve.rc` and
`cfg/config_default.cfg` from that user's installation into ignored mod-local
files. The installed original has no PC `scripts/kb_def.lst`, so deployment
projects the original default cfg's bind pairs, in order, into the SDK list
format. These three retail-derived startup inputs are local deployment data,
not tracked/distributed mod sources. Missing original files or bind commands
fail deployment; no HL2MP defaults or invented bindings are substituted.
Existing user `config.cfg`/`autoexec.cfg` are never copied or overwritten.

## Target runtime

Source SDK Base 2013 Multiplayer, app 243750, build ID `17413218`.

| Module | Machine | Image base | PE timestamp | SHA-256 |
|---|---|---|---|---|
| `hl2_win64.exe` | 0x8664 | 0x140000000 | 1739853523 | `4c695bde95ccc1fa53b7741b31a2d6a398fe1a4d0d0757981a87a44a3ca18387` |
| `srcds_win64.exe` | 0x8664 | 0x140000000 | 1739853522 | `673fd37872f47187b2218ed273359021bb115bdc7c8158291654cfa873844080` |
| `bin/x64/engine.dll` | 0x8664 | 0x180000000 | 1739853765 | `adba182f610ae3ccac3e42430e23c3eefc3f5f3b6b36d1b7b09225ec6016c5a6` |
| `bin/x64/dedicated.dll` | 0x8664 | 0x180000000 | 1739853711 | `35679c6738164f0058598ae9293f6967cc7f2ef1d2b0e278c781343758effe39` |
| `bin/x64/filesystem_stdio.dll` | 0x8664 | 0x180000000 | 1739853580 | `a8700336a3fd448515798d26d3df2a0fda351e569887b8af2ed30cc97331decb` |
| `bin/x64/materialsystem.dll` | 0x8664 | 0x180000000 | 1739853661 | `dd8059fa55f22e8088a6b9ec33adc7425ea857f916b3264b8055159a4bc8d4f2` |
| `bin/x64/datacache.dll` | 0x8664 | 0x180000000 | 1739853584 | `49f05b729b393be5e7bda7c1ac98228674c58cd8b1b017e7e6f956e0a6890628` |
| `bin/x64/studiorender.dll` | 0x8664 | 0x180000000 | 1739853639 | `4765b37069e67127a76687364da66e6bd29b4305db1cb7ae0be27beaed003cc6` |
| `bin/x64/vscript.dll` | 0x8664 | 0x180000000 | 1739853549 | `a2d3ff0050016670af2886a7052fbdb41600f3e83782b1d042060a4b6e682192` |
| `bin/x64/vphysics.dll` | 0x8664 | 0x180000000 | 1739853555 | `8cb09dc7a4b711f6bf64ccdac12c7b16e9dcf7027dc25c74a57741fcf54e6f9c` |
| `bin/x64/shaderapidx9.dll` | 0x8664 | 0x180000000 | 1739853649 | `eadbea8cfe02459f9d1d979d92fea6f16f6b27f317241a15d8358a4ae8e33e23` |
| `bin/x64/stdshader_dx9.dll` | 0x8664 | 0x180000000 | 1739853591 | `b52d5194677df8f953932ce4e6ab8581363ac3c4760699f55c8d6ca3c1d69d66` |
| `bin/x64/launcher.dll` | 0x8664 | 0x180000000 | 1739853594 | `0c55be03653eab6cfee4384bacaa9914942bbbf4866619d5aa246d1891a3a3eb` |
| `bin/x64/tier0.dll` | 0x8664 | 0x180000000 | 1739853513 | `089c71ffffebf0bc9b72e7586ed57162028d781fe08bb129661c6a82fed92010` |
| `bin/x64/vstdlib.dll` | 0x8664 | 0x180000000 | 1739853525 | `7f21c5572e520310f42d3329d9bde6b995cdb552d9cbb87728e5deeee765253b` |

Rendering comparisons must establish whether a D3D9 wrapper is active from the actual run. Historical wrapper/cache filenames are not a statement that this machine currently loads one; the pinned SDK module identities alone do not prove the renderer path.

## Current pinned x64 build configuration

Checkout: `C:/Users/Administrator/Documents/repo/source-sdk-l4d2`.
The session-local `local://BuildRecipe.md` and reusable
`C:/l4d2_work/build/build95_*.bat` describe the current machine pipeline;
the script names are historical, not a request to revert current sources.
`README.md` contains the reproducible commands.

- Visual Studio 18 Community / MSBuild:
  `C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe`.
- Compiler: `vcvars64.bat -vcvars_ver=14.44`,
  `VCToolsVersion=14.44.35207`; Windows SDK `10.0.26100.0`.
  VS18's registered `PlatformToolset=v145` label is required on this host,
  but the compiler must remain MSVC 14.44, not 14.51. Use `CL=/MP2`, `/m:2`.
- Python: current `py -3` is 3.15; VPC:
  `src/devtools/bin/vpc.exe` (Mar 8 2025).
- Generate `l4d2.sln` with
  `vpc.exe /l4d2 /define:SOURCESDK +game +game_shader_l4d2 /mksln l4d2.sln`;
  generate `hl2mp_regression.sln` with
  `/hl2mp /define:SOURCESDK +gamedlls /mksln hl2mp_regression.sln`.
- Build client/server as separate serialized invocations, then both stock
  HL2MP regression roles, then `game_shader_l4d2_win64_l4d2`.
  Use `Release`, `win64`, normal `PostBuildEventUseInBuild=true`.
  HL2MP is a regression role, not the L4D2 runtime or deployment payload.
- Current source outputs:
  `src/game/client/Release_mod_l4d2/{client.dll,client.pdb}`,
  `src/game/server/Release_mod_l4d2/{server.dll,server.pdb}`, and
  `src/materialsystem/stdshaders/Release_dx9_mod_l4d2/{game_shader_generic_l4d2.dll,game_shader_generic_l4d2.pdb}`.
- **Never Clean/Rebuild the shader project.** The normal shader-DLL build
  passes `SelectedFiles=skip_custombuild` and
  `L4D2ShaderGameDir=<checkout>/game/mod_l4d2`, retaining the committed
  generated `.inc` inputs and 43 catalog-defined VCS in
  `game/mod_l4d2/shaders/fxc`. Only actual source/catalog changes justify
  explicit regeneration via `buildl4d2shaders.bat` /
  `build_l4d2_ps20.py`. Those tools compile port sources, not retail VCS.
- Shared SDK seams stay under their L4D2 guards where applicable; the
  game-side frame-animation decoder in `src/public/bone_setup.cpp` and
  `studio.*` is shared. Current build/runtime acceptance must be taken from
  the ledger, not inferred from this input/configuration description.

## Current in-place L4D2 deployment and launch

Target install:
`C:/Program Files (x86)/Steam/steamapps/common/Source SDK Base 2013 Multiplayer`.
The target hosts are its `hl2_win64.exe` and `srcds_win64.exe`. Both use
`-game C:/Users/Administrator/Documents/repo/source-sdk-l4d2/game/mod_l4d2`;
the working directory is the real SDK install. Oracle runs use the real
original install, not these target hosts. No mirrored game roots/junctions
or copied runtime assets are required.

From the checkout, `py -3 scripts/l4d2/prepare_deployment.py` uses those
installed roots by default (explicit `--original-install` / `--sdk-install`
or the corresponding `L4D2_*` environment variables may select another
authorized install). It renders tracked `gameinfo_template.txt` into ignored
`gameinfo.txt` and provisions the three local startup inputs above.
`--gameinfo-only` does this without DLL/PDB/VCS requirements.

Normal post-build publication copies module/PDB pairs into
`game/mod_l4d2/bin/x64`. When deploying an artifact explicitly, take the three
pairs directly from the current Release output directories, plus the committed
43 VCS, into a temporary `dlls/` + `vcs/` artifact folder under
`C:/l4d2_scratch`, then pass `--artifact-root`. This folder is not a game root.
Do not assume files already in mod-bin are the desired source outputs.
Do not ship a mixed module/PDB cohort or original compiled shader bytes.

`--check-only` writes nothing and launches/loads nothing. It parses gameinfo,
checks ordinary file/search-path/catalog presence, and compares the three
startup inputs byte-for-byte with the installed-source projection. A complete
current layout has five original layers, nine VPK indexes, 22 search paths,
three startup files, three DLLs, three PDBs and 43 VCS. It does not hash module
outputs, prove ABI compatibility or close a runtime/fidelity gate.

`verify_inputs.py` supplies only the shared, side-effect-free original-layer
order and `require` check used by the deployment operator. Main retired
`verify_host.ps1`, `guard_originals.py`, `freeze_source.py` and the standalone
build55 manifest profiles. Their legacy host/bootstrap, staged-content,
receipt and snapshot workflows are not current build/deployment requirements;
do not recreate them.

The tracked VMFs `mapsrc/m2_contracts.vmf` and `m4_survivor_lab.vmf` are authored
test-map sources, not runtime retail maps. Their historical compiled-map and
multi-root M0/M2 run records remain in the ledger; they do not prescribe today's
in-place roots or establish a fresh current build/test result.

## Source provenance and distribution boundary

The upstream SDK input is the licensed Valve tree (comparison base
`be57e3d01ac7d7aa6a2cacbfef187c0b46616db0`). Its supplied tools/libraries and
non-L4D2 sample content are not retail L4D2 payload. The port's dependency-path
delta introduces only `bsp21/third_party/minhook-1.3.4`: Tsuda's MinHook plus
Patkov's bundled HDE64, with local thread/owner-lifetime hardening. Both full
HDE notices remain in its bundled license and both notice aggregations.
Existing Protocol Buffers, Snappy, LZMA, IJG JPEG, Info-ZIP and other SDK notices
are retained; libspng, zlib, bundled Google Test/PngSuite source-test notices
and missing Snappy 2008/2011 attributions are included in both aggregations.
Source distribution keeps component-local licenses/headers as well.

The Turtle Rock 2006 headers in
`terror_{polygon_button,radial_menu}.{cpp,h}` (and split `terror_radial_button.inc`)
trace to the public Alien Swarm
SDK widget foundation, not an uncleared leaked tree: compare
[ReactiveDrop's initial clean SDK import](https://github.com/ReactiveDrop/reactivedrop_public_src/tree/9231d40422194dfecca8b698698df4af7e0d99f1/src/game/client/swarm/vgui).
The constructor/hotspot parsing and polygon member layout match; the radial
nine-direction enum, cursor/bounds/fade/resource fields and method families
match. Current L4D2 numeric behavior and Frame/viewport/input ownership are
reconstructed separately and are not the ASW HUD/camera path. Preserve the
Turtle Rock copyrights. Main accepts this public SDK-derived foundation under
the project's SDK-license distribution conditions; the mirror's README is
not a separate Turtle Rock license grant and describes ASW's exact license
wording as unspecified.

No added tracked retail executable/module, VPK/BSP/NAV, model/texture/audio or
whole reference-source tree was found in the port delta. Retail startup texts
are no longer tracked; deployment derives/copies them only from the user's
install. `.gitignore` excludes local startup/runtime data, retail-content
directories and local reference/decompilation trees. These exclusions do not
clear newly imported code or authorize publishing a working game directory.
Distribute only cleared mod sources/build outputs with `LICENSE` and the
notice aggregation; never archive the deployed directory with its local data.
This is not a formal clean-room or blanket rights-clearance claim.

## Native-container disposition (plan section 3.5)

Main selected migration, not an STL exception, for all nine BSP21 vector
declarations and the vector-reference helper signature. The reader now uses
`CUtlVector` throughout; no behavior or admission-policy substitution is
intended. Build/runtime acceptance belongs in the ledger, not this source
description.

- `adapter.cpp`: `g_PropTint` → `CUtlVector<PropTint>`, keeping prop-index order,
  resize/value-initialization semantics and per-map clear/capacity lifetime.
- `adapter.cpp`: `g_PackedVersions` → `CUtlVector<PackedVersion>`, preserving
  append order, linear first-match lookup, mutex scope and order-preserving
  `Remove(index)` (not `FastRemove`, a map or a case-folding dictionary).
- `adapter.cpp`: local `canonical` worldlight bytes →
  `CUtlVector<unsigned char>` sized to the exact logical byte count; retain the
  synchronous native-consumer lifetime and zero-initialization contract.
- `bsp21_format.h`: `Bsp21Layout::gameLumps` →
  `CUtlVector<Bsp21GameLump>`, preserving serialized directory order, deep-copy
  assignment, resize/count semantics and the existing duplicate/offset checks.
- `preflight.cpp`: `packed` in `Bsp21ReadPayload`, `data` in both worldlight
  validation scopes and both game-lump validation scopes, plus the
  `Bsp21ReadLump` reference parameter → `CUtlVector<unsigned char>`, retaining
  exact logical sizes, contiguous storage, initialization and failure behavior.
  The local `Bsp21Resize` operation reserves the exact requested capacity before
  `SetCountNonDestructively`, then zeros only a newly appended POD tail,
  preserving existing elements and capacity on shrink/clear. `Bsp21Layout`
  retains deep copy/assignment with exact reservation and a self-assignment
  guard. Directory/cache removal never reorders entries.

There is no new port-owned STL string/map/set/stream layer. Shared SDK
`std::string` datamap/NetProp handling, Protocol Buffers/Snappy string APIs,
SDK VScript `std::function`, SDK `CUtlVector`'s internal algorithm use and the
unchanged SDK launcher are existing interfaces/implementations, not new port
containers; retain them under section 3.5's existing/external-API exception.
TF-only STL is not a new L4D2 path. The removed, unselected prototype's
`g_TintObserved` vector is no longer a migration item.

