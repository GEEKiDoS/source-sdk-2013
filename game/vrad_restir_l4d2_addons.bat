@echo off
rem Bakes enhanced lighting for every map inside L4D2 add-on VPKs (see vrad_restir_l4d2_addons.ps1).
rem   vrad_restir_l4d2_addons.bat [vrad_restir options] [-map name ...] [addon.vpk ...]
rem -map limits the bake to the named maps. Without .vpk arguments every mod_l4d2\addons\*.vpk is baked. Options go to vrad_restir_l4d2.bat, whose defaults
rem apply to every option not given.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vrad_restir_l4d2_addons.ps1" %*
exit /b %ERRORLEVEL%
