# Sly 2 VR: starts the game in VR with a log of the run in logs\ (the last 20 are kept).
#   -Mode fp | stereo | screen     first person, third person in 3D, or the normal picture on a big screen
#   -Bones                         also print the player's bones and the hands chosen (the first run's check)
#   -Calibrate                     ask for the T-pose calibration again at the title (height, forward, hands)
param(
    [ValidateSet('fp', 'stereo', 'screen')][string]$Mode = 'fp',
    [switch]$Bones,
    [switch]$Calibrate
)
. (Join-Path $PSScriptRoot 'common.ps1')

$code = 1
try {
    $cfg = Read-Config
    if (-not $cfg) { throw 'Sly 2 VR is not installed yet: run Install.bat first.' }
    if (-not (Test-Path $cfg.dataDir)) { throw "Sly 2's installation is gone ($($cfg.dataDir)): run Install.bat again." }
    $exe = Join-Path $Script:Root 'bin\Sly2-Release.exe'
    if (-not (Test-Path $exe)) { throw "The game is missing ($exe): run Install.bat again." }

    $logs = Join-Path $Script:Root 'logs'
    New-Item -ItemType Directory -Force -Path $logs | Out-Null
    $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $log = Join-Path $logs "sly2vr-$stamp-$Mode.log"
    $out = "$log.out"
    $err = "$log.err"

    $env:SLY2N_VR = $Mode
    $env:SLY2N_VR_INI = Join-Path $Script:Root 'Sly2VR.ini'
    if ($Bones) { $env:SLY2N_VR_BONES = '1' } else { Remove-Item Env:SLY2N_VR_BONES -ErrorAction SilentlyContinue }
    if ($Calibrate) { $env:SLY2N_VR_CALIBRATE = '1' } else { Remove-Item Env:SLY2N_VR_CALIBRATE -ErrorAction SilentlyContinue }

    $rt = Get-OpenXRRuntime
    $header = @(
        "Sly 2 VR $(Get-PackageVersion), mode $Mode$(if ($Bones) { ' (bones printed)' })$(if ($Calibrate) { ' (calibration)' }), started $(Get-Date -Format s)",
        "Windows: $([Environment]::OSVersion.VersionString)",
        "GPU: $((Get-GpuNames) -join '; ')",
        "OpenXR runtime: $(Describe-Runtime $rt)",
        "Sly 2: $($cfg.dataDir)",
        "VR profile: $(Join-Path $Script:Root 'user')",
        ''
    )
    Set-Content -Path $log -Value $header -Encoding UTF8
    if (-not $rt) { Write-Host 'Warning: no OpenXR runtime is active (Quest: Meta Quest Link app > Settings > General).' -ForegroundColor Yellow }
    Write-Host "Starting Sly 2 VR ($Mode). This window keeps the log; close the game to finish."

    $args = @('--data-dir', "`"$($cfg.dataDir)`"", '--user-dir', "`"$(Join-Path $Script:Root 'user')`"")
    $p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Split-Path $exe) -PassThru -Wait `
        -RedirectStandardOutput $out -RedirectStandardError $err -NoNewWindow
    $code = $p.ExitCode

    Add-Content -Path $log -Value '---- output ----' -Encoding UTF8
    if (Test-Path $out) { Get-Content $out | Add-Content -Path $log -Encoding UTF8; Remove-Item $out }
    Add-Content -Path $log -Value '---- errors and VR messages ----' -Encoding UTF8
    if (Test-Path $err) { Get-Content $err | Add-Content -Path $log -Encoding UTF8; Remove-Item $err }
    Add-Content -Path $log -Value "---- exit code $code at $(Get-Date -Format s) ----" -Encoding UTF8

    # only the last 20 runs kept
    Get-ChildItem -Path $logs -Filter 'sly2vr-*.log' | Sort-Object LastWriteTime -Descending | Select-Object -Skip 20 |
        Remove-Item -Force -ErrorAction SilentlyContinue

    if ($code -ne 0) {
        Write-Host "The game stopped with code $code. The log: $log" -ForegroundColor Yellow
        Write-Host 'Run "Collect Logs.bat" and send me the zip it makes.'
        Pause-IfInteractive
    }
} catch {
    Write-Host "Could not start: $($_.Exception.Message)" -ForegroundColor Red
    Pause-IfInteractive
}
exit $code
