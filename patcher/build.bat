@echo off
rem Builds the WA2DC patcher (x64) with the Visual Studio Build Tools.
rem The builder supplies game.ico (extracted from the XAPK) next to this file.
rem liblzma (xz 5.6.3, 0BSD) is vendored unmodified under third_party\xz.
setlocal
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VCVARS% set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x64 >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out

set "XZ=third_party\xz\src"
set "LZMA_DEFS=/DWIN32 /D_WINDOWS /DNDEBUG /DHAVE_CHECK_CRC32 /DHAVE_CHECK_CRC64 /DHAVE_CHECK_SHA256 /DHAVE_DECODERS /DHAVE_DECODER_ARM /DHAVE_DECODER_ARM64 /DHAVE_DECODER_ARMTHUMB /DHAVE_DECODER_DELTA /DHAVE_DECODER_IA64 /DHAVE_DECODER_LZMA1 /DHAVE_DECODER_LZMA2 /DHAVE_DECODER_POWERPC /DHAVE_DECODER_RISCV /DHAVE_DECODER_SPARC /DHAVE_DECODER_X86 /DHAVE_ENCODERS /DHAVE_ENCODER_ARM /DHAVE_ENCODER_ARM64 /DHAVE_ENCODER_ARMTHUMB /DHAVE_ENCODER_DELTA /DHAVE_ENCODER_IA64 /DHAVE_ENCODER_LZMA1 /DHAVE_ENCODER_LZMA2 /DHAVE_ENCODER_POWERPC /DHAVE_ENCODER_RISCV /DHAVE_ENCODER_SPARC /DHAVE_ENCODER_X86 /DHAVE_INTTYPES_H /DHAVE_LZIP_DECODER /DHAVE_MF_BT2 /DHAVE_MF_BT3 /DHAVE_MF_BT4 /DHAVE_MF_HC3 /DHAVE_MF_HC4 /DHAVE_STDBOOL_H /DHAVE_STDINT_H /DHAVE_VISIBILITY=0 /DHAVE__BOOL /DMYTHREAD_VISTA /DTUKLIB_FAST_UNALIGNED_ACCESS /DTUKLIB_SYMBOL_PREFIX=lzma_"
set "LZMA_INC=/I%XZ%\liblzma\api /I%XZ%\liblzma\common /I%XZ%\liblzma\check /I%XZ%\liblzma\lz /I%XZ%\liblzma\rangecoder /I%XZ%\liblzma\lzma /I%XZ%\liblzma\delta /I%XZ%\liblzma\simple /I%XZ%\common"

if not exist out\lzma mkdir out\lzma
for /r "%XZ%\liblzma" %%f in (*.c) do (
    if /I not "%%~nxf"=="crc32_small.c" if /I not "%%~nxf"=="crc64_small.c" if /I not "%%~nxf"=="crc32_tablegen.c" if /I not "%%~nxf"=="crc64_tablegen.c" if /I not "%%~nxf"=="fastpos_tablegen.c" if /I not "%%~nxf"=="price_tablegen.c" (
        cl /nologo /c /O2 /MT /std:c11 %LZMA_DEFS% %LZMA_INC% "%%f" /Fo"out\lzma\%%~nf.obj" || exit /b 1
    )
)
cl /nologo /c /O2 /MT /std:c11 %LZMA_DEFS% %LZMA_INC% "%XZ%\common\tuklib_physmem.c" /Fo"out\lzma\tuklib_physmem.obj" || exit /b 1
cl /nologo /c /O2 /MT /std:c11 %LZMA_DEFS% %LZMA_INC% "%XZ%\common\tuklib_cpucores.c" /Fo"out\lzma\tuklib_cpucores.obj" || exit /b 1
lib /nologo /out:out\lzma.lib out\lzma\*.obj || exit /b 1

rc /nologo /fo out\patcher.res patcher.rc || exit /b 1
cl /nologo /O2 /MT /EHsc /W3 /utf-8 /std:c++17 /DUNICODE /D_UNICODE %LZMA_INC% ^
   patcher.cpp bundle_lzma.cpp third_party\miniz\miniz.c third_party\miniz\miniz_tdef.c ^
   third_party\miniz\miniz_tinfl.c third_party\miniz\miniz_zip.c out\patcher.res out\lzma.lib ^
   /Foout\ /Fe"out\WA2DC Patcher.exe" ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib ole32.lib shell32.lib ^
   shlwapi.lib cabinet.lib msdelta.lib winmm.lib uuid.lib || exit /b 1
echo built
