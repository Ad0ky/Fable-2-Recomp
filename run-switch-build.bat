@echo off
rem Fable 2 NX on renut_nx_ultimate_base: runs the Switch build in Docker and writes switch-build.log next to this file.
rem The toolchain image is fable2-nx's (fable2-switch-dev:3: lo-switch-dev + Clang 20, tools\nx\Dockerfile).
rem
rem   run-switch-build.bat                build (code generation and shaders only the first time)
rem   run-switch-build.bat codegen        run the code generator again (and every post-codegen pass), then build
rem   run-switch-build.bat postcodegen    the post-codegen passes again (a new machine-copy list), then build
rem   run-switch-build.bat shaders        only the shaders and the DKSH cache (copied to the SD folder)
rem   run-switch-build.bat clean          delete the Switch build folder first
rem   run-switch-build.bat pgo-generate   PGO step 1: an instrumented LTO build (out\build\switch-pgo-gen); play it, it
rem                                       writes sdmc:/switch/fable2-nx/pgo/banjo_pgo_<N>.stream
rem   run-switch-build.bat pgo-merge      PGO step 2: the streams in the SD folder's pgo\ merged into pgo-inplace\
rem   run-switch-build.bat pgo-use        PGO step 3: the optimised LTO build from pgo-inplace\ (out\build\switch-pgo)
rem
rem Your game folder (with default.xex): GAME_DIR below, or set GAME_DIR before running.
rem Shader captures: the SD card's \switch\fable2-nx\shader_capture (the staging folder below) and fable2-nx's
rem out\shader_capture are copied into out\shader_capture before every build.
cd /d "%~dp0"
if "%GAME_DIR%"=="" set "GAME_DIR=%~dp0..\Fable 2 1.5.0"
set "SDCARD=%~dp0..\SD-card\switch\fable2-nx"
set "IMAGE=fable2-switch-dev:3"
set "EXTRA="
if /i "%~1"=="codegen" set "EXTRA=-e CODEGEN=1"
if /i "%~1"=="postcodegen" set "EXTRA=-e POSTCODEGEN=1"
if /i "%~1"=="clean" set "EXTRA=-e CLEAN=1"
if /i "%~1"=="shaders" set "EXTRA=-e SHADERS=1"
rem fable2-nx (2026-11-24): PGO + LTO (docs/building.md, section 4; docs/hand-and-machine-optimising-guide.md, section 4).
if /i "%~1"=="pgo-generate" set "EXTRA=-e BUILD_DIR=/work/out/build/switch-pgo-gen -e JOBS=8 -e "CMAKE_EXTRA=-DBANJO_PGO=generate -DBANJO_SWITCH_LTO=ON""
if /i "%~1"=="pgo-use" set "EXTRA=-e BUILD_DIR=/work/out/build/switch-pgo -e JOBS=8 -e "CMAKE_EXTRA=-DBANJO_PGO=use -DBANJO_SWITCH_LTO=ON""
echo Fable 2 NX (ultimate base) Switch build - %date% %time% > switch-build.log
git log --oneline -1 >> switch-build.log 2>&1
where docker >> switch-build.log 2>&1
if errorlevel 1 (echo RESULT: Docker is not installed. >> switch-build.log & goto end)
docker info > nul 2>&1
if errorlevel 1 (echo RESULT: Docker Desktop is not running. Start it and try again. >> switch-build.log & goto end)
if not exist app\assets mkdir app\assets
if not exist app\assets\default.xex (
  if not exist "%GAME_DIR%\default.xex" (echo RESULT: no default.xex in "%GAME_DIR%" - set GAME_DIR. >> switch-build.log & goto end)
  copy /y "%GAME_DIR%\default.xex" app\assets\default.xex >> switch-build.log
)
rem fable2-nx (2026-11-23): the game's shader banks, for the shaders build (tools/fable2_shader_bank.py): all 588 shaders.
if not exist app\assets\shaders mkdir app\assets\shaders
if exist "%GAME_DIR%\data\Shaders\*.sbk" xcopy /d /y /q "%GAME_DIR%\data\Shaders\*.sbk" app\assets\shaders\ >> switch-build.log 2>&1
if not exist out\shader_capture mkdir out\shader_capture
if exist "%~dp0..\fable2-nx\out\shader_capture" xcopy /d /y /q "%~dp0..\fable2-nx\out\shader_capture\*.bin" out\shader_capture\ >> switch-build.log 2>&1
if exist "%SDCARD%\shader_capture" xcopy /d /y /q "%SDCARD%\shader_capture\*.bin" out\shader_capture\ >> switch-build.log 2>&1
rem fable2-nx (2026-11-17): the console's pipeline list and its shader cache (with what it compiled while playing),
rem for the shaders build: it compiles every listed pipeline's variants ahead and keeps the cache's own entries.
if not exist out\console mkdir out\console
if exist "%SDCARD%\pipeline_list.bin" copy /y "%SDCARD%\pipeline_list.bin" out\console\pipeline_list.bin >> switch-build.log
if exist "%SDCARD%\deko3d_shaders.bin" copy /y "%SDCARD%\deko3d_shaders.bin" out\console\deko3d_shaders.bin >> switch-build.log
docker image inspect lo-switch-dev > nul 2>&1
if errorlevel 1 (
  echo Building the toolchain image lo-switch-dev, first run only - this takes a while...
  echo === docker build lo-switch-dev === >> switch-build.log
  docker build --progress=plain --platform linux/amd64 -t lo-switch-dev https://github.com/autorunhq/switch-dev.git#main >> switch-build.log 2>&1
  if errorlevel 1 (echo RESULT: toolchain image build failed >> switch-build.log & goto end)
)
docker image inspect %IMAGE% > nul 2>&1
if errorlevel 1 (
  echo Adding Clang 20 to the toolchain image: %IMAGE%, first run only...
  echo === docker build %IMAGE% === >> switch-build.log
  docker build --progress=plain -t %IMAGE% -f tools\nx\Dockerfile tools\nx >> switch-build.log 2>&1
  if errorlevel 1 (echo RESULT: %IMAGE% image build failed >> switch-build.log & goto end)
)
if /i "%~1"=="pgo-merge" goto pgomerge
echo Building Fable 2 NX for Switch (ultimate base)... the first run also runs the code generator and makes the shaders.
echo === Switch build === >> switch-build.log
docker run --rm %EXTRA% -v "%CD%:/work" -w /work %IMAGE% bash tools/nx/build-switch.sh >> switch-build.log 2>&1
set BUILDRC=%errorlevel%
echo RESULT: build exit code %BUILDRC% >> switch-build.log
if "%BUILDRC%"=="0" (
  if not exist "%SDCARD%" mkdir "%SDCARD%"
  if exist app\out\renut_switch\deko3d_shaders.bin copy /y app\out\renut_switch\deko3d_shaders.bin "%SDCARD%\deko3d_shaders.bin" >> switch-build.log
  if /i not "%~1"=="shaders" (
    copy /y out\switch\fable2-nx.nro "%SDCARD%\fable2-nx.nro" >> switch-build.log
    copy /y out\switch\fable2-nx.elf "%SDCARD%\fable2-nx.elf" >> switch-build.log
  )
)
goto end
:pgomerge
rem fable2-nx (2026-11-24): every session's stream of ONE instrumented build (empty the console's pgo folder before
rem playing a new instrumented build); pgo-inplace must hold no .gcda yet (move an older profile aside).
if not exist "%SDCARD%\pgo\banjo_pgo_*.stream" (echo RESULT: no banjo_pgo_*.stream in "%SDCARD%\pgo" - copy the console's sdmc:/switch/fable2-nx/pgo folder there. >> switch-build.log & goto end)
if exist out\pgo_streams rmdir /s /q out\pgo_streams
mkdir out\pgo_streams
copy /y "%SDCARD%\pgo\banjo_pgo_*.stream" out\pgo_streams\ >> switch-build.log
echo === PGO merge into pgo-inplace === >> switch-build.log
docker run --rm -v "%CD%:/work" -w /work %IMAGE% python3 tools/pgo_merge.py out/pgo_streams pgo-inplace --all >> switch-build.log 2>&1
echo RESULT: merge exit code %errorlevel% >> switch-build.log
:end
echo Done. See switch-build.log
pause
