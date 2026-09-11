@echo off
setlocal
cd /d "%~dp0"
 
echo Starting rebuild script...
 
echo ----- Cleaning up build files -----
call "%~dp0clean.bat"
if errorlevel 1 (
    echo Clean step failed with error code %errorlevel%.
    exit /b %errorlevel%
)
 
echo ----- Building up build files -----
call "%~dp0run.bat"
if errorlevel 1 (
    echo Build step failed with error code %errorlevel%.
    exit /b %errorlevel%
)
 
echo All scripts finished!
endlocal