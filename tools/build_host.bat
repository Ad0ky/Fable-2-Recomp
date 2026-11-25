@echo off
call "%~dp0pc_env.bat" || exit /b 1
rem fable2-nx (2026-11-24): this repository's out\host (tools\cfg_host.bat); JOBS (env) jobs, 5 by default.
if "%JOBS%"=="" set "JOBS=5"
cmake --build "%~dp0..\out\host" --target %1 -j %JOBS%
