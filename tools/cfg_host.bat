@echo off
call "%~dp0pc_env.bat" || exit /b 1
rem fable2-nx (2026-11-24, as gears-nx): this repository's SDK, configured in out\host (the generator lands in
rem sdk\out\win-amd64\rexglue.exe); it named banjo-nx's folders.
cmake -S "%~dp0..\sdk" -B "%~dp0..\out\host" -G Ninja -DCMAKE_BUILD_TYPE=Release %REX_CMAKE_COMPILERS% -DREXGLUE_ENABLE_TRACY=OFF
