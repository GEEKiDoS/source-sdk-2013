Upscaler and frame generation runtimes for shaderapidx12 (DLAA, FSR, XeSS, DLSS-NR, DLSS-G, FSR FG, XeSS-FG)
==========================================================================================================

The repository contains only the headers and licenses; shaderapidx12 builds against them without any runtime present.
The runtime DLLs are not included. The renderer loads each one with LoadLibrary when an upscaler is first enabled; a
missing DLL only makes that provider report unavailable (r_upscaler / r_dlss_nr status -1), and everything else works.

Installing
----------
Put each DLL in its redist folder below (git-ignored). The shaderapidx12 post-build step copies whatever is present
into game/bin/x64. Copying the DLLs straight into game/bin/x64 works as well.

  Provider                Files                                         Folder
  ----------------------  --------------------------------------------  ------------------------
  XeSS AA                 libxess.dll                                   xess/redist/
  FSR native AA           amd_fidelityfx_loader_dx12.dll                fidelityfx2/redist/
                          amd_fidelityfx_upscaler_dx12.dll              (both are required)
  DLAA                    nvngx_dlss.dll                                ngx/redist/
  DLSS-NR                 nvngx_dlssnr.dll                              dlssnr/redist/

Where to get them
-----------------
XeSS
  Intel XeSS SDK 3.0.2: https://github.com/intel/xess/releases/tag/v3.0.2
  Release asset XeSS_SDK_3.0.2.zip, file bin/libxess.dll.

FSR
  AMD FidelityFX SDK v2.3.0:
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v2.3.0/Kits/FidelityFX/signedbin
  The loader finds the upscaler DLL by name in its own folder, so keep the two files together. FSR 4 is chosen only
  on AMD GPUs and has not been tested; other GPUs get FSR 3.1.

DLAA
  NVIDIA DLSS SDK: https://github.com/NVIDIA/DLSS/tree/main/lib/Windows_x86_64/rel, file nvngx_dlss.dll.
  Also needs an NVIDIA RTX GPU and driver; the driver provides the NGX core (_nvngx.dll).

DLSS-NR
  Not publicly distributed. The build that runs on RTX 40 series is a modified third-party build that must not be
  committed; see dlssnr/NOTICE.txt. Calls go through nvngx.dll_dlssnr_dx12.dll, which the shaderapidx12 build produces.

Verified builds
---------------
These are the files this renderer was tested with (SHA-256). Other versions with the same API should also load,
but have not been tested.

  libxess.dll                       2.0.2.68    251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7
  amd_fidelityfx_loader_dx12.dll    2.3.0.2740  e2d85aa05a9bd9ed8b38935fdf5199372cca6f74c12015143bb6f945ee1608aa
  amd_fidelityfx_upscaler_dx12.dll  4.1.1.2740  d0dcccc74a43c44ba435b7a369b456e0970d8a4464e4bd683119b374f2c9fb46
  nvngx_dlss.dll                    310.8.0.0   c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e
  nvngx_dlssnr.dll                  310.8.0.0   e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a

The DLSS repository's main branch currently ships a newer nvngx_dlss.dll than the 310.8.0.0 tested here.

Frame generation (r_framegen)
-----------------------------
Same rules: headers in the repository, DLLs git-ignored, loaded on first use, a missing provider is skipped by
r_framegen 1 (auto) and reported as status -1 when forced. Frame generation needs a windowed or borderless mode.

  Provider                Files                                         Folder
  ----------------------  --------------------------------------------  ------------------------
  DLSS-G                  sl.interposer.dll, sl.common.dll,             streamline/redist/
                          sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll,
                          nvngx_dlssg.dll
  FSR frame generation    amd_fidelityfx_loader_dx12.dll                fidelityfx2/redist/
                          amd_fidelityfx_framegeneration_dx12.dll
  XeSS-FG                 libxess_fg.dll, libxell.dll                   xess/redist/

DLSS-G
  NVIDIA Streamline SDK: https://github.com/NVIDIAGameWorks/Streamline/tree/main/bin/x64 (sl.*.dll) and
  nvngx_dlssg.dll from the same folder. Production sl.interposer.dll only: the renderer verifies its NVIDIA signature
  before loading (bypass with -dx12slunsigned for development builds). Requires an RTX 40 series or newer and
  hardware-accelerated GPU scheduling.

FSR frame generation
  AMD FidelityFX SDK v2.3.0 signedbin (same link as above), file amd_fidelityfx_framegeneration_dx12.dll next to the
  loader. Runs on any DX12 GPU; always one generated frame.

XeSS-FG
  Intel XeSS SDK 3.0.2 (same link as above), files bin/libxess_fg.dll and bin/libxell.dll. One generated frame on
  non-Intel GPUs.

  sl.interposer.dll                        2.13.0.0    27b2190057994c0b287c2c5716953bf1586f6499ac12fbbb2092b9aaf8396570
  sl.common.dll                            2.13.0.0    a4b2b5acbe49fbc6d44dd432cac19cd53218f698b2539dc7ed0fb268c72cfc8d
  sl.dlss_g.dll                            2.13.0.0    b8b5effd7debdb750abd216de43385fb653261712bc315d85eba68811fb3ee02
  sl.reflex.dll                            2.13.0.0    ecf12973cdcec2ffced2ea77b1c7e45f4d387e7c864ddb5531b66a6f947effb3
  sl.pcl.dll                               2.13.0.0    12aa4e76c28a27c735e4ecb3072f44d09428acb107b70ac38e4bd48ddb05f88d
  nvngx_dlssg.dll                          310.8.0.0   5d5cbf14d2727d47f93fd10bf77bd91708ae122482a6f86fd564971641ebd47b
  amd_fidelityfx_framegeneration_dx12.dll  4.0.1.2740  02297beedd285e822d3a64f314cf00faf378dcec0edc47ff0c4dd71b3a8c2f18
  libxess_fg.dll                           1.3.1.78    ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27
  libxell.dll                              1.3.2.10    d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7

Licenses
--------
Each provider folder carries its license: xess/LICENSE.txt, fidelityfx2/LICENSE.md, ngx/LICENSE.txt, dlssnr/LICENSE.txt,
streamline/LICENSE.txt.
