@echo off
setlocal
rem Runs vrad_restir without installing anything into the SDK Base folders.
rem   vrad_restir.bat [vrad_restir options] <map.bsp | map name under mod_episodic\maps>
rem Example:
rem   vrad_restir.bat -both -StaticPropLighting -TextureShadows -final ep2_outland_09
rem
rem Override the content roots by setting these before calling (or edit the defaults):
set "SDK_MP=%SDK_MP%"
set "SDK_SP=%SDK_SP%"
set "HL2_DIR=%HL2_DIR%"
if "%SDK_MP%"=="" set "SDK_MP=E:\SteamLibrary\steamapps\common\Source SDK Base 2013 Multiplayer"
if "%SDK_SP%"=="" set "SDK_SP=E:\SteamLibrary\steamapps\common\Source SDK Base 2013 Singleplayer"
if "%HL2_DIR%"=="" set "HL2_DIR=E:\SteamLibrary\steamapps\common\Half-Life 2"

set "GAMEDIR=%~dp0"
set "GAMEDIR=%GAMEDIR:~0,-1%"
set "MOD=%GAMEDIR%\mod_episodic"
set "TOOLDIR=%GAMEDIR%\bin\x64"
set "TOOLGAME=%TEMP%\vrad_restir_game"

if not exist "%TOOLDIR%\vrad_restir.exe" (
	echo vrad_restir.exe not found in %TOOLDIR% - build the vrad_restir solution first.
	exit /b 1
)
if not exist "%SDK_MP%\bin\x64\tier0.dll" (
	echo Source SDK Base 2013 Multiplayer x64 runtime not found at %SDK_MP%\bin\x64
	exit /b 1
)

rem Tool runtime (tier0/vstdlib/filesystem_stdio) comes from the SDK Base; nothing is copied.
set "PATH=%SDK_MP%\bin\x64;%PATH%"

rem The mod's gameinfo uses |appid_NNN| mounts that only the engine understands; the
rem tool filesystem needs explicit paths, so write a private gameinfo for this run.
if not exist "%TOOLGAME%" mkdir "%TOOLGAME%"
(
	echo "GameInfo"
	echo {
	echo 	game	"VRAD ReSTIR tool mount"
	echo 	type	singleplayer_only
	echo 	FileSystem
	echo 	{
	echo 		SteamAppId	243750
	echo 		SearchPaths
	echo 		{
	echo 			game+mod	"%MOD%"
	echo 			game		"%HL2_DIR%\ep2"
	echo 			game		"%HL2_DIR%\episodic"
	echo 			game		"%SDK_SP%\ep2\ep2_pak.vpk"
	echo 			game		"%SDK_SP%\episodic\ep1_pak.vpk"
	echo 			game		"%SDK_SP%\hl2\hl2_textures.vpk"
	echo 			game		"%SDK_SP%\hl2\hl2_misc.vpk"
	echo 			game		"%SDK_MP%\hl2\hl2_textures.vpk"
	echo 			game		"%SDK_MP%\hl2\hl2_misc.vpk"
	echo 			game		"%SDK_SP%\ep2"
	echo 			game		"%SDK_SP%\episodic"
	echo 			game		"%SDK_MP%\hl2"
	echo 			platform	"%SDK_MP%\platform"
	echo 		}
	echo 	}
	echo }
) > "%TOOLGAME%\gameinfo.txt"

rem Last argument is the map; a bare name resolves under mod_episodic\maps.
set "MAP="
set "ARGS="
:collect
if "%~1"=="" goto run
if not "%MAP%"=="" set "ARGS=%ARGS% %MAP%"
set "MAP=%~1"
shift
goto collect

:run
if "%MAP%"=="" (
	echo usage: %~nx0 [vrad_restir options] ^<map.bsp^>
	exit /b 1
)
if not exist "%MAP%" if exist "%MOD%\maps\%MAP%.bsp" set "MAP=%MOD%\maps\%MAP%.bsp"
if not exist "%MAP%" if exist "%MOD%\maps\%MAP%" set "MAP=%MOD%\maps\%MAP%"
if not exist "%MAP%" (
	echo map not found: %MAP%
	exit /b 1
)

"%TOOLDIR%\vrad_restir.exe"%ARGS% -game "%TOOLGAME%" "%MAP%"
exit /b %ERRORLEVEL%
