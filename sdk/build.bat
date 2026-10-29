@echo off
setlocal EnableExtensions

rem Build the application in apps from any working directory.
set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "ADF_PATH=%ROOT_DIR%"
set "IDF_PATH=%ROOT_DIR%\esp-idf"
set "APP_DIR=%ROOT_DIR%\apps"
set "BUILD_EXIT_CODE="
set "FLASH_MONITOR_EXIT_CODE="
set "EXIT_CODE="

if not exist "%ADF_PATH%\export.bat" (
    echo ERROR: Cannot find the repository export script: "%ADF_PATH%\export.bat"
    exit /b 1
)
if not exist "%APP_DIR%\CMakeLists.txt" (
    echo ERROR: Cannot find the apps project at "%APP_DIR%"
    exit /b 1
)
if not exist "%IDF_PATH%\tools\idf.py" (
    echo ERROR: ESP-IDF is not initialized at "%IDF_PATH%"
    echo Run install.bat first.
    exit /b 1
)
if not exist "%ROOT_DIR%\qumios-dev\components\qm_agent\include\qm_agent.h" (
    echo ERROR: qumios-dev is not initialized.
    echo Run install.bat first.
    exit /b 1
)

where.exe git.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: git.exe was not found in PATH.
    exit /b 1
)
python.exe --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: python.exe was not found in PATH.
    echo Run install.bat from a terminal with Python available.
    exit /b 1
)

echo.
echo [1/3] Exporting ESP-IDF and applying ADF patches...
call "%ADF_PATH%\export.bat"
if errorlevel 1 goto :failed
if not defined IDF_PYTHON_ENV_PATH (
    echo ERROR: ESP-IDF Python environment was not exported.
    echo Run install.bat again, then retry build.bat.
    set "BUILD_EXIT_CODE=1"
    goto :failed
)
set "IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe"
if not exist "%IDF_PYTHON%" (
    echo ERROR: ESP-IDF Python was not found: "%IDF_PYTHON%"
    echo Run install.bat again, then retry build.bat.
    set "BUILD_EXIT_CODE=1"
    goto :failed
)

echo.
echo [2/3] Building the apps project...
pushd "%APP_DIR%"
if errorlevel 1 goto :failed
"%IDF_PYTHON%" "%IDF_PATH%\tools\idf.py" build
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
if not "%BUILD_EXIT_CODE%"=="0" (
    popd
    goto :failed
)
popd

echo [3/3] Flashing the application and starting the serial monitor...
pushd "%APP_DIR%"
if errorlevel 1 goto :failed
"%IDF_PYTHON%" "%IDF_PATH%\tools\idf.py" flash monitor
set "FLASH_MONITOR_EXIT_CODE=%ERRORLEVEL%"
popd
if not "%FLASH_MONITOR_EXIT_CODE%"=="0" (
    goto :failed
)

echo.
echo Build, flash, and monitor completed successfully.
echo Output: "%APP_DIR%\build"
endlocal
exit /b 0

:failed
if defined FLASH_MONITOR_EXIT_CODE (
    set "EXIT_CODE=%FLASH_MONITOR_EXIT_CODE%"
) else if defined BUILD_EXIT_CODE (
    set "EXIT_CODE=%BUILD_EXIT_CODE%"
) else (
    set "EXIT_CODE=%ERRORLEVEL%"
)
if "%EXIT_CODE%"=="0" set "EXIT_CODE=1"
echo.
echo Build failed with exit code %EXIT_CODE%.
endlocal & exit /b %EXIT_CODE%
