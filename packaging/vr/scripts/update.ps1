# Sly 2 VR: applies an update zip (Sly2-VR-update-<version>.zip): checks every file against the update's manifest
# (SHA-256), backs up the files it replaces to backup\<old version>-<time>\, then copies the new ones in.
# Your VR profile (user\), Sly2VR.ini and logs\ are never touched. Rollback.bat puts the last backup back.
#   Drag the zip onto Update.bat, or drop it into the updates folder and run Update.bat.
param([string]$Zip)
. (Join-Path $PSScriptRoot 'common.ps1')

$updateLog = Join-Path $Script:Root 'logs\update.log'
function Note([string]$text) {
    Write-Host $text
    New-Item -ItemType Directory -Force -Path (Split-Path $updateLog) | Out-Null
    Add-Content -Path $updateLog -Value "$(Get-Date -Format s)  $text" -Encoding UTF8
}

$temp = $null
try {
    if (-not $Zip) {
        $newest = Get-ChildItem -Path (Join-Path $Script:Root 'updates') -Filter '*.zip' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $newest) { throw 'No update: drag an update zip onto Update.bat, or put it in the updates folder.' }
        $Zip = $newest.FullName
    }
    if (-not (Test-Path $Zip)) { throw "No such file: $Zip" }
    $old = Get-PackageVersion
    Note "Update from $Zip (installed: $old)"

    $temp = Join-Path ([IO.Path]::GetTempPath()) ("sly2vr-update-" + [Guid]::NewGuid().ToString('N'))
    Expand-Archive -Path $Zip -DestinationPath $temp
    $manifestPath = Join-Path $temp 'manifest.json'
    if (-not (Test-Path $manifestPath)) { throw 'This zip has no manifest.json: not a Sly 2 VR update.' }
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.package -ne 'Sly2-VR') { throw 'This zip is not a Sly 2 VR update.' }

    # every file checked before anything changes
    foreach ($f in $manifest.files) {
        if ($f.path -match '(^|[\\/])\.\.([\\/]|$)' -or [IO.Path]::IsPathRooted($f.path)) { throw "Refused path: $($f.path)" }
        if ($f.path -match '^(user|logs|backup|updates)[\\/]' -or $f.path -eq 'Sly2VR.ini' -or $f.path -eq 'config.json') {
            throw "Refused: an update may not replace $($f.path)."
        }
        $src = Join-Path $temp $f.path
        if (-not (Test-Path $src)) { throw "The update is incomplete: $($f.path) is missing." }
        if ((Get-Sha256 $src) -ne $f.sha256) { throw "The update is damaged: $($f.path) does not match its checksum." }
    }
    $running = Get-Process -Name 'Sly2-Release' -ErrorAction SilentlyContinue
    if ($running) { throw 'Sly 2 is running: close the game first.' }

    # the backup, then the copy
    $backup = Join-Path $Script:Root ("backup\$old-" + (Get-Date).ToString('yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    $added = @()
    foreach ($f in $manifest.files) {
        $dst = Join-Path $Script:Root $f.path
        if (Test-Path $dst) {
            $bak = Join-Path $backup $f.path
            New-Item -ItemType Directory -Force -Path (Split-Path $bak) | Out-Null
            Copy-Item -Force $dst $bak
        } else { $added += $f.path }
    }
    Copy-Item -Force (Join-Path $Script:Root 'version.txt') (Join-Path $backup 'version.txt') -ErrorAction SilentlyContinue
    ($added -join "`r`n") | Set-Content -Path (Join-Path $backup 'added.txt') -Encoding UTF8
    foreach ($f in $manifest.files) {
        $dst = Join-Path $Script:Root $f.path
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item -Force (Join-Path $temp $f.path) $dst
    }
    Set-Content -Path (Join-Path $Script:Root 'version.txt') -Value $manifest.version -Encoding UTF8
    # (the update's own follow-up: new Start menu shortcuts and the like)
    $post = Join-Path $Script:Root 'scripts\post-update.ps1'
    if (Test-Path $post) { & powershell -NoProfile -ExecutionPolicy Bypass -File $post }
    Note "Updated $old -> $($manifest.version) ($($manifest.files.Count) files; backup in $backup)"
    if ($manifest.notes) { Write-Host ''; Write-Host "What's new:"; Write-Host $manifest.notes }
    Write-Host ''
    Write-Host 'Done. If something is worse than before, run Rollback.bat.' -ForegroundColor Green
} catch {
    Note "Update failed: $($_.Exception.Message) (nothing was changed unless a copy failed midway: then run Rollback.bat)"
    exit 1
} finally {
    if ($temp -and (Test-Path $temp)) { Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue }
    Pause-IfInteractive
}
