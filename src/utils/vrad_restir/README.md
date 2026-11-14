# VRAD ReSTIR

VRAD ReSTIR is the Win64 Vulkan-compute light baker for Source BSP files. It is a separate tool from the legacy `vrad` implementation and is invoked through `vrad_restir.exe`. The launcher loads the adjacent `vrad_restir_dll.dll`; `-both` preserves authored LDR and HDR output.

Paired bakes resolve both effective scenes before GPU transport. Only exact agreement of geometry/occlusion, decoded material textures/emission, lights, styles and receiver sampling domains admits one shared solution. HDR entity scales/overrides, eight-component light values, `$hdrcolorscale` and mode-filtered RAD directives therefore still produce separate solutions whenever they change effective inputs. Equal scenes log `skipping second GPU transport` and reuse the solved full-source/receiver lighting, ambient gather, prop meshes and detail lighting; each mode still uses its own native encoding, VHV names/flags, highres records and CRC identities. Shared PPL/detail payloads retain HDR-final semantics. Both modes remain in the owned paired transaction until success; genuine ordinary `-ldr` remains single-mode.

Behavioral paired-bake acceptance (real baker, isolated fixtures; no shipping BSP writes):

```powershell
python utils/vrad_restir/tests/paired_bake_test.py --fixture-dir <temporary-fixtures> --tool-game <temporary-tool-game>
```

This exercises equivalent inputs against separate ordinary mode controls, HDR sentinel/default/zero/scale boundaries, eight-component and mode-filtered RAD emission/occlusion, HDR-scaled materials, highres native identities, and paired failure rollback.

Equivalence is based on resolved lighting, not different key text: for example, two RAD values below the existing texlight admission threshold can legitimately share a solution. An occlusion regression fixture must enable its static props' shadows; `noshadow` cannot change an already non-shadow-casting prop.

## Prerequisites

