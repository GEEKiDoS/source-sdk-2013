Upscaler runtimes for shaderapidx12 (DLAA, FSR, XeSS, DLSS-NR)
==============================================================

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

Licenses
--------
Each provider folder carries its license: xess/LICENSE.txt, fidelityfx2/LICENSE.md, ngx/LICENSE.txt, dlssnr/LICENSE.txt.
