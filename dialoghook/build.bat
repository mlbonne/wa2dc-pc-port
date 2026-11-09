@echo off
rem Builds the version.dll proxy / Unity dialog Saves-tab hook (x86, like the Unity 3.5 player).
setlocal
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VCVARS% set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x86 >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /MT /EHsc /W3 /utf-8 /std:c++17 /DUNICODE /D_UNICODE /LD ^
   dialoghook.cpp /Fe"out\version.dll" /Foout\ ^
   /link /DEF:dialoghook.def user32.lib gdi32.lib comctl32.lib shlwapi.lib advapi32.lib || exit /b 1
echo built
