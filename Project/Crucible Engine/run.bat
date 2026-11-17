REM # ------------------------------------------------------------ #
REM # run.bat file for Crucible Engine
REM # Team: Medium Well
REM # Module: GAM_300
REM # ------------------------------------------------------------ #
REM # Usage:
REM #   run.bat        Generate for the newest Visual Studio installed
REM #                  (or keep the one the Build folder already uses)
REM #   run.bat 2022   Generate for Visual Studio 2022
REM #   run.bat 2026   Generate for Visual Studio 2026
REM # ------------------------------------------------------------ #
@echo on
setlocal

REM The comment indicator is REM

REM Work from the folder this file is in, so the relative paths below line up
cd /d "%~dp0"

REM #------------#
REM Var Names
REM #------------#
set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%Build
set LOCAL_CMAKE_DIR=%PROJECT_DIR%cmake
set CMAKE_EXE=
set VS_YEAR=
set VS_INSTALL_DIR=
set CACHED_GENERATOR=
set CACHED_INSTANCE=
set CACHED_HOME=
set STALE_REASON=
set INSTANCE_ARG=
set SOLUTION_FILE=
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

REM ============================================
REM Pick which Visual Studio to generate for
REM ============================================
REM 1) A year typed after run.bat wins
if "%~1"=="2022" set VS_YEAR=2022
if "%~1"=="2026" set VS_YEAR=2026
if not "%~1"=="" if not defined VS_YEAR goto :BAD_ARGUMENT

REM 2) Otherwise keep the generator an existing Build folder was made with,
REM    because CMake cannot switch generators inside one build folder
if not exist "%BUILD_DIR%\CMakeCache.txt" goto :CACHE_CHECKED
for /f "tokens=1* delims==" %%g in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHED_GENERATOR=%%h"
for /f "tokens=1* delims==" %%g in ('findstr /b /c:"CMAKE_GENERATOR_INSTANCE:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHED_INSTANCE=%%h"
for /f "tokens=1* delims==" %%g in ('findstr /b /c:"CMAKE_HOME_DIRECTORY:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHED_HOME=%%h"

REM    ...but only if that Build folder really belongs to this PC and this folder.
REM    A Build folder synced from another PC (OneDrive) or left over from a
REM    Visual Studio that is not installed here is deleted and made again.
REM    CMake writes paths with / so turn them into \ before comparing
if defined CACHED_INSTANCE set "CACHED_INSTANCE=%CACHED_INSTANCE:/=\%"
if defined CACHED_HOME set "CACHED_HOME=%CACHED_HOME:/=\%"
if defined CACHED_HOME if /i not "%CACHED_HOME%"=="%PROJECT_DIR:~0,-1%" set "STALE_REASON=it was generated in another folder or on another PC"
if defined CACHED_INSTANCE if not exist "%CACHED_INSTANCE%\" set "STALE_REASON=the Visual Studio it was made for is not installed on this PC"
if defined STALE_REASON goto :STALE_BUILD
:CACHE_CHECKED
if not defined VS_YEAR if "%CACHED_GENERATOR%"=="Visual Studio 17 2022" set VS_YEAR=2022
if not defined VS_YEAR if "%CACHED_GENERATOR%"=="Visual Studio 18 2026" set VS_YEAR=2026
if defined VS_YEAR if "%~1"=="" echo Keeping %CACHED_GENERATOR% from the existing Build folder. To switch, run clean.bat, then run.bat 2022 or run.bat 2026.

REM 3) Otherwise use the newest Visual Studio that has the C++ tools installed
if not defined VS_YEAR (
    call :FIND_VS "[18.0,19.0)"
    if defined VS_INSTALL_DIR set VS_YEAR=2026
)
if not defined VS_YEAR (
    call :FIND_VS "[17.0,18.0)"
    if defined VS_INSTALL_DIR set VS_YEAR=2022
)

REM 4) Could not tell: assume Visual Studio 2022, as this script always did
if not defined VS_YEAR (
    echo WARNING: Could not detect Visual Studio, assuming Visual Studio 2022.
    set VS_YEAR=2022
)

REM #------------#
REM Settings for each Visual Studio
REM CMake 4.2 is the first version that knows Visual Studio 2026
REM CMAKE_VERSION is the version downloaded when no usable CMake is found
REM #------------#
if "%VS_YEAR%"=="2026" (
    set "GENERATOR=Visual Studio 18 2026"
    set "VS_RANGE=[18.0,19.0)"
    set MIN_CMAKE_TEXT=4.2
    set MIN_CMAKE_NUM=402
    set CMAKE_VERSION=4.4.4
) else (
    set "GENERATOR=Visual Studio 17 2022"
    set "VS_RANGE=[17.0,18.0)"
    set MIN_CMAKE_TEXT=3.23
    set MIN_CMAKE_NUM=323
    set CMAKE_VERSION=3.30.3
)
echo Generating for %GENERATOR%
if defined CACHED_GENERATOR if not "%CACHED_GENERATOR%"=="%GENERATOR%" goto :GENERATOR_MISMATCH

REM Where that Visual Studio is installed (to find the CMake it ships with)
call :FIND_VS "%VS_RANGE%"
if exist "%VSWHERE%" if not defined VS_INSTALL_DIR echo WARNING: Visual Studio %VS_YEAR% with the C++ tools was not found on this PC. CMake will probably fail below.

REM Tell CMake to use exactly that install on a fresh Build folder
REM (an existing Build folder already remembers its own)
if defined VS_INSTALL_DIR if not exist "%BUILD_DIR%\CMakeCache.txt" set INSTANCE_ARG=-DCMAKE_GENERATOR_INSTANCE:PATH="%VS_INSTALL_DIR:\=/%"

