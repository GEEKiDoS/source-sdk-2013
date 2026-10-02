@echo off
rem Starts Half-Life 2: Episode Two (mod_episodic). Extra arguments are passed through, e.g. -dx9 or +map ep2_outland_01.
cd /d "%~dp0"
start "" mod_episodic_win64.exe -game mod_episodic -windowed -w 1600 -h 900 -novid -console +r_dlss_nr_layers 1 +fps_max 60 %*
