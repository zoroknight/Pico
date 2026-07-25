@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PICO_ROOT=%SCRIPT_DIR%.."
set "SOLUTION_PATH=%PICO_ROOT%\Build\Pico.sln"

if not exist "%SOLUTION_PATH%" (
    echo Pico solution was not found:
    echo %SOLUTION_PATH%
    echo.
    echo Run GenerateProjectFiles.bat first.
    echo.
    pause
    exit /b 1
)

start "" "%SOLUTION_PATH%"

endlocal
