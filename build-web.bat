@echo off
rem WebAssembly build (emsdk at C:\Tools\emsdk).  Output: build-web\yogho.js/.wasm(/.data)
rem Pass the folder holding YOGHO.EXE + YOGHODAT.DAT as %1 to bundle it for LOCAL testing only.
call C:\Tools\emsdk\emsdk_env.bat >nul 2>&1
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "%~dp0"
set PRE=
if not "%~1"=="" set PRE=-DYOGHO_PRELOAD_DIR=%~1
if not exist build-web call emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release %PRE% || exit /b 1
cmake --build build-web || exit /b 1
