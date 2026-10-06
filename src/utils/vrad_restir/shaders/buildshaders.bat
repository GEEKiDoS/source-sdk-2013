@echo off
setlocal EnableExtensions
pushd "%~dp0" || exit /b 1
set "GLSLC=..\..\..\thirdparty\vulkan_sdk\Bin\glslc.exe"
set "SPIRVVAL=..\..\..\thirdparty\vulkan_sdk\Bin\spirv-val.exe"
if not exist "%GLSLC%" (
	echo ERROR: Missing shader compiler %GLSLC%
	popd
	exit /b 1
)
if not exist "%SPIRVVAL%" (
	echo ERROR: Missing validator %SPIRVVAL%
	popd
	exit /b 1
)
set /a MODULES=0
for %%S in (bvh_morton bvh_sort bvh_hierarchy bvh_refit restir_init restir_candidates restir_temporal restir_spatial restir_accumulate restir_reconstruct restir_sun_visibility restir_trace_rays restir_gather_ambient restir_light_points) do (
	call :Compile %%S hw 1
	if errorlevel 1 goto Failed
	call :Compile %%S sw 0
	if errorlevel 1 goto Failed
)
echo SUCCESS: %MODULES% Vulkan 1.2 shader modules compiled and validated.
popd
exit /b 0

:Compile
echo Compiling %1_%2.spv
"%GLSLC%" --target-env=vulkan1.2 -O -DRESTIR_HW_RAYQUERY=%3 -o %1_%2.spv %1.comp
if errorlevel 1 (
	echo ERROR: Compilation failed for %1_%2.spv
	exit /b 1
)
echo Validating %1_%2.spv
"%SPIRVVAL%" --target-env vulkan1.2 %1_%2.spv
if errorlevel 1 (
	echo ERROR: Validation failed for %1_%2.spv
	exit /b 1
)
set /a MODULES+=1 >nul
exit /b 0

:Failed
echo ERROR: Shader generation stopped after %MODULES% validated modules.
popd
exit /b 1
