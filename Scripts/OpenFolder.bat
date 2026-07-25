@echo off
setlocal EnableExtensions

pushd "%~dp0\.."
set "PICO_ROOT=%cd%"
popd

set "DEVENV="

for /f "delims=" %%i in ('where devenv.exe 2^>nul') do (
    if not defined DEVENV set "DEVENV=%%i"
)

if defined DEVENV goto OpenVisualStudio

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto VisualStudioNotFound

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.CoreEditor -property productPath`) do (
    if not defined DEVENV set "DEVENV=%%i"
)

if not defined DEVENV goto VisualStudioNotFound

:OpenVisualStudio
echo Opening Pico folder in Visual Studio:
echo %PICO_ROOT%
echo.

start "" /D "%PICO_ROOT%" "%DEVENV%" "%PICO_ROOT%"
goto End

:VisualStudioNotFound
echo Visual Studio was not found.
echo.
echo Please install Visual Studio 2022, or open this folder manually:
echo %PICO_ROOT%
echo.
echo Visual Studio menu:
echo File ^> Open ^> Folder
echo.
pause
exit /b 1

:End
endlocal
