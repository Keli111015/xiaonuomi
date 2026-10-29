@echo off

set "ORIG_PATH=%cd%"
set "ADF_PATH=%~dp0"
if "%ADF_PATH:~-1%"=="\" set "ADF_PATH=%ADF_PATH:~0,-1%"

:: Always use the ESP-IDF checkout tracked by this repository.
set "IDF_PATH=%ADF_PATH%\esp-idf"

echo ADF_PATH: %ADF_PATH%
echo IDF_PATH: %IDF_PATH%

call "%IDF_PATH%\export.bat"
if errorlevel 1 goto :ErrorHandling
python.exe "%ADF_PATH%\tools\adf_install_patches.py" apply-patch
if errorlevel 1 goto :ErrorHandling

echo.
echo The following command can be executed now to view detailed usage:
echo.
echo   idf.py --help
echo.
echo Compilation example (The commands highlighted in yellow below are optional: Configure the chip and project settings separately)
echo.
echo   cd %ADF_PATH%\examples\cli
echo   [33midf.py set-target esp32[0m
echo   [33midf.py menuconfig[0m
echo   idf.py build
echo.
goto :Success

:ErrorHandling
echo The script encountered an error.
exit /b 1

:Success
echo ESP-IDF environment setup completed successfully.
exit /b 0