REM ============================================
REM Find a CMake that is new enough
REM ============================================
REM 1) CMake on the PATH
for /f "delims=" %%i in ('where cmake 2^>nul') do (
    if not defined CMAKE_EXE call :TRY_CMAKE "%%i"
)

REM 2) The CMake that comes with Visual Studio
if not defined CMAKE_EXE if defined VS_INSTALL_DIR call :TRY_CMAKE "%VS_INSTALL_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

REM 3) A copy this script downloaded before
set "LOCAL_CMAKE_EXE=%LOCAL_CMAKE_DIR%\cmake-%CMAKE_VERSION%-windows-x86_64\bin\cmake.exe"
if not defined CMAKE_EXE call :TRY_CMAKE "%LOCAL_CMAKE_EXE%"
if defined CMAKE_EXE goto :FOUND_CMAKE

REM #------------#
REM 4) Download CMake (locally) into the cmake folder
REM #------------#
echo No suitable CMake found, downloading CMake %CMAKE_VERSION%...
if not exist "%LOCAL_CMAKE_DIR%" mkdir "%LOCAL_CMAKE_DIR%"
REM Get Cmake from github repo
powershell -NoProfile -Command ^
    "$ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest https://github.com/Kitware/CMake/releases/download/v%CMAKE_VERSION%/cmake-%CMAKE_VERSION%-windows-x86_64.zip -OutFile cmake.zip"
if errorlevel 1 (
    echo ERROR: Failed to download CMake.
	REM Pause here so that can see error log
	pause
    exit /b 1
)
powershell -NoProfile -Command ^
    "Expand-Archive cmake.zip -DestinationPath cmake -Force"
del cmake.zip
call :TRY_CMAKE "%LOCAL_CMAKE_EXE%"
if not defined CMAKE_EXE (
    echo ERROR: The downloaded CMake could not be used.
    pause
    exit /b 1
)

:FOUND_CMAKE
REM output which one it using
echo Using CMake: %CMAKE_EXE%

REM #------------#
REM Create empty build folder if not exist :D
REM #------------#
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

REM #------------#
REM Run CMake File
REM #------------#
echo Running CMake ..
"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64 %INSTANCE_ARG%
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed with code %errorlevel%.
    pause
    exit /b %errorlevel%
)
REM uncommment these to debug (you cant see them normally anyways so no point
REM echo Project generated successfully in %BUILD_DIR%.
REM pause

REM Say which solution to open (2026 writes Crucible.slnx, 2022 writes Crucible.sln)
if exist "%BUILD_DIR%\Crucible.sln" set "SOLUTION_FILE=Crucible.sln"
if exist "%BUILD_DIR%\Crucible.slnx" if "%VS_YEAR%"=="2026" set "SOLUTION_FILE=Crucible.slnx"
if not defined SOLUTION_FILE if exist "%BUILD_DIR%\Crucible.slnx" set "SOLUTION_FILE=Crucible.slnx"
if defined SOLUTION_FILE echo Done. Open Build\%SOLUTION_FILE% with Visual Studio %VS_YEAR%.
exit /b 0


REM ============================================
REM Helpers (only reached through goto or call)
REM ============================================

:BAD_ARGUMENT
echo ERROR: Unknown option "%~1". Use run.bat, run.bat 2022 or run.bat 2026.
pause
exit /b 1

:STALE_BUILD
echo The Build folder cannot be reused: %STALE_REASON%.
echo Deleting it to generate from scratch...
rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" goto :WIPE_FAILED
set CACHED_GENERATOR=
goto :CACHE_CHECKED

:WIPE_FAILED
echo ERROR: Could not delete the Build folder. Close Visual Studio and the Editor, then run this again.
pause
exit /b 1

:GENERATOR_MISMATCH
echo ERROR: The Build folder was made for %CACHED_GENERATOR%, not %GENERATOR%.
echo CMake cannot switch generators in the same folder.
echo Run clean.bat first, then run.bat %VS_YEAR% again.
pause
exit /b 1

:FIND_VS
REM Sets VS_INSTALL_DIR to a Visual Studio install in the version range %1
REM that has the C++ x64 tools, or leaves it empty if there is none
set VS_INSTALL_DIR=
if not exist "%VSWHERE%" goto :EOF
for /f "delims=" %%p in ('call "%VSWHERE%" -products * -version %1 -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS_INSTALL_DIR=%%p"
goto :EOF

:TRY_CMAKE
REM Sets CMAKE_EXE to %1 if that cmake.exe exists and is at least MIN_CMAKE_TEXT
if not exist "%~1" goto :EOF
set FOUND_CMAKE_VERSION=
for /f "tokens=3" %%v in ('call "%~1" --version 2^>nul') do if not defined FOUND_CMAKE_VERSION set "FOUND_CMAKE_VERSION=%%v"
if not defined FOUND_CMAKE_VERSION goto :EOF
for /f "tokens=1,2 delims=.-" %%a in ("%FOUND_CMAKE_VERSION%") do set /a FOUND_CMAKE_NUM=%%a*100+%%b
if %FOUND_CMAKE_NUM% LSS %MIN_CMAKE_NUM% goto :TRY_CMAKE_TOO_OLD
set "CMAKE_EXE=%~1"
goto :EOF
:TRY_CMAKE_TOO_OLD
echo Skipping "%~1": CMake %FOUND_CMAKE_VERSION% is older than %MIN_CMAKE_TEXT%, which %GENERATOR% needs.
goto :EOF
