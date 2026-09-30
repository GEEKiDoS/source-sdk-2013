@echo off
setlocal
set "SCRIPT=%~dp0process_shaders.ps1"
if /i "%~1"=="-game" goto destination
if /i "%~1"=="-stage" goto destination
echo Usage: buildshaders_dx12.bat -game ^<gamedir^> ^|-stage ^<tempdir^> [-Root ^<nativeShaderRoot^>] [-LegacyRoot ^<legacyShaderRoot^>] 1>&2
exit /b 2
:destination
if "%~2"=="" exit /b 2
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -Native %*
exit /b %ERRORLEVEL%
