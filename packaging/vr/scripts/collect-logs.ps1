# Sly 2 VR: zips what's needed to look into a problem: the last 10 run logs, the update log, Sly2VR.ini, the version,
# and a short system report (Windows, GPU, the OpenXR runtime). The zip lands on your desktop. No saves, no game files.
. (Join-Path $PSScriptRoot 'common.ps1')

$temp = $null
try {
    $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $temp = Join-Path ([IO.Path]::GetTempPath()) "Sly2VR-logs-$stamp"
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    $logs = Join-Path $Script:Root 'logs'
    Get-ChildItem -Path $logs -Filter 'sly2vr-*.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending |
        Select-Object -First 10 | ForEach-Object { Copy-Item $_.FullName $temp }
    foreach ($f in (Join-Path $logs 'update.log'), (Join-Path $Script:Root 'Sly2VR.ini'), (Join-Path $Script:Root 'version.txt')) {
        if (Test-Path $f) { Copy-Item $f $temp }
    }
    $cfg = Read-Config
    @(
        "Sly 2 VR $(Get-PackageVersion), collected $(Get-Date -Format s)",
        "Windows: $([Environment]::OSVersion.VersionString), 64-bit: $([Environment]::Is64BitOperatingSystem)",
        "GPU: $((Get-GpuNames) -join '; ')",
        "OpenXR runtime: $(Describe-Runtime (Get-OpenXRRuntime))",
        "Installed in: $($Script:Root)",
        "Sly 2: $(if ($cfg) { $cfg.dataDir } else { 'not installed' })"
    ) | Set-Content -Path (Join-Path $temp 'system.txt') -Encoding UTF8
    $zip = Join-Path ([Environment]::GetFolderPath('Desktop')) "Sly2VR-logs-$stamp.zip"
    Compress-Archive -Path (Join-Path $temp '*') -DestinationPath $zip -Force
    Write-Host "Saved $zip" -ForegroundColor Green
    Start-Process explorer.exe "/select,`"$zip`""
} catch {
    Write-Host "Could not collect the logs: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($temp -and (Test-Path $temp)) { Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue }
    Pause-IfInteractive
}
