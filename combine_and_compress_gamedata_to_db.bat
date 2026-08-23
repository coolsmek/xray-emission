@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  combine_and_compress_gamedata_to_db.bat
REM  1. Merge gamedata_vk\ into gamedata\ (with conflict handling)
REM  2. Compress merged gamedata into 000_modded_exes_gamedata.db0
REM ============================================================

set "ROOT=%~dp0"
set "BASE_DIR=%ROOT%gamedata"
set "OVERLAY_DIR=%ROOT%gamedata_vk"
set "COMPRESSOR_DIR=%ROOT%compressor"
set "OUTPUT_DIR=%ROOT%compressor_output"
set "STAGE_DIR=%COMPRESSOR_DIR%\gamedata"
set "OUTPUT_NAME=000_modded_exes_gamedata.db0"

REM Conflict resolution policy for the whole run:
REM   (empty)   = ask for every conflict
REM   overlay   = always take gamedata_vk version
REM   base      = always keep gamedata version
REM   skip      = never overwrite (same as base, but no diff shown)
set "GLOBAL_POLICY="

echo ========================================================
echo  Gamedata Merge + Compress Tool
echo ========================================================
echo Base    (gamedata)    : %BASE_DIR%
echo Overlay (gamedata_vk) : %OVERLAY_DIR%
echo Output                : %OUTPUT_DIR%\%OUTPUT_NAME%
echo.

REM ---- Sanity checks -------------------------------------
if not exist "%BASE_DIR%" (
    echo [ERROR] Base gamedata folder not found: %BASE_DIR%
    pause & exit /b 1
)
if not exist "%OVERLAY_DIR%" (
    echo [ERROR] Overlay gamedata_vk folder not found: %OVERLAY_DIR%
    pause & exit /b 1
)
if not exist "%COMPRESSOR_DIR%\xrCompress.exe" (
    echo [ERROR] xrCompress.exe not found in: %COMPRESSOR_DIR%
    pause & exit /b 1
)
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo --------------------------------------------------------
echo Step 0: Creating throwaway copy of gamedata ...
echo --------------------------------------------------------
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
xcopy "%BASE_DIR%" "%STAGE_DIR%\" /E /I /Y /Q >nul
echo [INFO] Working copy: %STAGE_DIR%
echo.

echo --------------------------------------------------------
echo Step 1: Merging gamedata_vk into gamedata ...
echo --------------------------------------------------------
echo.

set /a MERGED_NEW=0
set /a MERGED_OVERWRITE=0
set /a MERGED_SKIP=0

REM Walk every file in the overlay folder
for /f "delims=" %%F in ('dir /b /s /a-d "%OVERLAY_DIR%"') do (
    set "SRC=%%F"
    set "REL=%%F"
    set "REL=!REL:%OVERLAY_DIR%\=!"
    set "DST=%STAGE_DIR%\!REL!"

    if not exist "!DST!" (
        REM ---- New file: just copy ----
        for %%D in ("!DST!") do if not exist "%%~dpD" mkdir "%%~dpD"
        copy /Y "!SRC!" "!DST!" >nul
        echo [NEW]      !REL!
        set /a MERGED_NEW+=1
    ) else (
        REM ---- Conflict: file exists in both ----
        call :HANDLE_CONFLICT "!REL!" "!SRC!" "!DST!"
    )
)

echo.
echo Merge summary: !MERGED_NEW! new, !MERGED_OVERWRITE! overwritten, !MERGED_SKIP! kept.
echo.

echo --------------------------------------------------------
echo Step 2: Compressing to %OUTPUT_NAME% ...
echo --------------------------------------------------------
pushd "%COMPRESSOR_DIR%"
xrCompress.exe gamedata -ltx build_patch.ltx -pack -strong -1024 -db
popd

REM xrCompress creates gamedata.db0 in the compressor folder
if exist "%COMPRESSOR_DIR%\gamedata.db0" (
    if exist "%OUTPUT_DIR%\%OUTPUT_NAME%" del /q "%OUTPUT_DIR%\%OUTPUT_NAME%"
    move /Y "%COMPRESSOR_DIR%\gamedata.db0" "%OUTPUT_DIR%\%OUTPUT_NAME%" >nul
    echo [OK] Created: %OUTPUT_DIR%\%OUTPUT_NAME%
) else (
    echo [ERROR] Expected gamedata.db0 was not produced. Check compressor output above.
    goto :CLEANUP
)

:CLEANUP
echo.
echo [INFO] Cleaning up staging folder ...
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"

echo.
echo ========================================================
echo  Done!
echo ========================================================
pause
exit /b 0

REM ============================================================
REM  :HANDLE_CONFLICT  <relpath>  <src>  <dst>
REM ============================================================
:HANDLE_CONFLICT
set "C_REL=%~1"
set "C_SRC=%~2"
set "C_DST=%~3"

REM Apply a global policy if one is set
if /i "%GLOBAL_POLICY%"=="overlay" goto :CONF_TAKE_OVERLAY
if /i "%GLOBAL_POLICY%"=="base"    goto :CONF_KEEP_BASE
if /i "%GLOBAL_POLICY%"=="skip"    goto :CONF_KEEP_BASE

REM Quick identical-check: if files are the same, no conflict
fc /b "%C_SRC%" "%C_DST%" >nul 2>&1
if not errorlevel 1 (
    echo [SAME]     %C_REL%  (identical, skipped)
    set /a MERGED_SKIP+=1
    goto :eof
)

:CONF_ASK
echo.
echo -------------------------------------------------------
echo [CONFLICT] %C_REL%
echo   Overlay (gamedata_vk): %C_SRC%
echo   Base    (gamedata)   : %C_DST%
echo -------------------------------------------------------
echo   [O] Take gamedata_vk version (overwrite)
echo   [B] Keep gamedata version    (skip)
echo   [D] Show diff (fc) then ask again
echo   [OA] Take gamedata_vk for ALL remaining conflicts
echo   [BA] Keep gamedata for ALL remaining conflicts
echo   [Q] Abort merge
set "CHOICE="
set /p "CHOICE=Your choice: "

if /i "%CHOICE%"=="O"  goto :CONF_TAKE_OVERLAY
if /i "%CHOICE%"=="B"  goto :CONF_KEEP_BASE
if /i "%CHOICE%"=="D"  goto :CONF_DIFF
if /i "%CHOICE%"=="OA" ( set "GLOBAL_POLICY=overlay" & goto :CONF_TAKE_OVERLAY )
if /i "%CHOICE%"=="BA" ( set "GLOBAL_POLICY=base"    & goto :CONF_KEEP_BASE )
if /i "%CHOICE%"=="Q"  ( echo [ABORTED] Merge cancelled by user. & pause & exit /b 2 )
echo Invalid choice, try again.
goto :CONF_ASK

:CONF_DIFF
echo.
echo ===================== DIFF (fc) =======================
fc "%C_DST%" "%C_SRC%"
echo =======================================================
goto :CONF_ASK

:CONF_TAKE_OVERLAY
copy /Y "%C_SRC%" "%C_DST%" >nul
echo [OVERWRITE] %C_REL%
set /a MERGED_OVERWRITE+=1
goto :eof

:CONF_KEEP_BASE
echo [KEEP-BASE] %C_REL%
set /a MERGED_SKIP+=1
goto :eof