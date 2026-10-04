# VRAD ReSTIR

VRAD ReSTIR is the Win64 Vulkan-compute light baker for Source BSP files. It is a separate tool from the legacy `vrad` implementation and is invoked through `vrad_restir.exe`. The launcher loads the adjacent `vrad_restir_dll.dll`; `-both` performs independent LDR and HDR passes.

## Prerequisites

- Visual Studio 2022 and the repository VPC toolchain.
- A Vulkan 1.2-capable loader and GPU. The in-tree SDK subset is under `thirdparty/vulkan_sdk` and supplies the C headers, `vulkan-1.lib`, `glslc.exe`, and `spirv-val.exe`. The Vulkan runtime DLL is supplied by the graphics driver and is not vendored.
- Optional: Intel Open Image Denoise 2.5.1 runtime (`OpenImageDenoise.dll`, `OpenImageDenoise_core.dll`, the `OpenImageDenoise_device_*.dll` you want, and `tbb12.dll`) beside `vrad_restir.exe` or on `PATH`. The C API is declared locally in `restir_denoiser.cpp` and every entry point is resolved at run time, so no OIDN headers or import library are needed to build. Without the DLL the baker reports `denoiser=bilateral-fallback` and uses its built-in edge-aware filter. OIDN is Apache-2.0 licensed and is not included in this repository.
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
| `-restir_notexturealbedo` | albedo on | Disable per-texel bounce albedo. By default bounce light picks up each material's `$basetexture` per texel instead of VRAD's single reflectivity per material: the texture is linearized (2.2) and scaled so its average equals the material's reflectivity, so total bounced energy matches VRAD and only its spatial distribution changes (a dark stripe on a wall bounces less than its light neighbour). Applies to lightmap bounces, leaf ambient cubes and static/detail prop gathers; materials whose base texture cannot be loaded use their reflectivity. `-restir_texturealbedo` is accepted as the explicit default. |
| `-StaticPropLighting` | off | Bake static-prop vertex and texel lighting. |
| `-TextureShadows` | off | Evaluate alpha-tested material coverage for shadows. |
| `-smooth N` | `45` degrees | Phong smoothing threshold. |
| `-lights FILE` | none | Additional `.rad` light file. |

Legacy VRAD switches used by existing compile configurations are accepted and reported as ignored when they do not affect this baker (`-fast` and `-final` are honoured as presets instead). Unknown switches are errors. `-StaticPropPolys` is accepted as a no-op because ReSTIR always uses polygon-precision static-prop geometry.

Measured on `ep2_outland_09` (RTX 4070 SUPER, 920k luxels, one mode, no prop lighting): `-fast` 9 s, default 14 s, `-final` 34 s. Per-face HDR ratio against VRAD's bake: median 0.99 / 1.01 / 1.01 and 10th percentile 0.71 / 0.83 / 0.88 respectively; the 10th percentile tracks the dim, indirect-only faces that need more samples to converge.

Example, from `game\bin\x64`:

```bat
vrad_restir.exe -both -StaticPropLighting -TextureShadows -game <path to gameinfo dir> <path>\ep2_outland_09.bsp
```

`gameinfo.txt` must be a tool-readable one: the engine-only `|appid_NNN|` search-path mounts are not resolved by the tool file system, so list the content directories and `.vpk` files explicitly (quote paths containing spaces).

## BSP output

The result remains an ordinary Source BSP. The selected mode's face lightmaps, face style/offset arrays, worldlights, per-leaf ambient index/lighting lumps, static-prop pak entries (`sp_N.vhv` / `sp_hdr_N.vhv`), and detail-prop game lump are written using the existing Source contracts. LDR and HDR data coexist after `-both`; a single-mode pass does not replace the opposite mode's lighting data. Output is staged as `<map>.bsp.restir.tmp` and validated before the original BSP is replaced.

Invalid luxels follow VRAD's defaults: a face with no samples at all is written as `(255,0,0)`; a brush luxel that no sample cell reaches is reconstructed from the surrounding samples with VRAD's bounce-radial kernel and excluded from the face average; a luxel nothing reaches is black (`bRed2Black`). Displacement luxels without coverage are black.

## Lightmap density

VBSP fixes lightmap density per brush side (`lightmapscale`, default 16 units per luxel) and splits faces so no brush lightmap exceeds 32 luxels; the engine and VRAD rely on that invariant. `-restir_lightmapscale F` (F < 1) raises the density of an already compiled BSP: every texinfo used by a lit brush face has its luxel vectors scaled by 1/F, face extents are recomputed, and faces that now exceed 32 luxels are split exactly like VBSP's `SubdivideFace` (`utils/vbsp/faces.cpp:1167`). New vertices and edges are welded with VBSP's tolerances, shared split edges are emitted as reversed surfedges, and every face-indexed lump is remapped (models, nodes, leaf faces, face ids, macro texture info, displacement parent faces, overlays, vertex normals). Split faces drop their T-junction primitives; the split edge is exact on both sides. Displacements keep their original density (their sample grid is baked into separate lumps).

Limits: `LUMP_LIGHTING` is capped at 16 MB per mode (`MAX_MAP_LIGHTING`); the stage estimates the light data size from the new extents and refuses scales that would exceed it, printing the shortfall. `ep2_outland_09` reaches the cap at about `0.58` (3.0x luxels). Overlays cannot reference more than 64 faces and are truncated with a warning when a split pushes them over. The applied scale is recorded as `_restir_lightmapscale` on worldspawn so a later pass (including the launcher's second `-both` pass) does not densify twice; coarsening an already densified BSP is refused — recompile with VBSP instead.
