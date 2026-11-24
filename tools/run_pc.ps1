# katamari-nx: one PC test run of app/out/pc/renut.exe: minimised, muted, its own log, PNG captures every N seconds
# (nfsmw_captura_cada_s), stopped after -Seconds. Stops only the process it started.
#   powershell -File tools/run_pc.ps1 -Seconds 120 -Name boot1 [-Every 15] [-Extra "--gpu_plugin=xenos --foo=1"]
# The log is written unbuffered (log_file_writer = false): the run is stopped by force, which would drop the buffer.
# -Visible: the window is shown (not minimised): Renut presents (and so captures and paces by vsync) only with a
# swapchain, which a minimised window does not get.
# fable2-nx (2026-11-24): -Renderer renut (Renut's renderer, as on the Switch) or emulated (the SDK's emulated Xbox 360
# GPU, the reference); -GameDir the folder with default.xex (default: ..\Fable 2 1.5.0 next to the repo).
param([int]$Seconds = 90, [string]$Name = "run", [int]$Every = 15, [string]$Extra = "", [switch]$Visible,
      [string]$Renderer = "renut", [string]$GameDir = "")
$root = Split-Path -Parent $PSScriptRoot
if (-not $GameDir) { $GameDir = Join-Path (Split-Path -Parent $root) "Fable 2 1.5.0" }
$gameRoot = $GameDir -replace "\\", "/"  # quoted below: Start-Process joins the arguments with spaces
$dir = Join-Path $root "app\out\pc"
$exe = Join-Path $dir "renut.exe"
$logs = Join-Path $root "out\pc_runs\$Name"
New-Item -ItemType Directory -Force $logs | Out-Null
$log = Join-Path $logs "$Name.log"
Remove-Item $log -ErrorAction SilentlyContinue
$min = if ($Visible) { "false" } else { "true" }
$args = @("--log_file=$log", "--audio_mute=true", "--window_start_minimized=$min", "--fullscreen=false",
          "--game_data_root=`"$gameRoot`"", "--banjo_renderer=$Renderer", "--user_data_root=$root/out/pc_user",
          "--nfsmw_captura_cada_s=$Every", "--nfsmw_captura_max=60", "--log_file_writer=false") +
        @($Extra -split ' ' | Where-Object { $_ })
# The emulated GPU is the SDK's plugin (rexgpu-xenos.dll), loaded only when named.
if ($Renderer -eq "emulated") { $args += "--gpu_plugin=xenos" }
$style = if ($Visible) { "Normal" } else { "Minimized" }
$p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $dir -PassThru -WindowStyle $style `
     -RedirectStandardError (Join-Path $logs "stderr.txt") -RedirectStandardOutput (Join-Path $logs "stdout.txt")
Write-Output "started pid $($p.Id)"
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Seconds 2 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped pid $($p.Id) after $Seconds s" }
else { Write-Output "exited by itself with code $($p.ExitCode)" }
Start-Sleep -Seconds 1
# The captures land next to the executable (nfsmw_nativo_captura.cpp); move this run's into its folder.
Get-ChildItem $dir -Filter "*.png" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $p.StartTime } |
    Move-Item -Destination $logs -Force -ErrorAction SilentlyContinue
Get-ChildItem (Join-Path $dir "capturas") -Filter "*.png" -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $p.StartTime } | Move-Item -Destination $logs -Force -ErrorAction SilentlyContinue
if (Test-Path $log) { Get-Content $log -Tail 25 }
