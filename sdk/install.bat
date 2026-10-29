@echo off
setlocal EnableExtensions

rem Install the toolchain for the ESP-IDF checkout shipped in this repository.
set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "ADF_PATH=%ROOT_DIR%"
set "IDF_PATH=%ROOT_DIR%\esp-idf"
set "EXIT_CODE=0"

python.exe --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: python.exe was not found in PATH.
    echo Install a supported Python version for ESP-IDF and reopen this terminal.
    echo Disable the Microsoft Store Python execution alias if it takes precedence.
    set "EXIT_CODE=1"
    goto :failed
)

if not exist "%IDF_PATH%\install.bat" (
    echo ERROR: ESP-IDF checkout is incomplete: "%IDF_PATH%\install.bat" is missing.
    echo esp-idf is part of this repository, so make sure the working tree is complete.
    set "EXIT_CODE=1"
    goto :failed
)

python.exe "%IDF_PATH%\tools\python_version_checker.py"
if errorlevel 1 (
    echo ERROR: The installed Python version is not supported by this ESP-IDF checkout.
    set "EXIT_CODE=1"
    goto :failed
)

echo.
echo Installing tools for the local ESP-IDF checkout...
echo ESP-IDF source: "%IDF_PATH%"
call "%IDF_PATH%\install.bat"
set "IDF_INSTALL_EXIT_CODE=%ERRORLEVEL%"
if not "%IDF_INSTALL_EXIT_CODE%"=="0" (
    echo ERROR: ESP-IDF tool installation failed.
    set "EXIT_CODE=%IDF_INSTALL_EXIT_CODE%"
    goto :failed
)
echo.
echo Repository setup completed successfully.
echo Run build.bat next to configure, build, erase, flash, and monitor the app.
endlocal
exit /b 0

:failed
if "%EXIT_CODE%"=="0" set "EXIT_CODE=1"
echo.
echo Repository setup failed with exit code %EXIT_CODE%.
endlocal & exit /b %EXIT_CODE%
