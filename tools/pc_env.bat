@echo off
rem Sets up the Windows host toolchain for building ReXGlue projects:
rem   - MSVC 14.5x headers/libs + Windows SDK (VS 18 Build Tools, vcvars64)
rem   - VS-bundled CMake 4.x and Ninja 1.13 (never the MSYS2 cmake: it mangles Windows paths)
rem   - MSYS2 clang 22 driving the MSVC ABI (-target x86_64-pc-windows-msvc), linked with lld-link
rem Usage: call tools\pc_env.bat, then run cmake with %REX_CMAKE_COMPILERS%.
rem fable2-nx (2026-11-24, as gears-nx): where the usual paths are missing, this PC's: VS 2022 Community (its CMake and
rem Ninja), LLVM 22 in Program Files (the same MSVC-target clang) and Python 3.10. Quoted: they have spaces.
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
if not exist "%VSROOT%" set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "PYTHON_DIR=C:/Python314"
if not exist "C:\Python314\python.exe" set "PYTHON_DIR=D:/Program Files/Python/Python310"
set "PATH=%PYTHON_DIR%;%PATH%"
set "LLVM_BIN=C:/devkitPro/msys2/clang64/bin"
if not exist "C:\devkitPro\msys2\clang64\bin\clang.exe" set "LLVM_BIN=C:/Program Files/LLVM/bin"
set REX_CMAKE_COMPILERS="-DCMAKE_C_COMPILER=%LLVM_BIN%/clang.exe" "-DCMAKE_CXX_COMPILER=%LLVM_BIN%/clang++.exe" -DCMAKE_C_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_LINKER_TYPE=LLD "-DCMAKE_RC_COMPILER=%LLVM_BIN%/llvm-rc.exe" "-DCMAKE_AR=%LLVM_BIN%/llvm-ar.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" "-DPYTHON_EXECUTABLE=%PYTHON_DIR%/python.exe" "-DPython3_EXECUTABLE=%PYTHON_DIR%/python.exe"
rem The code generator writes the loaded game image here (tools\post_codegen.py: ro_constants.py and the entry modes
rem of flush_mode_flow.py read it). Game data: never committed or distributed.
set "REXGLUE_DUMP_IMAGE=%~dp0..\out\renut_image.bin"
