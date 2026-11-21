from pathlib import Path
import os
s=Path('ci/circleci-build-msvc.bat').read_text()
start=s.index('if not "%MSVC_VERSION%" == "2022" goto wr_vs_ready')
end=s.index('\nrem The test executable',start)
block=s[start:end]
mode=os.environ['PROBE_MODE']
if mode=='normalized':
 block=block.replace(':wr_vs_selected\n', ':wr_vs_selected\nif not "%VSINSTALLDIR:~-1%"=="\\" set "VSINSTALLDIR=%VSINSTALLDIR%\\"\n')
if mode=='private':
 block=block.replace('VSINSTALLDIR','WR_VS_INSTANCE')
 block=block.replace('call "%WR_VS_INSTANCE%\\VC', 'set "VSINSTALLDIR="\ncall "%WR_VS_INSTANCE%\\VC')
block=block.replace('where cl\n','set VSINSTALLDIR\nset VCINSTALLDIR\nset VCToolsInstallDir\nset VCToolsVersion\nset PATH\nwhere cl\n')
header='@echo off\nsetlocal EnableExtensions\nset "MSVC_VERSION=2022"\nset "VSINSTALLDIR="\ncall refreshenv\n'
Path('bootstrap-probe.bat').write_text(header+block+'\nexit /b 0\n')
print('Probe mode:',mode)
