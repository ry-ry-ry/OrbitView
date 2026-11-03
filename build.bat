@echo off
rem Builds build\OrbitView\64\win.xpl. Pass "install" to copy it into X-Plane
rem (found from X-Plane's own install record, or set XPLANE to the folder).
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set VCVARS=
for /f "usebackq delims=" %%i in (`call "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do set "VCVARS=%%i"
if not defined VCVARS (echo vcvars64.bat not found & exit /b 1)
call "%VCVARS%" >nul 2>nul || exit /b 1

if not exist build\obj mkdir build\obj
if not exist build\OrbitView\64 mkdir build\OrbitView\64

cl /nologo /O2 /EHsc /MT /std:c++17 /W3 /LD ^
   /DIBM=1 /DXPLM200 /DXPLM210 /DXPLM300 /DXPLM301 /DXPLM303 /DXPLM400 ^
   /I SDK\CHeaders\XPLM /I SDK\CHeaders\Widgets ^
   src\OrbitView.cpp /Fobuild\obj\ /Fe:build\OrbitView\64\win.xpl ^
   /link SDK\Libraries\Win\XPLM_64.lib SDK\Libraries\Win\XPWidgets_64.lib user32.lib ^
   /IMPLIB:build\obj\win.lib || exit /b 1

copy /y config.ini build\OrbitView\ >nul

if /i not "%1"=="install" exit /b 0
if not defined XPLANE if exist "%LOCALAPPDATA%\x-plane_install_12.txt" set /p XPLANE=<"%LOCALAPPDATA%\x-plane_install_12.txt"
if not defined XPLANE (echo X-Plane 12 not found, set XPLANE to its folder & exit /b 1)
if "%XPLANE:~-1%"=="/" set "XPLANE=%XPLANE:~0,-1%"
if "%XPLANE:~-1%"=="\" set "XPLANE=%XPLANE:~0,-1%"
set "DEST=%XPLANE%\Resources\plugins\OrbitView"
if not exist "%DEST%\64" mkdir "%DEST%\64"
copy /y build\OrbitView\64\win.xpl "%DEST%\64\" >nul || exit /b 1
if not exist "%DEST%\config.ini" copy /y config.ini "%DEST%\" >nul
echo Installed to %DEST%
