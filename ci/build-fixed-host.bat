@echo off
setlocal
for /f "tokens=* USEBACKQ" %%p in (
  `"%programfiles(x86)%\Microsoft Visual Studio\Installer\vswhere" -latest -property installationPath`
) do call "%%p\Common7\Tools\vsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1
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
