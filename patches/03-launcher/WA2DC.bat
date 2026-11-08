@echo off
rem White Album 2 Dream Communication - PC port launcher.
rem -force-opengl avoids the D3D9 letterbox/blend artifacts on this Unity 3.5 build.
cd /d "%~dp0"
start "" "WA2DC.exe" -force-opengl %*
