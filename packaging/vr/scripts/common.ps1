# Sly 2 VR: shared helpers for the install, launch, update and log scripts (PowerShell 5.1, built into Windows 10/11).
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

# The package's folder (the one holding bin\, scripts\ and the .bat files).
$Script:Root = Split-Path -Parent $PSScriptRoot
$Script:InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\Sly2VR'

function Get-PackageVersion([string]$dir = $Script:Root) {
    $f = Join-Path $dir 'version.txt'
    if (Test-Path $f) { return (Get-Content $f -Raw).Trim() }
    return 'unknown'
}

function Read-Config {
    $f = Join-Path $Script:Root 'config.json'
    if (-not (Test-Path $f)) { return $null }
    return Get-Content $f -Raw | ConvertFrom-Json
}

function Write-Config($cfg) {
    $cfg | ConvertTo-Json | Set-Content -Path (Join-Path $Script:Root 'config.json') -Encoding UTF8
}

# Every installed Sly 2 region under a SafeHouse installations folder (each has an installation.json).
function Find-Sly2Installs([string]$installations) {
    if (-not (Test-Path $installations)) { return @() }
    return @(Get-ChildItem -Path $installations -Directory -Filter 'sly2*' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch '\.' -and (Test-Path (Join-Path $_.FullName 'installation.json')) })
}

# The active OpenXR runtime (the app the headset runs through), or $null.
function Get-OpenXRRuntime {
    foreach ($key in 'HKLM:\SOFTWARE\Khronos\OpenXR\1', 'HKCU:\SOFTWARE\Khronos\OpenXR\1') {
        try {
            $v = (Get-ItemProperty -Path $key -Name ActiveRuntime -ErrorAction Stop).ActiveRuntime
            if ($v) { return $v }
        } catch { }
    }
    return $null
}

function Describe-Runtime([string]$path) {
    if (-not $path) { return 'none' }
    if ($path -match 'oculus|meta') { return "Meta Quest Link ($path)" }
    if ($path -match 'steamvr|valve') { return "SteamVR ($path)" }
    if ($path -match 'virtual ?desktop|vdxr') { return "Virtual Desktop ($path)" }
    if ($path -match 'mixedreality|microsoft') { return "Windows Mixed Reality ($path)" }
    return $path
}

function Get-GpuNames {
    try { return @(Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) (driver $($_.DriverVersion))" }) }
    catch { return @('unknown') }
}

# SHA-256 of a file, lower case.
function Get-Sha256([string]$path) {
    return (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLowerInvariant()
}

function Pause-IfInteractive([string]$message = 'Press Enter to close this window.') {
    if ([Environment]::UserInteractive) { [void](Read-Host $message) }
}

# The Start menu folder "Sly 2 VR" and the desktop shortcut, for the package in $dst (install, and after an update).
function Set-Sly2VRShortcuts([string]$dst) {
    $shell = New-Object -ComObject WScript.Shell
    $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Sly 2 VR'
    New-Item -ItemType Directory -Force -Path $menu | Out-Null
    $links = [ordered]@{
        'Sly 2 VR - First person' = 'Play VR - First person.bat'
        'Sly 2 VR - Stereo (third person)' = 'Play VR - Stereo.bat'
        'Sly 2 VR - Big screen' = 'Play VR - Screen.bat'
        'Sly 2 VR - First run (check the hands)' = 'Play VR - First run (check hands).bat'
        'Sly 2 VR - Calibrate (first person)' = 'Play VR - Calibrate (first person).bat'
        'Sly 2 VR - Settings' = 'Settings.bat'
        'Sly 2 VR - Update' = 'Update.bat'
        'Sly 2 VR - Collect logs' = 'Collect Logs.bat'
        'Sly 2 VR - Read me' = 'README-FIRST.txt'
        'Sly 2 VR - Controls (print me)' = 'CONTROLS.txt'
    }
    foreach ($name in $links.Keys) {
        $target = Join-Path $dst $links[$name]
        if (-not (Test-Path $target)) { continue }
        $lnk = $shell.CreateShortcut((Join-Path $menu "$name.lnk"))
        $lnk.TargetPath = $target
        $lnk.WorkingDirectory = $dst
        $lnk.Save()
    }
    $desk = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Sly 2 VR.lnk'))
    $desk.TargetPath = Join-Path $dst 'Play VR - First person.bat'
    $desk.WorkingDirectory = $dst
    $desk.Save()
}
