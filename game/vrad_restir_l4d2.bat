@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem Runs vrad_restir against extracted L4D2 content without installing anything into SDK Base.
rem Lighting is HDR only. With -restir_shadowmaps the map (shipped v21 or v20) is left untouched and the
rem result is written to mod_l4d2\maps\<map>.light; without it, a v21 input is rewritten as a standard
rem BSP v20 map whose v21 static-prop RGBA tints ride the v20 ptnt game lump (version 1).
rem GPU submissions adapt toward about 250 ms per fence wait to avoid Windows TDR on slow GPUs.
rem   vrad_restir_l4d2.bat [vrad_restir options] <map.bsp | map name under mod_l4d2\maps or the retail maps>
rem Example:
rem   vrad_restir_l4d2.bat -restir_shadowmaps -fast c1m1_hotel
rem
rem Override the content roots by setting these before calling:
set "SDK_MP=%SDK_MP%"
set "L4D2_ROOT=%L4D2_ROOT%"
if "%SDK_MP%"=="" set "SDK_MP=C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer"
if "%L4D2_ROOT%"=="" set "L4D2_ROOT=C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2"

set "GAMEDIR=%~dp0"
set "GAMEDIR=%GAMEDIR:~0,-1%"
set "MOD=%GAMEDIR%\mod_l4d2"
set "TOOLDIR=%GAMEDIR%\bin\x64"
set "TOOLRUN=%TEMP%\vrad_restir_l4d2_runtime"
set "TOOLGAME=%TEMP%\vrad_restir_game_l4d2"

if not exist "!TOOLDIR!\vrad_restir.exe" (
    echo vrad_restir.exe not found in !TOOLDIR! - build the vrad_restir solution first.
    exit /b 1
)
if not exist "!SDK_MP!\bin\x64\tier0.dll" (
    echo Source SDK Base 2013 Multiplayer x64 runtime not found at !SDK_MP!\bin\x64
    exit /b 1
)
if not exist "!L4D2_ROOT!\left4dead2\pak01_dir.vpk" (
    echo L4D2 base VPK index not found under !L4D2_ROOT!
    exit /b 1
)

rem Stage the executable's expected bin\x64 runtime without modifying SDK Base.
if not exist "!TOOLRUN!" mkdir "!TOOLRUN!"
if not exist "!TOOLRUN!\bin\x64" mkdir "!TOOLRUN!\bin\x64"
copy /Y "!TOOLDIR!\vrad_restir.exe" "!TOOLRUN!\vrad_restir.exe" >nul
copy /Y "!TOOLDIR!\vrad_restir_dll.dll" "!TOOLRUN!\vrad_restir_dll.dll" >nul
copy /Y "!SDK_MP!\bin\x64\filesystem_stdio.dll" "!TOOLRUN!\bin\x64\filesystem_stdio.dll" >nul
copy /Y "!SDK_MP!\bin\x64\tier0.dll" "!TOOLRUN!\bin\x64\tier0.dll" >nul
copy /Y "!SDK_MP!\bin\x64\vstdlib.dll" "!TOOLRUN!\bin\x64\vstdlib.dll" >nul
for %%F in ("!TOOLDIR!\OpenImageDenoise*.dll" "!TOOLDIR!\tbb*.dll") do if exist "%%~F" copy /Y "%%~F" "!TOOLRUN!" >nul
xcopy /I /Y "!TOOLDIR!\vrad_restir_shaders\*.spv" "!TOOLRUN!\vrad_restir_shaders" >nul
set "PATH=!TOOLDIR!;!SDK_MP!\bin\x64;%PATH%"

rem The tool runtime is staged in TEMP; no SDK Base files are modified.
if not exist "!TOOLRUN!\bin\x64\filesystem_stdio.dll" (
    echo Could not stage filesystem_stdio.dll under !TOOLRUN!\bin\x64
    exit /b 1
)

rem The engine gameinfo uses |appid_NNN| mounts; the tool filesystem needs explicit paths.
if not exist "!TOOLGAME!" mkdir "!TOOLGAME!"
(
    echo "GameInfo"
    echo {
    echo     game "VRAD ReSTIR L4D2 tool mount"
    echo     type multiplayer_only
    echo     FileSystem
    echo     {
    echo         SteamAppId 243750
    echo         SearchPaths
    echo         {
    echo             game+mod "!MOD!"
    echo             game "!L4D2_ROOT!\update\pak01_dir.vpk"
    echo             game "!L4D2_ROOT!\update"
    echo             game "!L4D2_ROOT!\left4dead2_dlc3\pak01_dir.vpk"
    echo             game "!L4D2_ROOT!\left4dead2_dlc3"
    echo             game "!L4D2_ROOT!\left4dead2_dlc2\pak01_dir.vpk"
    echo             game "!L4D2_ROOT!\left4dead2_dlc2"
    echo             game "!L4D2_ROOT!\left4dead2_dlc1\pak01_dir.vpk"
    echo             game "!L4D2_ROOT!\left4dead2_dlc1"
    echo             game "!L4D2_ROOT!\left4dead2\pak01_dir.vpk"
    echo             game "!L4D2_ROOT!\left4dead2"
    echo             game "!SDK_MP!\hl2\hl2_textures_dir.vpk"
    echo             game "!SDK_MP!\hl2\hl2_misc_dir.vpk"
    echo             game "!SDK_MP!\hl2"
    echo             platform "!SDK_MP!\platform\platform_misc_dir.vpk"
    echo             platform "!SDK_MP!\platform"
    echo         }
    echo     }
    echo }
) > "!TOOLGAME!\gameinfo.txt"

rem Last argument is the map; a bare name resolves under mod_l4d2\maps.
set "MAP="
set "ARGS="
:collect
if "%~1"=="" goto run
if not "!MAP!"=="" set "ARGS=!ARGS! !MAP!"
set "MAP=%~1"
shift
goto collect

:run
if "!MAP!"=="" (
    echo usage: %~nx0 [vrad_restir options] ^<map.bsp^>
    exit /b 1
)
if not exist "!MAP!" if exist "!MOD!\maps\!MAP!.bsp" set "MAP=!MOD!\maps\!MAP!.bsp"
if not exist "!MAP!" if exist "!MOD!\maps\!MAP!" set "MAP=!MOD!\maps\!MAP!"
if not exist "!MAP!" if exist "!L4D2_ROOT!\left4dead2\maps\!MAP!.bsp" set "MAP=!L4D2_ROOT!\left4dead2\maps\!MAP!.bsp"
if not exist "!MAP!" if exist "!L4D2_ROOT!\left4dead2\maps\!MAP!" set "MAP=!L4D2_ROOT!\left4dead2\maps\!MAP!"
if not exist "!MAP!" (
    echo map not found: !MAP!
    exit /b 1
)

rem A .light always lands in the mod's maps folder, never next to a retail BSP.
"!TOOLRUN!\vrad_restir.exe"!ARGS! -restir_lightdir "!MOD!\maps" -game "!TOOLGAME!" "!MAP!"
exit /b %ERRORLEVEL%
