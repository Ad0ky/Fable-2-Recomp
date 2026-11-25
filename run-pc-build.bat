@echo off
rem fable2-nx (2026-11-24): the PC build (tools\build_pc.bat: Renuts renderer and the emulated GPU in one renut.exe,
rem app\out\pc), its output in pc-build.log next to this file. Run it from File Explorer or a prompt. The first time it
rem also builds glslangValidator (tools\build_glslang.bat) and takes the DirectX shader compiler from the Windows SDK
rem when XenosRecomps dxc-bin has no Windows files.
cd /d "%~dp0"
echo Fable 2 NX PC build - %date% %time% > pc-build.log
git log --oneline -1 >> pc-build.log 2>&1
set "DXC=app\thirdparty\reblue-XenosRecomp\thirdparty\dxc-bin"
set "KIT=C:\Program Files (x86)\Windows Kits\10"
if not exist "%DXC%\lib\x64\dxcompiler.lib" copy /y "%KIT%\Lib\10.0.26100.0\um\x64\dxcompiler.lib" "%DXC%\lib\x64\" >> pc-build.log 2>&1
if not exist "%DXC%\bin\x64\dxc.exe" copy /y "%KIT%\bin\10.0.26100.0\x64\dx?.exe" "%DXC%\bin\x64\" >> pc-build.log 2>&1
if not exist "%DXC%\bin\x64\dxcompiler.dll" copy /y "%KIT%\bin\10.0.26100.0\x64\dxcompiler.dll" "%DXC%\bin\x64\" >> pc-build.log 2>&1
if not exist "%DXC%\bin\x64\dxil.dll" copy /y "%KIT%\bin\10.0.26100.0\x64\dxil.dll" "%DXC%\bin\x64\" >> pc-build.log 2>&1
if not exist "out\glslang\StandAlone\glslangValidator.exe" cmd /c tools\build_glslang.bat >> pc-build.log 2>&1
cmd /c tools\build_pc.bat >> pc-build.log 2>&1
rem The SDK's DLLs are copied next to renut.exe only when renut.exe itself relinks: a change in the SDK alone
rem (the emulated GPU plugin, the runtime) would otherwise run the old DLL.
if not errorlevel 1 xcopy /y /d sdk\out\win-amd64\*.dll app\out\pc\ >> pc-build.log 2>&1
if errorlevel 1 (echo RESULT: FAILED, exit %errorlevel% >> pc-build.log) else (echo RESULT: OK >> pc-build.log)
echo finished %date% %time% >> pc-build.log
