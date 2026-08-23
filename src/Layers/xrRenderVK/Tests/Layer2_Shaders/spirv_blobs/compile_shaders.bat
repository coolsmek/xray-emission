@echo off
REM compile_shaders.bat — One-time SPIR-V blob generation for Layer 2 tests
REM Run this once to generate white_triangle_vert.h and white_triangle_frag.h
REM Prerequisites:
REM   - Windows SDK with DirectXShaderCompiler (dxc.exe)
REM   - xxd (available in Git Bash or WSL)

setlocal enabledelayedexpansion

echo.
echo ============================================================================
echo xrRenderVK Layer 2 SPIR-V Blob Generator
echo ============================================================================
echo.

REM Try to find dxc.exe — common installation paths
set "DXC_PATHS=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\dxc.exe"
set "DXC_PATHS=!DXC_PATHS! C:\Program Files\Windows Kits\10\bin\10.0.22000.0\x64\dxc.exe"
set "DXC_PATHS=!DXC_PATHS! C:\Program Files\DirectXShaderCompiler\bin\dxc.exe"

set DXC_FOUND=0
for %%P in (!DXC_PATHS!) do (
    if exist "%%P" (
        set "DXC=%%P"
        set DXC_FOUND=1
        goto :found_dxc
    )
)

:found_dxc
if !DXC_FOUND! == 0 (
    echo ERROR: DirectXShaderCompiler (dxc.exe) not found
    echo.
    echo To install:
    echo   1. Open Visual Studio Installer
    echo   2. Select Windows 11 SDK (or 10)
    echo   3. Check "C++ Build Tools"
    echo.
    echo OR manually set DXC path in this script.
    exit /b 1
)

echo Found DXC at: !DXC!
echo.

REM Try to find xxd — available in Git Bash or WSL
where xxd >nul 2>&1
if errorlevel 1 (
    echo ERROR: xxd not found in PATH
    echo.
    echo To install:
    echo   - Git Bash: included with Git for Windows
    echo   - WSL: install vim or bsdmainutils
    echo.
    echo Then add Git Bash bin to PATH, or run from Git Bash:
    echo   bash compile_shaders.bat
    exit /b 1
)

echo Found xxd in PATH
echo.
echo Compiling HLSL shaders to SPIR-V ...
echo.

if not exist "white_triangle.vert.hlsl" (
    echo ERROR: white_triangle.vert.hlsl not found in current directory
    exit /b 1
)

if not exist "white_triangle.frag.hlsl" (
    echo ERROR: white_triangle.frag.hlsl not found in current directory
    exit /b 1
)

echo [1/4] Compiling white_triangle.vert.hlsl ...
"!DXC!" -T vs_6_0 -E main -spirv -Fo white_triangle.vert.spv white_triangle.vert.hlsl
if errorlevel 1 (
    echo ERROR: Vertex shader compilation failed
    exit /b 1
)

echo [2/4] Compiling white_triangle.frag.hlsl ...
"!DXC!" -T ps_6_0 -E main -spirv -Fo white_triangle.frag.spv white_triangle.frag.hlsl
if errorlevel 1 (
    echo ERROR: Fragment shader compilation failed
    exit /b 1
)

echo [3/4] Generating white_triangle_vert.h ...
xxd -i white_triangle.vert.spv > white_triangle_vert.h
if errorlevel 1 (
    echo ERROR: xxd failed on vertex blob
    exit /b 1
)

echo [4/4] Generating white_triangle_frag.h ...
xxd -i white_triangle.frag.spv > white_triangle_frag.h
if errorlevel 1 (
    echo ERROR: xxd failed on fragment blob
    exit /b 1
)

echo.
echo ============================================================================
echo SUCCESS!
echo ============================================================================
echo.
echo Generated files:
echo   - white_triangle_vert.h  (embedded in test_ShaderReflection.cpp)
echo   - white_triangle_frag.h  (embedded in test_ShaderReflection.cpp)
echo.
echo These headers are already included by the test files and are ready for use.
echo You can safely commit these files to version control.
echo.
echo Verify the blobs are valid SPIR-V (first 4 bytes should be 03 02 23 07):
echo   - white_triangle.vert.spv
echo   - white_triangle.frag.spv
echo.
endlocal

