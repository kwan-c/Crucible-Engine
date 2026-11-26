
@echo on
setlocal

REM #------------#
REM Configuration
REM #------------#
set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%build
set LOCAL_CMAKE_DIR=%PROJECT_DIR%cmake

REM #------------#
REM Destory build folder
REM #------------#
if exist "%BUILD_DIR%" (
    echo Deleting build folder
    rmdir /s /q "%BUILD_DIR%"
)
if exist "%BUILD_DIR%" goto :DELETE_FAILED

REM #------------#
REM Remove local CMake (if still not deleted by run.bat
REM #------------#
if exist "%LOCAL_CMAKE_DIR%" rmdir /s /q "%LOCAL_CMAKE_DIR%"

exit /b 0

:DELETE_FAILED
echo ERROR: Could not delete the build folder. Something still has files in it open.
echo Close Visual Studio and the Editor, then run this again.
pause
exit /b 1
