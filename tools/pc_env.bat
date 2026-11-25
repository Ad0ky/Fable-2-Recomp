@echo off
rem Sets up the Windows host toolchain for building ReXGlue projects:
rem   - MSVC 14.5x headers/libs + Windows SDK (VS 18 Build Tools, vcvars64)
rem   - VS-bundled CMake 4.x and Ninja 1.13 (never the MSYS2 cmake: it mangles Windows paths)
rem   - MSYS2 clang 22 driving the MSVC ABI (-target x86_64-pc-windows-msvc), linked with lld-link
rem Usage: call tools\pc_env.bat, then run cmake with %REX_CMAKE_COMPILERS%.
rem fable2-nx (2026-11-24/25): where the usual paths are missing, the installed ones: Visual Studio 2026 Community or
rem 2022 Community (its CMake and Ninja), MSYS2's clang64, else Visual Studio's own LLVM, else LLVM in Program Files,
rem and Python 3.14 (C:\Python314 or the per-user install) or 3.10. Quoted: they have spaces.
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "PYDIR=C:\Python314"
if not exist "%PYDIR%\python.exe" set "PYDIR=%LOCALAPPDATA%\Python\pythoncore-3.14-64"
if not exist "%PYDIR%\python.exe" set "PYDIR=D:\Program Files\Python\Python310"
set "PATH=%PYDIR%;%PATH%"
set "PYEXE=%PYDIR:\=/%/python.exe"
set "PYTHON_DIR=%PYDIR:\=/%"
set "LLVM_BIN=C:/devkitPro/msys2/clang64/bin"
if not exist "C:\devkitPro\msys2\clang64\bin\clang++.exe" set "LLVM_BIN=%VSROOT:\=/%/VC/Tools/Llvm/x64/bin"
if not exist "C:\devkitPro\msys2\clang64\bin\clang++.exe" if not exist "%VSROOT%\VC\Tools\Llvm\x64\bin\clang++.exe" set "LLVM_BIN=C:/Program Files/LLVM/bin"
set REX_CMAKE_COMPILERS="-DCMAKE_C_COMPILER=%LLVM_BIN%/clang.exe" "-DCMAKE_CXX_COMPILER=%LLVM_BIN%/clang++.exe" -DCMAKE_C_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_LINKER_TYPE=LLD "-DCMAKE_RC_COMPILER=%LLVM_BIN%/llvm-rc.exe" "-DCMAKE_AR=%LLVM_BIN%/llvm-ar.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" "-DPYTHON_EXECUTABLE=%PYEXE%" "-DPython3_EXECUTABLE=%PYEXE%"
rem The code generator writes the loaded game image here (tools\post_codegen.py: ro_constants.py and the entry modes
rem of flush_mode_flow.py read it). Game data: never committed or distributed.
set "REXGLUE_DUMP_IMAGE=%~dp0..\out\renut_image.bin"
