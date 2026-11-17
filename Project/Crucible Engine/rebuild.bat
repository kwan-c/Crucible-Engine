@echo off
setlocal
cd /d "%~dp0"

REM Usage:
REM   rebuild.bat        Clean, then generate for the same Visual Studio as before
REM   rebuild.bat 2022   Clean, then generate for Visual Studio 2022
REM   rebuild.bat 2026   Clean, then generate for Visual Studio 2026

echo Starting rebuild script...

REM Remember which Visual Studio the Build folder uses before it is deleted,
REM so a rebuild does not silently switch to another one
set VS_YEAR=%~1
set "CACHE_FILE=%~dp0Build\CMakeCache.txt"
if not defined VS_YEAR if exist "%CACHE_FILE%" findstr /b /c:"CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022" "%CACHE_FILE%" >nul && set VS_YEAR=2022
if not defined VS_YEAR if exist "%CACHE_FILE%" findstr /b /c:"CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026" "%CACHE_FILE%" >nul && set VS_YEAR=2026

echo ----- Cleaning up build files -----
call "%~dp0clean.bat"
if errorlevel 1 (
    echo Clean step failed with error code %errorlevel%.
    exit /b %errorlevel%
)

echo ----- Building up build files -----
call "%~dp0run.bat" %VS_YEAR%
if errorlevel 1 (
    echo Build step failed with error code %errorlevel%.
    exit /b %errorlevel%
)

echo All scripts finished!
endlocal
