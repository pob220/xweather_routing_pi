@echo off
setlocal EnableExtensions
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
python %*
exit /b %errorlevel%
