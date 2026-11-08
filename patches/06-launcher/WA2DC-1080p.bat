@echo off
rem White Album 2 Dream Communication - 1920x1080 exclusive fullscreen option.
rem Sets the player's saved display mode, then launches. The Unity configuration
rem dialog still appears; click Play. Use WA2DC.bat for the default 1024x768 window.
set KEY=HKCU\Software\AQUAPLUS\WA2DC
reg add "%KEY%" /v "Screenmanager Resolution Width_h182942802" /t REG_DWORD /d 1920 /f >nul
reg add "%KEY%" /v "Screenmanager Resolution Height_h2627697771" /t REG_DWORD /d 1080 /f >nul
reg add "%KEY%" /v "Screenmanager Is Fullscreen mode_h3981298716" /t REG_DWORD /d 1 /f >nul
cd /d "%~dp0"
start "" "WA2DC.exe" -force-opengl %*
