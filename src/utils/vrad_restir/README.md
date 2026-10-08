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
| `-restir_iterations N` | `128` | ReSTIR iterations; `1..65535`. Bounce light is fed back from the irradiance of earlier iterations, so this also bounds the effective bounce depth; the final estimate averages the second half of the iterations. |
| `-restir_candidates N` | `8` | Local-light (point/spot/surface) candidates per sample per iteration; `1..4096`. The sun, sky ambient, and the bounce path are one candidate each on top of this. |
| `-restir_spatial_radius N` | `2` | Spatial reuse radius in luxel cells; `0..128`. |
| `-restir_maxbounces N` | `4` | Maximum path bounces; `0` disables indirect light; `0..128`. |
| `-restir_seed N` | `1` | Deterministic sequence seed; `0..2147483647`. Same inputs and seed give byte-identical lighting. |
| `-restir_gpu N` | first discrete GPU | Vulkan adapter index; `0..255`. |
| `-restir_force_compute_bvh` | off | Force the software BVH compute backend. |
| `-restir_denoiser oidn\|none` | `oidn` | Select OIDN (bilateral fallback when the DLL is absent) or disable denoising. |
| `-restir_denoiser_quality fast\|balanced\|high` | `balanced` | OIDN quality setting. |
| `-restir_denoiser_device default\|cpu\|gpu` | `default` | OIDN device preference. |
| `-restir_probe x y z nx ny nz` | off | Diagnostic: after the bake, print the GPU light table and the direct/indirect light arriving at the given world point and normal per style. |
| `-restir_lightmapscale F` | `1.0` | Multiplier on brush-face luxel size, `0.0625..1.0`; `0.5` doubles density per axis. See "Lightmap density" below. |
| `-restir_emissivescale F` | `1.0` | Material-emission multiplier, `0..1000`; `0` disables material emitters without affecting `.rad` texlights. |
| `-restir_notexturealbedo` | albedo on | Disable per-texel bounce albedo. By default bounce light picks up each material's `$basetexture` per texel instead of VRAD's single reflectivity per material: the texture is linearized (2.2) and scaled so its average equals the material's reflectivity, so total bounced energy matches VRAD and only its spatial distribution changes (a dark stripe on a wall bounces less than its light neighbour). Applies to lightmap bounces, leaf ambient cubes and static/detail prop gathers; materials whose base texture cannot be loaded use their reflectivity. `-restir_texturealbedo` is accepted as the explicit default. |
| `-StaticPropLighting` | off | Bake static-prop vertex and texel lighting. |
| `-TextureShadows` | off | Evaluate alpha-tested material coverage for shadows. |
| `-smooth N` | `45` degrees | Phong smoothing threshold. |
| `-lights FILE` | none | Additional `.rad` light file. |

Legacy VRAD switches used by existing compile configurations are accepted and reported as ignored when they do not affect this baker (`-fast` and `-final` are honoured as presets instead). Unknown switches are errors. `-StaticPropPolys` is accepted as a no-op because ReSTIR always uses polygon-precision static-prop geometry.

Measured on `ep2_outland_09` (RTX 4070 SUPER, 920k luxels, one mode, no prop lighting): `-fast` 9 s, default 14 s, `-final` 34 s. Per-face HDR ratio against VRAD's bake: median 0.99 / 1.01 / 1.01 and 10th percentile 0.71 / 0.83 / 0.88 respectively; the 10th percentile tracks the dim, indirect-only faces that need more samples to converge.

The simplest way to run it is `game\vrad_restir.bat`, which needs nothing copied into the SDK Base folders: it puts the SDK Base 2013 Multiplayer `bin\x64` runtime on `PATH` for the call, writes a tool-readable `gameinfo.txt` under `%TEMP%\vrad_restir_game` from the configured content roots, and forwards every other argument. A bare map name resolves under `mod_episodic\maps`:

```bat
game\vrad_restir.bat -both -StaticPropLighting -TextureShadows -final ep2_outland_09
```

Content roots default to the local Steam library; override with `SDK_MP`, `SDK_SP` and `HL2_DIR` environment variables (or edit the defaults at the top of the script).

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
movable brush entities do not cast permanent local shadows.

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



## Emissive materials

Every `UnlitGeneric` material emits light. Other shaders emit when `$selfillum` is enabled, using base-texture alpha or `$selfillummask` as the emission mask; without a separate mask, the VTF must advertise an alpha channel. Emission uses the `$basetexture` colour per texel, linearized with gamma 2.2, rather than a material-average colour. Tool materials and render-target base textures are excluded.

`UnlitGeneric` emission honours `$color`, `$color2`, `$hdrcolorscale` in HDR, `$alphatest` coverage and non-additive `$translucent` alpha. Self-illumination uses linear `$selfillumtint` and the mask's stored RGB values without gamma conversion; it ignores `$translucent`, `$alphatest` and `$hdrcolorscale`, matching the shader's self-illumination path. `$nocull` makes either type emit from both sides.

A displayed linear colour D emits D*255/pi radiance per unit area in VRAD light units, multiplied by `-restir_emissivescale`. Emitters cover world and brush-entity faces, displacements and all static props, including no-shadow props, with the prop's skin applied. Material emitters are not exported to `LUMP_WORLDLIGHTS`; their light is baked into face lightmaps, static/detail prop lighting and leaf ambient cubes.

## Lightmap density

VBSP fixes lightmap density per brush side (`lightmapscale`, default 16 units per luxel) and splits faces so no brush lightmap exceeds 32 luxels; the engine and VRAD rely on that invariant. `-restir_lightmapscale F` (F < 1) raises the density of an already compiled BSP: every texinfo used by a lit brush face has its luxel vectors scaled by 1/F, face extents are recomputed, and faces that now exceed 32 luxels are split exactly like VBSP's `SubdivideFace` (`utils/vbsp/faces.cpp:1167`). New vertices and edges are welded with VBSP's tolerances, shared split edges are emitted as reversed surfedges, and every face-indexed lump is remapped (models, nodes, leaf faces, face ids, macro texture info, displacement parent faces, overlays, vertex normals). Split faces drop their T-junction primitives; the split edge is exact on both sides. Displacements keep their original density (their sample grid is baked into separate lumps).

Limits: `LUMP_LIGHTING` is capped at 16 MB per mode (`MAX_MAP_LIGHTING`); the stage estimates the light data size from the new extents and refuses scales that would exceed it, printing the shortfall. `ep2_outland_09` reaches the cap at about `0.58` (3.0x luxels). Overlays cannot reference more than 64 faces and are truncated with a warning when a split pushes them over. The applied scale is recorded as `_restir_lightmapscale` on worldspawn so a later pass (including the launcher's second `-both` pass) does not densify twice; coarsening an already densified BSP is refused — recompile with VBSP instead.
