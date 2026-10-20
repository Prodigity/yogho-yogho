@echo off
rem Compile-only check of the given source files (or all of src\*.c): check.bat src\foo.c src\bar.c
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build\check mkdir build\check
set FILES=%*
if "%FILES%"=="" set FILES=src\*.c
cl /nologo /c /W3 /std:c11 /D_CRT_SECURE_NO_WARNINGS /Isrc /Ithird_party\nuked-opl3 /IC:\Tools\SDL2\SDL2-2.32.10\include /Fobuild\check\ %FILES%
