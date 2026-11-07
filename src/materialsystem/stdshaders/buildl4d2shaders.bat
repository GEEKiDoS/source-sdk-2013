@echo off
setlocal
if "%~1"=="" (
 echo Usage: buildl4d2shaders ^<in-place-mod-directory^>
 exit /b 1
)
set "L4D2_MOD_ROOT=%~f1"
pushd "%~dp0"
if errorlevel 1 exit /b 1
rem Match process_shaders.ps1's static -ver 20b invocation, but fail immediately
rem on compiler errors and publish catalog VCS files only, never the whole tree.
powershell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command ^
 "$ErrorActionPreference = 'Stop';" ^
 "try {" ^
 " $root = (Get-Location).Path;" ^
 " $compiler = Join-Path $root '..\..\devtools\bin\ShaderCompile2.exe';" ^
 " $target = Join-Path $env:L4D2_MOD_ROOT 'shaders\fxc';" ^
 " if (-not (Test-Path -LiteralPath (Join-Path $env:L4D2_MOD_ROOT 'gameinfo.txt') -PathType Leaf)) { throw 'The in-place mod directory must contain gameinfo.txt.'; }" ^
 " New-Item -ItemType Directory -Force -Path 'include', 'shaders\fxc', $target | Out-Null;" ^
 " foreach ($line in Get-Content -LiteralPath 'l4d2_shaders_dx9_20b.txt') {" ^
 "  $source = $line.Trim();" ^
 "  if (-not $source -or $source.StartsWith('//')) { continue; }" ^
 "  Write-Host ('Building catalog20b: ' + $source);" ^
 "  & $compiler '-ver' '20b' '-shaderpath' $root $source;" ^
 "  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE; }" ^
 "  $name = ([System.IO.Path]::GetFileNameWithoutExtension($source) -replace '_ps2x$', '_ps20b') + '.vcs';" ^
 "  Copy-Item -LiteralPath (Join-Path 'shaders\fxc' $name) -Destination (Join-Path $target $name) -Force;" ^
 " }" ^
 "} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1; }"
set "result=%errorlevel%"
if not "%result%"=="0" goto done
py -3 build_l4d2_ps20.py --source-dir "%~dp0." --output-dir "%L4D2_MOD_ROOT%\shaders\fxc"
set "result=%errorlevel%"
:done
popd
exit /b %result%
