@echo off
setlocal
if /i not "%CIRCLECI%"=="true" exit /b 1
rem The surrounding plugin manifest must not affect the core classic-mode vcpkg build.
if exist vcpkg.json ren vcpkg.json vcpkg-hostqa-disabled.json
call refreshenv
set "PATH=C:\Program Files\CMake\bin;%PATH%"
for /f "tokens=* USEBACKQ" %%p in (
  `"%programfiles(x86)%\Microsoft Visual Studio\Installer\vswhere" -latest -property installationPath`
) do call "%%p\Common7\Tools\vsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1
where cmake
if errorlevel 1 exit /b 1
if "%1"=="regression" (
  python ci\test-usb-window-lifetime.py
  if errorlevel 1 exit /b 1
  exit /b 0
)
pushd core-source
if "%1"=="deps" (
  call buildwin\win_deps_x64.bat wx33
  if errorlevel 1 exit /b 1
  exit /b 0
)
set CONFIGURATION=Release
set CMAKE_BUILD_PARALLEL_LEVEL=4
call ci\windows-x64.bat
if errorlevel 1 exit /b 1
popd
python ci\qualify-fixed-host.py
exit /b %ERRORLEVEL%
