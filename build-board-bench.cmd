@echo off
setlocal

rem Compile only: this script never erases or flashes a device.
if not defined IDF_PATH (
  echo ERROR: IDF_PATH is not set. Export the official ESP-IDF environment first.
  exit /b 1
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b 1

pushd "%~dp0firmware\board_bench"
python "%IDF_PATH%\tools\idf.py" build
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