- Visual Studio 2022 and the repository VPC toolchain.
- A Vulkan 1.2-capable loader and GPU. The in-tree SDK subset is under `thirdparty/vulkan_sdk` and supplies the C headers, `vulkan-1.lib`, `glslc.exe`, and `spirv-val.exe`. The Vulkan runtime DLL is supplied by the graphics driver and is not vendored.
- Optional: Intel Open Image Denoise 2.5.1 runtime beside `vrad_restir.exe` or on `PATH`. From `oidn-2.5.1.x64.windows.zip` (`bin\`) copy `OpenImageDenoise.dll`, `OpenImageDenoise_core.dll`, `tbb12.dll`, `tbbbind_2_5.dll` and the device modules you want: `OpenImageDenoise_device_cpu.dll` (always usable) and `OpenImageDenoise_device_cuda.dll` for NVIDIA (`_hip.dll` for AMD, `_sycl.dll` + `sycl9.dll`/`ur_*.dll` for Intel Arc). The C API is declared locally in `restir_denoiser.cpp` and every entry point is resolved at run time, so no OIDN headers or import library are needed to build. Without the DLL the baker reports `denoiser=bilateral-fallback` and uses its built-in edge-aware filter; with it, the summary line shows `denoiser=oidn device=<name>` (RTX 4070 SUPER via CUDA: ~0.8 s for 920k luxels; CPU: ~5 s). OIDN is Apache-2.0 licensed and is not included in this repository (its license text ships beside the DLLs as `OpenImageDenoise_LICENSE.txt`).
- Running the tool needs the Source tool runtime next to it: `vrad_restir.exe` loads `filesystem_stdio.dll`, `tier0.dll`, and `vstdlib.dll` through the normal `bin\x64` layout; the fixture setup in this repository puts `filesystem_stdio.dll` in `game\bin\x64` and the SDK Base 2013 `bin\x64` directory on `PATH`.

The baker has no CPU path-tracing fallback. If hardware ray queries are unavailable, it selects the GPU compute-BVH backend. CPU execution is permitted only for the optional OIDN denoiser.

## Building

Generate the Win64 projects from `src`:

```bat
devtools\bin\vpc.exe /2022 /win64 /episodic /define:SOURCESDK +vrad_restir /mksln vrad_restir.sln
```

The generated projects are `VRAD ReSTIR (DLL)` and `VRAD ReSTIR (Launcher)`. Outputs are placed in `game\bin\x64` as `vrad_restir_dll.dll` and `vrad_restir.exe`; generated SPIR-V is copied to `game\bin\x64\vrad_restir_shaders`.

Regenerate and validate shader variants after changing a `.comp` file:

```bat
utils\vrad_restir\shaders\buildshaders.bat
for %f in (game\bin\x64\vrad_restir_shaders\*.spv) do thirdparty\vulkan_sdk\Bin\spirv-val.exe --target-env vulkan1.2 "%f"
```

`buildshaders.bat` invokes the vendored compiler with `--target-env=vulkan1.2 -O` for both `-DRESTIR_HW_RAYQUERY=1` and `-DRESTIR_HW_RAYQUERY=0` variants.

## Runtime selection

The default backend is `hardware-rt` when the selected Vulkan adapter exposes the required ray-query and acceleration-structure extensions. Otherwise, or when `-restir_force_compute_bvh` is supplied, the backend is `compute-bvh`. Both backends execute the lighting workloads as Vulkan compute passes.

The default options are:

| Option | Default | Meaning |
|---|---:|---|
| `-ldr`, `-hdr`, `-both` | `-ldr` | Select the output lighting mode; `-both` is handled as two launcher passes. |
| `-fast`, `-final` | neither | Quality presets, mutually exclusive. `-fast`: 32 iterations, 4 candidates, 2 bounces, fast denoiser. `-final`: 512 iterations, 16 candidates, 6 bounces, high denoiser. Any explicit `-restir_iterations`, `-restir_candidates`, `-restir_maxbounces` or `-restir_denoiser_quality` overrides the preset regardless of argument order. |
| `-restir_iterations N` | `128` | Transport iterations; `1..65535`. Bounce light is fed back from the irradiance resolved by earlier iterations, so this also bounds the effective bounce depth; the final estimate is the mean of the iterations after a short burn-in (a quarter of them, at most 8). |
| `-restir_candidates N` | `8` | Samples per lightmap sample per iteration, `1..4096`: that many hemisphere rays (sky ambient where a ray reaches the sky, bounce where it lands on a lightmapped face) and that many samples over the `emit_surface` lights. The sun adds one sample. Point and spot lights take none: they are evaluated exactly, once per bake. |
| `-restir_spatial_radius N` | ignored | Accepted for existing compile configurations. Lighting samples are not reused between luxels. |
| `-restir_maxbounces N` | `4` | Maximum path bounces; `0` disables indirect light; `0..128`. |
| `-restir_seed N` | `1` | Deterministic sequence seed; `0..2147483647`. Same inputs and seed give byte-identical lighting. |
| `-restir_gpu N` | first discrete GPU | Vulkan adapter index; `0..255`. |
| `-restir_vram_budget MB` | queried budget | Optional per-process heap cap. The baker keeps geometry/BVH, the compact nearest-sample, exact-direct and previous-irradiance state resident. A scene whose transport state fits runs every iteration on resident GPU buffers with no host round trip. Otherwise it streams contiguous face-aligned sample/luxel/origin/accumulation/output/valid buffers through the 64 MiB staging ring: each iteration processes every page and refreshes previous irradiance from paged accumulation, and reconstruction releases transport pages before the final-lightmap/service phase. Pages never split a face, and a sample reads only its own records plus the resident tables, so paging cannot change the result. `-v` logs resident bytes, page capacity/count and heap peaks. |
| `-restir_denoiser oidn\|none` | `oidn` | Select OIDN (bilateral fallback when the DLL is absent) or disable denoising. |
| `-restir_denoiser_quality fast\|balanced\|high` | `balanced` | OIDN quality setting. |
| `-restir_denoiser_device default\|cpu\|gpu` | `default` | OIDN device preference. |
| `-restir_probe x y z nx ny nz` | off | Diagnostic: after the bake, print the GPU light table and the direct/indirect light arriving at the given world point and normal per style. |
| `-restir_lightmapscale F` | `1.0` | Multiplier on brush-face luxel size, `0.0625..1.0`; `0.5` doubles density per axis. See "Lightmap density" below. |
| `-restir_emissivescale F` | `1.0` | Material-emission multiplier, `0..1000`; `0` disables material emitters without affecting `.rad` texlights. |
| `-restir_ambientgrid N` | `32` | `-restir_shadowmaps` only: ambient probe spacing in units, `8..512`. See "Dense ambient probes". |
| `-restir_ambientgrid_reach N` | `512` | `-restir_shadowmaps` only: a probe brick exists within this distance of lit world/static-prop geometry, `0..65536`; `0` allocates every brick. |
| `-restir_notexturealbedo` | albedo on | Disable per-texel bounce albedo. By default bounce light picks up each material's `$basetexture` per texel instead of VRAD's single reflectivity per material: the texture is linearized (2.2) and scaled so its average equals the material's reflectivity, so total bounced energy matches VRAD and only its spatial distribution changes (a dark stripe on a wall bounces less than its light neighbour). Applies to lightmap bounces, leaf ambient cubes and static/detail prop gathers; materials whose base texture cannot be loaded use their reflectivity. `-restir_texturealbedo` is accepted as the explicit default. |
| `-StaticPropLighting` | off | Bake static-prop vertex and texel lighting. |
| `-TextureShadows` | off | Evaluate alpha coverage for shadows. Every static-prop mesh whose material has `$alphatest`/`$translucent` casts its alpha cutout (chain-link fences, foliage); stock VRAD restricts this to models flagged `$casttextureshadows` or listed by `forcetextureshadow`, which this baker accepts and ignores. |
| `-smooth N` | `45` degrees | Phong smoothing threshold. |
| `-lights FILE` | none | Additional `.rad` light file. |

Legacy VRAD switches used by existing compile configurations are accepted and reported as ignored when they do not affect this baker (`-fast` and `-final` are honoured as presets instead). Unknown switches are errors. `-StaticPropPolys` is accepted as a no-op because ReSTIR always uses polygon-precision static-prop geometry.

### Lighting estimator

Every (sample, style) record is estimated on its own; nothing is resampled or shared between luxels or iterations, so the noise that remains is uncorrelated from luxel to luxel.

- Point and spot lights are delta sources. `restir_direct.comp` evaluates every one of them for every sample with one shadow ray, once per bake, as VRAD does. They contribute no noise.
- `restir_transport.comp` runs once per iteration. Each hemisphere ray is cosine-distributed and resolves both hemisphere terms, which have disjoint support: sky ambient if it leaves through the sky, otherwise bounce light from the lightmapped face it hits. The ray directions of a sample follow one base-2 (0,2)-sequence across all its iterations under a per-sample digital shift, so they stratify the hemisphere instead of clumping. Open sky on an unoccluded surface is therefore exact.
- The sun takes one sample of its cone per iteration (exact when `SunSpreadAngle` is 0). `emit_surface` lights take `-restir_candidates` area samples with a uniform light choice.
- Bounce light reads the irradiance already resolved at the hit sample: exact direct plus the running mean of the sampled terms.

Measured on `c1m1_hotel` (RTX 4070 SUPER, `-both -StaticPropLighting -TextureShadows -final -restir_iterations 128 -restir_shadowmaps -restir_hlight_density 2`, 4.49M luxels, two transport solutions): 356 s without a denoiser, of which 57 s per mode is GPU transport. The earlier reservoir-reuse estimator took 1,799 s for the same command, 780 s per mode in transport. Noise, as the difference between two seeds: median 0.15% per lit luxel, 1.1% at the 90th percentile; a sunlit roof with open sky is exact where the earlier estimator left 29% in blobs about 8 luxels wide. Per-face HDR means against the retail VRAD lighting of the same map (`-hdr -fast`): median ratio 1.07, 63% of faces within 25% (earlier estimator 0.92 and 49%).

Known limit: a luxel in the near field of an emissive mesh with thousands of triangles still shows fireflies, because triangles are chosen by power without regard to distance. On `c1m1_hotel` 99% of the remaining squared noise sits in 0.1% of the luxels, 95% of it on eight faces around (8000, 12900, -970), far outside the playable area and directly under a 22,012-triangle emissive prop.

The remaining noise leaves the denoiser little to do. On the bake above OIDN roughly doubled the error of the largest surfaces (0.23% to 0.49% of RMS brightness) and raised the mismatch at luxels shared by coplanar faces from 2% to 43% at the 99th percentile, because every face is denoised as its own small block. `-restir_denoiser none` is the better choice at these settings.

Measured on `ep2_outland_09` with the earlier reservoir-reuse estimator and not repeated since (RTX 4070 SUPER, 920k luxels, one mode, no prop lighting): `-fast` 9 s, default 14 s, `-final` 34 s. Per-face HDR ratio against VRAD's bake: median 0.99 / 1.01 / 1.01 and 10th percentile 0.71 / 0.83 / 0.88 respectively.

The simplest way to run it is `game\vrad_restir.bat`, which needs nothing copied into the SDK Base folders: it puts the SDK Base 2013 Multiplayer `bin\x64` runtime on `PATH` for the call, writes a tool-readable `gameinfo.txt` under `%TEMP%\vrad_restir_game` from the configured content roots, and forwards every other argument. A bare map name resolves under `mod_episodic\maps`:

```bat
game\vrad_restir.bat -both -StaticPropLighting -TextureShadows -final ep2_outland_09
```

Content roots default to the local Steam library; override with `SDK_MP`, `SDK_SP` and `HL2_DIR` environment variables (or edit the defaults at the top of the script).
For the L4D2 port use `game\vrad_restir_l4d2.bat`. It expands the retail L4D2 layers (`update`, `left4dead2_dlc3`, `left4dead2_dlc2`, `left4dead2_dlc1`, and `left4dead2`) plus the SDK Base 2013 Multiplayer `hl2`/`platform` fallbacks into a private tool-readable gameinfo. The roots default to `L4D2_ROOT=C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2` and `SDK_MP=C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer`; set those environment variables to override them.

Shipped L4D2 BSP v21 maps are accepted directly. The loader decodes v21 lump headers, LZMA payloads (including independently compressed game-lump payloads), 100-byte worldlights, and v8/v9 static-prop records into the ordinary SDK v20 tool representation. Output is written as a standard BSP v20 map: rewritten lighting/worldlights/ambient and ReSTIR data use SDK layouts, and converted static props are emitted as SDK v6 (v8 input) or SDK v10 (v9 input) records. Because those SDK records have no diffuse-modulation field, a v21 bake with static props also writes the `ptnt` game lump (version 1), one RGBA tint entry per prop in `sprp` order. The L4D2 engine adapter consumes `ptnt` on v20 maps and uses the same existing draw hooks as v21; v21 sources keep their original tint path. A v20 source without `ptnt` receives no tint lump.
GPU work is submitted in adaptive, per-stage chunks. Each completed fence wait steers the next chunk toward a roughly 250 ms interval, clamped to the dispatch limit and the resident page's item count, so the compute-BVH backend remains below the Windows TDR threshold on slow GPUs without changing deterministic lighting results. This includes selected-light world-visibility pages; each chunk addresses its page-local service output before readback. Pacifier phases report exact canonicalization, upload, scene/BVH, bake-iteration, denoise, gather, and write work; verbose `-v` additionally prints resident/page heap peaks, page dimensions, and per-submit GPU/queue milliseconds. Bake stage submit/chunk summaries are emitted once after the bake instead of interleaving with the pacifier.

Paging is rebudgeted after source-style resolution compacts the output ranges, so a scene that fits after pruning uses one transport page and runs resident. Cross-page bounce reads use resident sample positions, the resident exact-direct table and a previous-iteration snapshot of the sampled sums refreshed only after every page finishes; a resident page publishes the same snapshot with a GPU copy. Reconstruction preserves the resident BVH for subsequent ambient/static-prop gathers.

Verification of the current estimator on RTX 4070 SUPER, c1m1 density 1 with `-both -StaticPropLighting -TextureShadows -fast -restir_shadowmaps -restir_denoiser none`: a forced-paged `-restir_vram_budget 1180` bake (13 transport pages after style resolution) and the resident bake wrote byte-identical lighting lumps, `.hlight`, `.hprobe` and static-prop VHV members. Two resident bakes with the same seed are byte-identical. The compute-BVH backend matches hardware-rt per face (median ratio 1.000, 1st percentile 0.998).

The following verification predates the current estimator.

Verification on GTX 1050 (2 GB), c1m1 density 1 with `-both -restir_shadowmaps -restir_iterations 2 -restir_candidates 1 -restir_maxbounces 1 -restir_denoiser none`: the unpaged reference completed in 4,523.39 s with one transport page after style resolution; heap-0 peaks were 1,445,454,816 B (LDR) and 1,466,754,016 B (HDR). A forced-paged `-restir_vram_budget 1400` counterpart completed in 3,797.59 s with three pages after style resolution and heap-0 peaks of 1,004,970,944 B (LDR) and 940,166,592 B (HDR). BSP LDR/HDR lighting lumps and the 286,255,488-byte embedded `.hlight` asset were byte-identical (decoded RGBExp32 and hlight pixel max/mean absolute differences 0/0). The complete density-2 `-fast` bake (default fast quality: 32 iterations, 4 candidates, 2 bounces, OIDN fast) also completed on the GTX 1050: wall 13,090.58 s; LDR 6,283.047 s and HDR 6,806.585 s; seven transport pages after style resolution; maximum heap-0 peaks 1,369,162,864 B (LDR) and 1,263,715,808 B (HDR). The validated density-2 assets are `.hlight` 283,490,920 B (LDR) / 566,981,784 B paired, `.hprobe` 105,111,040 B / 210,222,016 B paired, and `rshd` 8,520 B / 16,640 B paired; both modes were written successfully.

```bat
game\vrad_restir_l4d2.bat -both -StaticPropLighting -TextureShadows -fast C:\path\to\c1m1_hotel.bsp
```

The L4D2 wrapper stages the executable, shaders, and SDK `filesystem_stdio.dll` under `%TEMP%` so the SDK Base installation and repository tool directory are not modified. If OIDN DLLs are present beside `game\bin\x64\vrad_restir.exe`, they are staged too and the bake summary reports `denoiser=oidn device=...`; otherwise it reports the bilateral fallback.


Running the executable directly from `game\bin\x64`:

```bat
vrad_restir.exe -both -StaticPropLighting -TextureShadows -game <path to gameinfo dir> <path>\ep2_outland_09.bsp
```

`gameinfo.txt` must be a tool-readable one: the engine-only `|appid_NNN|` search-path mounts are not resolved by the tool file system, so list the content directories and `.vpk` files explicitly (quote paths containing spaces). `tier0.dll`, `vstdlib.dll` and `filesystem_stdio.dll` must be beside the executable or on `PATH`.

## BSP output

The result remains an ordinary Source BSP. The selected mode's face lightmaps, face style/offset arrays, worldlights, per-leaf ambient index/lighting lumps, static-prop pak entries (`sp_N.vhv` / `sp_hdr_N.vhv`), and detail-prop game lump are written using the existing Source contracts. LDR and HDR data coexist after `-both`; a single-mode pass does not replace the opposite mode's lighting data. Output is staged as `<map>.bsp.restir.tmp` and validated before the original BSP is replaced.

Invalid luxels follow VRAD's defaults: a face with no samples at all is written as `(255,0,0)`; a brush luxel that no sample cell reaches is reconstructed from the surrounding samples with VRAD's bounce-radial kernel and excluded from the face average; a luxel nothing reaches is black (`bRed2Black`). Displacement luxels without coverage are black.

Ordinary bakes follow VRAD's switchable-light rule: a face gets at most `MAXLIGHTMAPS` = 4 styles, including style 0. The scene build tests PVS, radius, hard fade, spot cone and cosine; faces with more than four candidate styles are resolved after upload with GPU shadow rays. A remaining ordinary overflow warns and retains the first four styles, as VRAD does.

With `-restir_shadowmaps`, full-source transport retains up to 64 styles for ambient/detail gathers. Native BSP receiver lightmaps retain four slots and omit selected-local direct/styles exclusively used by selected lights. The active `.hlight` v4 world RGB retains v3's selected-local diffuse and Source styles. Receiver styles are admitted first, then nonzero selected-local styles in canonical light-index order; an existing style (including style 0) always fits. If a local cannot fit the four-slot palette, its RGB is not baked on that face; its selected index is recorded for full runtime diffuse/specular with immutable visibility. No light is dropped. Native receiver-style overflow still rejects. Selected sun direct remains runtime-only.

Regression fixtures `source-style-overflow` and `baked-direct-style-overflow` require successful publication with exact per-face unbaked lists and independently checked RGB exclusion; the latter uses styles 0,32,33,34,35. `receiver-style-overflow` remains a native-style rejection with byte-exact rollback. `baked-direct-styles` fits four selected styles without fallback. `paired_bake_test.py` compares output against independent runtime direct equations, validates per-mode fallback summaries and checks unchanged native/VHV policies against ordinary controls.

### Hybrid local visibility and baked direct (`.hlight` v4 / `rshd` v5)

Every selected local light receives an independent deterministic visibility field,
including local-only maps without a selected sun. Historical `sunVisibilityOrigins`
now owns the shared geometric receiver origins for either kind of selected light.
Local visibility is the fraction of unobstructed finite receiver-to-emitter segments:
one central ray at `_shadow_radius 0`, otherwise 32 area-stratified samples on a
receiver-facing disk of the resolved `_shadow_radius`. It excludes radiance, styles,
cone, attenuation, receiver cosine and transport/denoising. The immutable `STATIC_SUN`
occluder mask and existing binary alpha coverage are shared with baked sun visibility;
movable brush entities do not cast permanent local shadows. A brush receiver origin
is the luxel grid point when it lies strictly inside its face, otherwise the nearest
in-face sample cell balance point (VRAD's radial rule for luxels outside the face),
pushed one unit along the face normal; the grid of a thin VBSP split can lie entirely
outside the face except for its middle column, and a point clamped onto a shared edge
would sit on the neighbouring plane. No luxel is blocked for being outside its face.

The sidecar stores rounded R8 visibility in sorted canonical selected-manifest order
(including the manifest sun slot in indices, but never a local entry for that slot).
Each complete face directory contains explicit uniform values or tightly packed dense
`highWidth*highHeight` planes, without RGB gutters. Uniform elision happens only after
tracing. A checksum-covered face/light support bitset declares required entries;
finite-radius distance-to-complete-face-bounds can prove zero support, while unbounded
lights are never omitted. Cone/fade-only rejection is deliberately not attempted.
All offsets are bounded and canonical, sections align to 16 bytes and dense payload
starts align to four bytes. The shared validator rejects incomplete required entries,
old runtime versions, malformed ordering/counts/padding and manifest/payload CRC mismatch.
The signed BSP-pak limit (`0x7fffffff`) is checked explicitly; density is never reduced
silently to make an oversized asset fit.

Static prop visibility is independent of VHV RGB production, including
`STATIC_PROP_NO_PER_VERTEX_LIGHTING` props. Original/recovered receiver positions are
traced with `NO_SELF_SHADOWING` also applied to recovered receivers; unrecoverable solid
receivers are blocked (zero). The exact VHV scatter maps visibility into every LOD and
strip-group hardware vertex. Prop records retain ordinal, MDL checksum, authored pose
identity and mesh vertex-order CRC. Pose identity is FNV-1a-64 (offset
14695981039346656037, prime 1099511628211) over little-endian float32 origin, angles and
lighting-origin triples followed by zero-extended uint32 prop flags. Vertex-order CRC32
covers little-endian uint32 flattened original vertex indices in hardware order:
cumulative original model vertex base + mesh vertexoffset + origMeshVertID.

Only the existing exact whole-scene HDR/LDR equality permits sharing a visibility set;
unequal modes serialize independent fields. The paired owned-BSP transaction and lossless
LZMA pak readback remain unchanged. A checked v1/v4 asset is accepted only as a complete
paired `-restir_shadowmaps` rebake input; none of its old enhanced payload is admitted
to the hybrid runtime or carried into the new asset. Rebuild **all** HW/SW SPIR-V modules
after the shared binding changes, not just `restir_local_visibility.comp`.

Version 3 additionally writes selected-local diffuse into high-resolution RGB only,
using exact runtime attenuation (distance clamped to one, capped denominator), spot
cone/exponent and quintic distance fade, multiplied by the same decoded R8 visibility.
Authored Source styles remain separate planes, with no current style modulation baked
into samples. Plain planes use Lambert; bump planes use the three renderer tangent-space
axes, preserving the pixel shader's linear weighted-basis response. The surface used
for radiance is unpushed (including displacement surface positions); visibility retains
its independent biased/recovered origins. Local addition happens after ordinary
receiver minlight/macro processing, so those treatments do not alter runtime direct.
Sun remains runtime-only. Native BSP lightmaps, ambient cubes, VHV and detail policy are
unchanged. Four highres styles are retained; overflowing selected-local contributions
use sparse per-face runtime exceptions rather than rejecting the paired publication.
Every lit world face requires `kFaceHasBakedLocalDirect` (bit 16) and a known baked pose.
Old v1/v2/v3 assets are runtime-rejected; complete paired rebakes validate their
identities then discard old enhanced data. Visibility sharing requires whole-scene equality.
The v3 visibility-set record uses its former reserved tail for two sparse section offsets
and counts. `UnbakedFaceDisk` is 12 bytes (`faceOrdinal`, `firstLightIndex`, `lightCount`);
records are strictly face-sorted and partition a contiguous uint32 selected-local index
array, sorted/unique within each face. Both sections follow the face-support bitmap and
precede R8 payload, each 16-byte aligned (including canonical empty offsets).
Existing 128-byte set and 16-byte face-visibility records retain their sizes. The validator
requires a full four-style lit baked palette, local-only indices with visibility support,
and styles absent from the owning palette. Shared sets require identical sparse lists.
The baker logs `Hlight: baked local direct fallback faces=N faceLights=M (LDR/HDR)`.
V3 was amended before shipping; preliminary no-exception-layout v3 assets must be rebaked.

Version 4 extends selected-local direct to eligible static-prop hardware vertices while
leaving native VHV/PPL byte encodings unchanged. VHV first converts RGBExp to linear,
then `LinearToVertexLight` / `ColorClamp` / RGBA8888; `DoLighting` decodes the color through
`GammaToLinear(staticLightingColor*cOverbright)`. This cannot preserve four separate
Source styles or the required HDR radiance range, so it is not the direct carrier.
Instead each hardware mesh's existing 32-byte visibility record replaces two reserved
words with `directPayloadByteOffset` / `directPayloadBytes` (both zero means full runtime).
A 4-aligned inline block follows its R8 planes in the same immutable payload:
`PropDirectDisk` (48 bytes), RGBA16F[style][angularPlane][hardwareVertex], then sorted
uint32 unbaked selected-local IDs. The header carries flags, a four-slot style palette
(style 0 first, unused 255), vertex/plane counts, RGB byte count and exception count.
RGB is unstyled normalized linear diffuse; A is zero. Two angular planes store Lambert
and squared half-Lambert from authored vertex normals. Geometry/radiance uses authored
world positions; visibility continues to use its independent recovered ray origins.
The exact all-LOD/strip-group source-index scatter is shared with visibility/VHV.
Styles are admitted in canonical selected-light order; overflow stays full runtime per
mesh and keeps its R8 field. Sun stays runtime-only; props using NO_PER_VERTEX_LIGHTING
or active per-texel lighting retain full per-pixel locals and are explicitly listed.
Vertex interpolation and base-normal sampling (rather than a pixel normal map) are
intentional approximations; finite-radius visibility itself remains the existing R8 field.
Half quantization has at most 1/2048 relative error for positive normal values and at
most 2^-25 absolute error in the subnormal range; values above 65504 reject explicitly.
Spatial/angular interpolation error is scene-dependent, not bounded by those storage errors.


Direct frame generation follows renderer `TangentSpaceSurfaceSetup` /
`TangentSpaceComputeBasis`: texture T anchors the frame and mirrored mappings flip
only S. This is not VRAD's texture-S-anchored `GetBumpNormals`. Normalize vertex
tangents exactly as the generated highres VS does, then interpolate normals/axes;
do not reconstruct or renormalize a per-luxel frame in the pixel-stage equation.
Displacement frames use full-resolution checkerboard triangles and separately smooth
tangent S across engine seams without modifying native transport cores. A fixed baked
field cannot reproduce a changing coarse displacement-LOD triangle's shading-frame
interpolation: exact receiver-frame matching assumes full-resolution attributes.

Brush receiver padding clamps to the closed hull of the actual native renderer
triangles. A degenerate T-junction triangle has no interior but can retain finite,
nonzero edges; those edges remain eligible for the existing nearest-edge clamp.
Interior barycentrics alone require a positive determinant. The resulting convex
weights interpolate the original vertex position and shading frame without changing
native primitive indices, inventing fan triangles, dropping luxels or loosening
geometry tolerances. Fully collapsed geometry with no usable edge still rejects.

The focused CPU regression is
`tests/baked_direct_test.py --receiver-geometry-only --receiver-probe <baked_receiver_probe.exe>`;
the full direct suite includes it. It covers the exact `d1_trainstation_02` face 845
native primitives and density-1/density-4 grids, collinear and repeated vertices,
positive/thin-positive interiors, collapsed rejection and actual point/frame interpolation.
An isolated density-4 station bake additionally completed both LDR/HDR solutions and
validated paired publication; face 845 retained its 33-by-5 grid in both modes.
The native VRAD `no samples 845` diagnostic can remain for this zero-area primitive;
it is distinct from the resolved enhanced receiver-mapping failure.

The dedicated `tests/baked_direct_test.py` invokes production CPU radiance/angular
helpers against an independent analytic oracle; paired fixture tests additionally
exercise actual RGB encoding, Source-style palettes, HDR/LDR identity and rollback.
High-resolution byte buffers use bounded unsigned capacity arithmetic; the shared
`CUtlMemory` allocator also guards the former signed doubling overflow at 1 GiB.
Final publication preflights all aligned sections and
reserves the complete file once; size limits and validation remain unchanged.
The CPU probe covers crossing 1 GiB, the exact signed limit, rejection above it,
aligned section accounting, and small real-buffer growth/data preservation.
Pass `--large-byte-growth` to `tests/baked_direct_test.py` for an actual shared
`CUtlVector<uint8>` growth from 1 GiB to 1.1 GiB; allocation failure reports a skip,
while successful growth checks capacity and endpoint data. Rebuild static tier1
(`CUtlBuffer::PutOverflow`) and all dependent runtime/tool binaries after allocator changes.

Producer regressions: `tests/hybrid_visibility_test.py --probe <validator-probe>`
executes the shared C++ validator on complete/corrupt byte fixtures, while
`tests/hybrid_visibility_gpu_test.py --probe <GPU-probe> --dll-dir <SDK-bin-x64>`
executes the production Vulkan service on both hardware-RT and compute-BVH.
The GPU fixture independently expects open=1, blocked=0 and disk penumbra=0.5;
it also proves binary alpha 127/128, immutable-only masks, finite ray endpoints,
no-self hit exclusion and blocked invalid receivers. The mathematical/source-only
checks remain explicitly labelled separately from these production GPU results.

The active paired runner additionally covers `prop-direct-analytic`,
`prop-direct-no-per-vertex`, `prop-direct-pertexel` and `prop-direct-zero-selected`.
Preparation chooses a separate real multi-LOD model for these cases, leaving the
frozen runner's existing model preference unchanged. Every authored LOD/strip-group
mesh must agree with VHV/source-index CRC and the authored-geometry diagnostics;
independent runtime equations reconstruct Lambert/half-Lambert RGB from manifest/R8.
The five-style fixture requires genuine nonzero overflow and tests incorrect
exception mutations. Skip fixtures assert compiled flags, per-mode prop-ID logs and
retained nonzero visibility. Zero-selected props still carry valid zero style-0 blocks.
Pass `--prop-vhv-baseline-producer` pointing to a preserved pre-v4 executable/DLL
and its SPV directory, with the executable-derived `bin/x64/filesystem_stdio.dll`
dependency available. Identical options prove native VHV bytes unchanged; ordinary
VRAD is not a valid control because its existing selected-light policy differs.
The analytic case also rebakes that real v3 output through checked v4 import.

Build the GPU probe after the baker in an x64 MSVC developer shell from `src`
(the probe and copied modules are test-only, not deployed game files):

```bat
cl /nologo /std:c++17 /EHsc /MT /DCOMPILER_MSVC /DCOMPILER_MSVC64 /DWIN32 /D_WIN32 /DWIN64 /D_WIN64 /DPLATFORM_64BITS ^
 /Ipublic /Ipublic\tier0 /Ipublic\tier1 /Iutils\vrad_restir /Ithirdparty\vulkan_sdk\Include ^
 utils\vrad_restir\tests\hybrid_visibility_gpu_probe.cpp ^
 /Fo:"%TEMP%\hybrid_visibility_gpu_probe.obj" /Fe:"%TEMP%\hybrid_visibility_gpu_probe.exe" ^
 /link utils\vrad_restir\Release\x64\restir_vulkan.obj utils\vrad_restir\Release\x64\restir_vulkan_scene.obj ^
 utils\vrad_restir\Release\x64\restir_vulkan_services.obj utils\vrad_restir\Release\x64\restir_vulkan_bake.obj ^
 /LIBPATH:lib\public\x64 tier1.lib tier0.lib vstdlib.lib mathlib.lib thirdparty\vulkan_sdk\Lib\vulkan-1.lib
xcopy /I /Y ..\game\bin\x64\vrad_restir_shaders\*.spv "%TEMP%\vrad_restir_shaders\"
python utils\vrad_restir\tests\hybrid_visibility_gpu_test.py --probe "%TEMP%\hybrid_visibility_gpu_probe.exe" --dll-dir "%SDK_MP%\bin\x64"
```

Prepared real-map variants include `hybrid-prop-no-per-vertex` and
`hybrid-prop-no-self`. The former must retain all visibility mesh/LOD blocks even
when no VHV files are emitted. `hlight_contract_test.visibility_stats` reports
open/blocked/fractional world and prop receivers once per unique visibility set.
`paired_bake_test.py` additionally asserts shared versus distinct sets under the
same whole-scene reuse decision, along with rollback of invalid paired output.

### Dense ambient probes (`.hprobe`)

`-restir_shadowmaps` also writes `maps/<name>.hprobe` beside the `.hlight` pak member: per lighting mode, a sparse
axis-aligned grid of SH L2 diffuse irradiance probes (`public/hprobe_bsp.h`) for the DX12 PBR model shaders. The
`.hlight` bytes, the native leaf ambient lumps and every old shader are unchanged. A probe holds the same quantity
as a leaf ambient cube, `E/pi` in the engine's linear cube units (a uniform environment gives its cube side), with a
directional basis. It folds in the full-source lightmap gather over 256 uniform sphere directions
(`restir_probe_sh.comp`, the miss policy of `restir_gather_ambient.comp`), material emitters (the 16-sample scheme of
`PointDirectLight`) and the `DWL_FLAGS_INAMBIENTCUBE` surface worldlights flagged by the leaf ambient stage.

Storage is 29 bytes per probe: R11G11B10F flat irradiance, six RGBA8_SNORM band textures (`s_k = e_k / DC * 0.25`,
ratio range +-4) and an R8 validity, all premultiplied by validity so hardware filtering renormalises. Bricks are
4x4x4 cells (5x5x5 probes, edge probes duplicated) and are allocated sparsely; the file stores the indirection table
and the padded brick atlas exactly as the runtime uploads them. The grid is capped at 256 MiB of padded atlas plus
indirection per mode and the bake fails with `raise -restir_ambientgrid or lower -restir_ambientgrid_reach` when it
would exceed that; nothing is reduced silently. Probes inside solid, inside an opaque leaf brush, or whose axis rays of
one spacing start inside geometry or reach a displacement back face are invalid; invalid probes get two
neighbour-mean dilation passes, and negative `l = 2` lobes are shrunk by 0.75/0.5/0.25/0 until the sphere no longer
undershoots by more than 2% of its peak. The bake prints `Hlight: ambient probe grid: N bricks (...)` and fails
with `ambient probe encoding/addressing self-check failed` if decoded strided probes disagree with the float
coefficients.

The `.hprobe` header carries the CRC32 of the exact `.hlight` it was baked with and each mode's face/lighting
identity, so a stale or foreign grid is rejected. A mode whose existing `.hlight` has no valid paired `.hprobe`
(maps baked before this stage) is not retained when the asset is rewritten: both modes of a `-restir_shadowmaps`
bake are replaced anyway, so such an input never fails the bake. The paired HDR reuse branch copies the LDR grid,
because it reuses the LDR transport, leaf cubes and worldlight flags. Without `-restir_shadowmaps` the `.hprobe`
member is removed together with the `.hlight`.

`tests/hprobe_contract_test.py <bsp>` parses the member with the header's rules, binds it to the `.hlight` bytes by
CRC and per-mode identity, requires at least one brick per mode and spot-checks stored texels;
`--hlight-reference <bsp>` additionally requires the `.hlight` member to be byte-identical to a bake of the same
input made before this stage. `hlight_contract_test.py` is unchanged.

## Emissive materials

Every `UnlitGeneric` material emits light. Other shaders emit when `$selfillum` is enabled, using base-texture alpha or `$selfillummask` as the emission mask; without a separate mask, the VTF must advertise an alpha channel. Emission uses the `$basetexture` colour per texel, linearized with gamma 2.2, rather than a material-average colour. Tool materials and render-target base textures are excluded.

`UnlitGeneric` emission honours `$color`, `$color2`, `$hdrcolorscale` in HDR, `$alphatest` coverage and non-additive `$translucent` alpha. Self-illumination uses linear `$selfillumtint` and the mask's stored RGB values without gamma conversion; it ignores `$translucent`, `$alphatest` and `$hdrcolorscale`, matching the shader's self-illumination path. `$nocull` makes either type emit from both sides.

A displayed linear colour D emits D*255/pi radiance per unit area in VRAD light units, multiplied by `-restir_emissivescale`. Emitters cover world and brush-entity faces, displacements and all static props, including no-shadow props, with the prop's skin applied. Material emitters are not exported to `LUMP_WORLDLIGHTS`; their light is baked into face lightmaps, static/detail prop lighting and leaf ambient cubes.

## Lightmap density

VBSP fixes lightmap density per brush side (`lightmapscale`, default 16 units per luxel) and splits faces so no brush lightmap exceeds 32 luxels; the engine and VRAD rely on that invariant. `-restir_lightmapscale F` (F < 1) raises the density of an already compiled BSP: every texinfo used by a lit brush face has its luxel vectors scaled by 1/F, face extents are recomputed, and faces that now exceed 32 luxels are split exactly like VBSP's `SubdivideFace` (`utils/vbsp/faces.cpp:1167`). New vertices and edges are welded with VBSP's tolerances, shared split edges are emitted as reversed surfedges, and every face-indexed lump is remapped (models, nodes, leaf faces, face ids, macro texture info, displacement parent faces, overlays, vertex normals). Split faces drop their T-junction primitives; the split edge is exact on both sides. Displacements keep their original density (their sample grid is baked into separate lumps).

Limits: `LUMP_LIGHTING` is capped at 16 MB per mode (`MAX_MAP_LIGHTING`); the stage estimates the light data size from the new extents and refuses scales that would exceed it, printing the shortfall. `ep2_outland_09` reaches the cap at about `0.58` (3.0x luxels). Overlays cannot reference more than 64 faces and are truncated with a warning when a split pushes them over. The applied scale is recorded as `_restir_lightmapscale` on worldspawn so a later pass (including the launcher's second `-both` pass) does not densify twice; coarsening an already densified BSP is refused — recompile with VBSP instead.

## Sky ambient match

Every bake also stores the factor that gives the map's 2D skybox the brightness of its `light_environment` ambient, as worldspawn keys `_skyscale_ldr` / `_skyscale_hdr` (one per baked mode, e.g. `"_skyscale_hdr" "0.911622"`). The DX12 client's `r_sky_ambientmatch` applies it (see `materialsystem/stdshaders_dx12/README.md`); no other tool reads the keys. `restir_sky_ambient.cpp` writes the key for the pass's mode just before the BSP is staged (`UnparseEntities`), replaces an earlier value and removes a stale key when the mode cannot be matched. The entity lump is not part of any `.hlight` identity (`hlight_output.cpp` `GeometryIdentities` hashes planes, texdata, vertexes, texinfo, edges, surfedges, models, the displacement lumps and the texdata string lumps, plus the mode's faces and lighting), so an `-restir_shadowmaps` bake stays admitted: the baked `d1_trainstation_02` copy loads in game with `Highres: state=2 ... error=""`.

**Definition.** The ambient is the scene's `emit_skyambient` intensity (the stock rules the scene builder already applies: `_ambientHDR` in HDR when valid, else `_ambient`, else half the sun; HDR multiplies `_AmbientScaleHDR`; first `light_environment` only; `-lightscale` included) divided by 255, i.e. the engine's linear units — the same value `restir_lightmap.glsl` returns for a ray that escapes to the sky — reduced to Rec.709 luminance. VRAD weights its sky ambient by cosine (`GatherSampleAmbientSkySSE`), so an unoccluded up-facing surface receives exactly that value. The sky it stands for is the uniform dome of the same cosine-weighted integral, so the sky luminance is the cosine-weighted mean over the upper hemisphere, `1/pi * integral(z > 0) Y(sky(w)) * w.z dw` with `Y` the luminance of the colour the sky shader writes, and the stored factor is `ambient / sky`. A single scalar, so the hue is untouched; no clamp is applied.

**Sampling.** The sky is evaluated the way the engine draws it, on a 1024x1024 grid per face (the down face never reaches `z > 0`): `engine/gl_warp.cpp` face layout (`st_to_vec`, `skytexorder`, the 1/512 texcoord clamp and the `t` flip), the face VMT's `$basetexturetransform` (a stock side face is `scale 1 2`, which maps the upper half of the face onto the texture and clamps the rest), the texture's clamp/wrap flags and the sky shader's decoding of `skybox/<skyname>{rt,lf,bk,ft,up,dn}` for the mode: `Sky` shader LDR = `$basetexture` (sRGB) * `$color`; HDR = `$hdrcompressedtexture` (RGBS: rgb * a, linear) * 8 * `$color`, else `$hdrbasetexture` (sRGB) * `$color`. In game, the model reproduced the centre of LDR screenshots of the real sky within 2% for the `ft` view and two `up` views and within 7% for `rt` (face orientation, `t` flip, transform and clamp); the `bk` and `lf` views were partly covered by world geometry and were not used.

**Not matched (a Warning, no key, the bake continues).** `UnlitGeneric` skies (the stock non-`_hdr` skybox VMTs): that shader applies `$color` through `GammaToLinear`, a 256-entry table that returns 1 from 0.95 up, so it cannot carry an exact scale. Also patch VMTs, `$hdrcompressedtexture0` skies (the DX12 shader draws them as a constant), 16-bit/float sky textures, a map without `light_environment`/`skyname`, and a zero ambient or sky luminance (`Msg`, no key). Warnings read `Sky ambient match (hdr): materials/skybox/<name>rt.vmt: <reason>; no _skyscale_hdr written`.

Each baked mode logs `VRAD ReSTIR: sky ambient match (ldr): ambient luminance A, sky luminance S, _skyscale_ldr F`. `d1_trainstation_02` (`skyname sky_day01_01_hdr`, `_ambient 190 201 220 100`): ambient luminance 0.23026 in both modes; sky luminance 0.24910 (LDR) / 0.25259 (HDR); factors 0.924396 / 0.911622. For comparison, a plain texel mean over the six face textures would give 0.640 / 0.631, the solid-angle mean of the upper hemisphere 0.757 / 0.747: the cosine weighting is what ties the factor to the lightmap's ambient. A `-both -fast` bake of a copy with `_ambient 190 201 220 255` gives 2.35721 / 2.32464, and LDR screenshots (`mat_hdr_level` is enforced to >= 2 by the DX12 renderer, so LDR runs on LDR-only maps; HDR lumps zeroed in the copy) measure on/off 2.358 in sRGB-decoded linear luminance.

Paired passes reload the transaction BSP between modes. `ParseEntities` pushes onto each entity's existing epair list, so `UnloadSelectedBSP` now frees the parsed entities; without that, the second pass's `UnparseEntities` wrote every key twice.
