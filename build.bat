@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this script from "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)
where ml64 >nul 2>nul
if errorlevel 1 (
    echo ML64 was not found in PATH.
    exit /b 1
)
if not exist dist mkdir dist
ml64 /c /nologo /Fo"dist\uwfix_hook.obj" "src\uwfix_hook.asm"
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /W3 "src\uwfix.c" "dist\uwfix_hook.obj" /link /DLL /DEF:"src\nbamUsbFinder.def" /OUT:"dist\nbamUsbFinder.dll" user32.lib
if errorlevel 1 exit /b 1
copy /y "src\uwfix.ini" "dist\uwfix.ini" >nul
echo BUILT dist\nbamUsbFinder.dll
