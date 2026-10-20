@echo off
rem Desktop build (MSVC Build Tools 2022 + Ninja).  Output: build\yogho.exe
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "%~dp0"
if not exist build cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build || exit /b 1
