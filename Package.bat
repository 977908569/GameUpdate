@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: Hot Update Packaging Script
:: Usage:
::   Package.bat base <version> [platform]
::   Package.bat patch <version> <base_version> [platform]
::
:: Examples:
::   Package.bat base 1.0.0
::   Package.bat patch 1.0.1 1.0.0
::   Package.bat base 1.0.0 Android
:: ============================================================

:: Configuration paths
set "UE_EDITOR=D:\UE5.7\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "UAT=D:\UE5.7\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat"
set "PROJECT=E:\Test\HotPatch\GameUpdate\GameUpdate.uproject"

:: Check parameters
if "%~1"=="" goto :show_help
if "%~2"=="" goto :show_help

set "MODE=%~1"
set "VERSION=%~2"
set "BASE_VERSION=%~3"
set "PLATFORM=%~4"

:: Default platform is Windows
if "%PLATFORM%"=="" set "PLATFORM=Windows"

:: Check mode
if /i "%MODE%"=="base" goto :check_base
if /i "%MODE%"=="patch" goto :check_patch
echo [ERROR] Unknown mode '%MODE%', supported: base or patch
exit /b 1

:check_base
if not exist "%UE_EDITOR%" (
    echo [ERROR] UnrealEditor-Cmd.exe not found
    echo Path: %UE_EDITOR%
    exit /b 1
)
if not exist "%UAT%" (
    echo [ERROR] RunUAT.bat not found
    echo Path: %UAT%
    exit /b 1
)
if not exist "%PROJECT%" (
    echo [ERROR] Project file not found
    echo Path: %PROJECT%
    exit /b 1
)
goto :start_base

:check_patch
if "%BASE_VERSION%"=="" (
    echo [ERROR] Patch requires base version
    echo Usage: Package.bat patch ^<version^> ^<base_version^> [platform]
    exit /b 1
)
if not exist "%UE_EDITOR%" (
    echo [ERROR] UnrealEditor-Cmd.exe not found
    exit /b 1
)
goto :start_patch

:: ============================================================
:: Base Package Build
:: ============================================================
:start_base
echo.
echo ============================================================
echo   Base Package Build
echo ============================================================
echo Version:  %VERSION%
echo Platform: %PLATFORM%
echo.

:: Step 1: Compile project
echo [1/2] Compiling project...
echo ------------------------------------------------------------
call "%UAT%" BuildCookRun -project="%PROJECT%" -targetplatform=Win64 -clientconfig=Development -build -noP4
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed
    exit /b 1
)
echo [OK] Compilation completed
echo.

:: Step 2: Package
echo [2/2] Building base package...
echo ------------------------------------------------------------
"%UE_EDITOR%" "%PROJECT%" -run=HotUpdate -mode=base -version=%VERSION% -platform=%PLATFORM% -skipbuild
if errorlevel 1 (
    echo.
    echo [ERROR] Base package build failed
    exit /b 1
)

echo.
echo ============================================================
echo   Base Package Build Successful!
echo ============================================================
echo Version:  %VERSION%
echo Platform: %PLATFORM%
echo Output:   Saved\HotUpdateVersions\%VERSION%\%PLATFORM%\
echo.
goto :end

:: ============================================================
:: Patch Package Build
:: ============================================================
:start_patch
echo.
echo ============================================================
echo   Patch Package Build
echo ============================================================
echo Patch Version: %VERSION%
echo Base Version:  %BASE_VERSION%
echo Platform:      %PLATFORM%
echo.

:: Check if base version exists
if not exist "Saved\HotUpdateVersions\%BASE_VERSION%" (
    echo [ERROR] Base version %BASE_VERSION% not found
    echo Run first: Package.bat base %BASE_VERSION% %PLATFORM%
    exit /b 1
)

:: Build patch package
echo Building patch package (incremental Cook)...
echo ------------------------------------------------------------
"%UE_EDITOR%" "%PROJECT%" -run=HotUpdate -mode=patch -version=%VERSION% -baseversion=%BASE_VERSION% -platform=%PLATFORM% -incrementalcook -skipbuild
if errorlevel 1 (
    echo.
    echo [ERROR] Patch package build failed
    exit /b 1
)

echo.
echo ============================================================
echo   Patch Package Build Successful!
echo ============================================================
echo Patch Version: %VERSION%
echo Base Version:  %BASE_VERSION%
echo Platform:      %PLATFORM%
echo Output:        Saved\HotUpdateVersions\%VERSION%\%PLATFORM%\
echo.
goto :end

:: ============================================================
:: Help
:: ============================================================
:show_help
echo.
echo ============================================================
echo   Hot Update Packaging Script
echo ============================================================
echo.
echo Usage:
echo   Package.bat base ^<version^> [platform]
echo   Package.bat patch ^<version^> ^<base_version^> [platform]
echo.
echo Parameters:
echo   mode          Package mode: base or patch
echo   version       Version number (e.g. 1.0.0)
echo   base_version  Base version (patch mode only)
echo   platform      Target platform (default: Windows)
echo                 Support: Windows, Android, IOS
echo.
echo Examples:
echo   Package.bat base 1.0.0
echo   Package.bat base 1.0.0 Android
echo   Package.bat patch 1.0.1 1.0.0
echo   Package.bat patch 1.0.1 1.0.0 Android
echo.
echo Notes:
echo   - Base package compiles project first, then packages
echo   - Patch package uses incremental Cook, only processes changed assets
echo   - Output directory: Saved\HotUpdateVersions\^<version^>\^<platform^>\
echo.
goto :end

:end
endlocal
