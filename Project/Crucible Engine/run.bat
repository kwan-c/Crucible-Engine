REM # ------------------------------------------------------------ #
REM # run.bat file for Crucible Engine
REM # Team: Medium Well
REM # Module: GAM_300
REM # ------------------------------------------------------------ #
@echo on
setlocal

REM The comment indicator is REM

REM #------------#
REM Var Names
REM #------------#
set CMAKE_VERSION=3.30.3
set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%Build
set LOCAL_CMAKE_DIR=%PROJECT_DIR%cmake
set CMAKE_EXE=

REM ============================================
REM Try system CMake first
REM ============================================
where cmake >nul 2>nul
if %errorlevel%==0 (
    for /f "delims=" %%i in ('where cmake') do (
        set CMAKE_EXE=%%i
        goto :FOUND_CMAKE
    )
)

REM #------------#
REM Check for CMake (locally)
REM Install as neccssary
REM #------------#

echo System CMake not found, downloading local copy...
if not exist "%LOCAL_CMAKE_DIR%\bin\cmake.exe" (
    REM Get Cmake from github repo
    powershell -Command ^
        "Invoke-WebRequest https://github.com/Kitware/CMake/releases/download/v%CMAKE_VERSION%/cmake-%CMAKE_VERSION%-windows-x86_64.zip -OutFile cmake.zip"
    if errorlevel 1 (
        echo ERROR: Failed to download CMake.
		REM Pause here so that can see error log
		pause
        exit /b 1
    )
    powershell -Command ^
        "Expand-Archive cmake.zip -DestinationPath . -Force"
    move cmake-%CMAKE_VERSION%-windows-x86_64 cmake
    del cmake.zip
)
set CMAKE_EXE=%LOCAL_CMAKE_DIR%\bin\cmake.exe

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
"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed with code %errorlevel%.
    pause
    exit /b %errorlevel%
)
REM uncommment these to debug (you cant see them normally anyways so no point
REM echo Project generated successfully in %BUILD_DIR%.
REM pause
exit /b 0
