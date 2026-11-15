# Sly 2 VR: undoes the last update: the files it replaced come back from the newest backup\ folder, the files it
# added are removed, and version.txt goes back. Your VR profile, Sly2VR.ini and logs are never touched.
. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $backup = Get-ChildItem -Path (Join-Path $Script:Root 'backup') -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $backup) { throw 'There is no update to undo.' }
    if (Get-Process -Name 'Sly2-Release' -ErrorAction SilentlyContinue) { throw 'Sly 2 is running: close the game first.' }
    $current = Get-PackageVersion
    $added = Join-Path $backup.FullName 'added.txt'
    if (Test-Path $added) {
        Get-Content $added | Where-Object { $_ -and $_ -notmatch '\.\.' } | ForEach-Object {
            $p = Join-Path $Script:Root $_
            if (Test-Path $p) { Remove-Item -Force $p }
        }
    }
    Get-ChildItem -Path $backup.FullName -Recurse -File | Where-Object { $_.Name -ne 'added.txt' } | ForEach-Object {
        $rel = $_.FullName.Substring($backup.FullName.Length).TrimStart('\', '/')
        $dst = Join-Path $Script:Root $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item -Force $_.FullName $dst
    }
    Remove-Item -Recurse -Force $backup.FullName
    $msg = "Rolled back $current -> $(Get-PackageVersion)"
    Add-Content -Path (Join-Path $Script:Root 'logs\update.log') -Value "$(Get-Date -Format s)  $msg" -Encoding UTF8
    Write-Host $msg -ForegroundColor Green
} catch {
    Write-Host "Rollback failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    Pause-IfInteractive
}
