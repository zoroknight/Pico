@echo off
setlocal

pushd "%~dp0\.."

echo Generating Pico Visual Studio 2022 project files...
echo.

cmake -S . -B Build -G "Visual Studio 17 2022" -A x64

if errorlevel 1 (
    echo.
    echo Failed to generate Pico Visual Studio project files.
    echo Make sure CMake is installed and available in PATH.
    echo.
    pause
    popd
    exit /b 1
)

echo.
echo Pico Visual Studio project files generated:
echo %cd%\Build\Pico.sln
echo.
pause

popd
endlocal
