Vulkan SDK subset vendored for utils/vrad_restir (VRAD ReSTIR GPU lightmap baker)
================================================================================

Version : LunarG Vulkan SDK 1.4.363.0 (Windows x64)
Source  : https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/vulkansdk-windows-X64-1.4.363.0.exe
Size    : 303,082,720 bytes
SHA256  : 94a82d378f7a5e3e54c9db7d2fb7016af136e14ac0a18dbf0f2f67a36352d141
          (matches https://sdk.lunarg.com/sdk/sha/1.4.363.0/windows/vulkansdk-windows-X64-1.4.363.0.exe.txt)

Contents (38 files, ~10.5 MB; nothing else from the SDK is needed to build or run the baker):
  Include/vulkan/*.h       C headers only (the C++ vulkan*.hpp / .cppm family is intentionally omitted)
  Include/vk_video/*.h     pulled in by vulkan_core.h
  Lib/vulkan-1.lib         loader import library; vulkan-1.dll itself is driver/runtime supplied, never vendored
  Bin/glslc.exe            shaderc v2026.4, standalone; compiles utils/vrad_restir/shaders/*.comp
  Bin/spirv-val.exe        SPIRV-Tools v2026.4; validates the generated SPIR-V (target vulkan1.2)
  LICENSE.txt              Apache-2.0 text + per-component attribution

VPC usage (utils/vrad_restir/vrad_restir.vpc):
  $AdditionalIncludeDirectories  "$BASE;$SRCDIR\thirdparty\vulkan_sdk\Include"
  $AdditionalDependencies        "$BASE $SRCDIR\thirdparty\vulkan_sdk\Lib\vulkan-1.lib"
No VULKAN_SDK environment variable is read anywhere.

.gitignore: the repository's Visual Studio template rule "[Bb]in/" would swallow Bin/; the root .gitignore carries
  !/src/thirdparty/vulkan_sdk/Bin/
immediately AFTER that rule (a negation placed before it has no effect). Verify with
  git check-ignore -v src/thirdparty/vulkan_sdk/Bin/glslc.exe   -> must print nothing.

Refresh recipe (new SDK version):
  1. Download the installer, verify size + SHA256 against the LunarG .txt.
  2. Do NOT run the installer (unattended install needs elevation and registers the system loader). Carve the 11 embedded
     7z payloads by signature 37 7A BC AF 27 1C and extract with 7-Zip: payload 2 = Bin, 6 = Include, 7 = Lib.
  3. Copy exactly the file set above; update Version/SHA256 here and the component versions in LICENSE.txt.
  4. Rebuild the shaders: utils\vrad_restir\shaders\buildshaders.bat (uses Bin\glslc.exe + Bin\spirv-val.exe).
