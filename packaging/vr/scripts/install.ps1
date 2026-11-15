# Sly 2 VR: installs the mod beside your SafeHouse install, without changing SafeHouse or its saves.
#   - copies this package to %LOCALAPPDATA%\Programs\Sly2VR (an update or a reinstall keeps your VR saves, settings
#     and logs there)
#   - finds your SafeHouse Sly 2 installation (the setup's, or a portable copy you point at)
#   - makes a separate VR profile: a copy of your Sly 2 saves and settings (your normal saves are never touched)
#   - adds Start menu shortcuts and a desktop shortcut, and checks for an OpenXR runtime
. (Join-Path $PSScriptRoot 'common.ps1')

function Choose([string]$title, [object[]]$items, [scriptblock]$label) {
    if ($items.Count -eq 1) { return $items[0] }
    Write-Host $title
    for ($i = 0; $i -lt $items.Count; $i++) { Write-Host ("  {0}) {1}" -f ($i + 1), (& $label $items[$i])) }
    while ($true) {
        $n = Read-Host 'Number'
        $k = 0
        if ([int]::TryParse($n, [ref]$k) -and $k -ge 1 -and $k -le $items.Count) { return $items[$k - 1] }
    }
}

try {
    $version = Get-PackageVersion
    Write-Host "Sly 2 VR ${version}: install" -ForegroundColor Cyan
    Write-Host ''

    # 1. SafeHouse's Sly 2 (the setup's folders first; else ask for a portable SafeHouse folder)
    $setupBase = Join-Path $env:LOCALAPPDATA 'ProjectCane'
    $installs = Find-Sly2Installs (Join-Path $setupBase 'installations')
    $userSource = Join-Path $setupBase 'user\sly2'
    if ($installs.Count -eq 0) {
        Write-Host 'SafeHouse''s Sly 2 is not in the usual place (the SafeHouse setup installs it under'
        Write-Host "$setupBase). If you use the portable SafeHouse, pick its folder (the one with bin and data)."
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = 'Pick your SafeHouse folder (the one that holds bin and data)'
        if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw 'No SafeHouse folder picked.' }
        $installs = Find-Sly2Installs (Join-Path $dlg.SelectedPath 'data\installations')
        $userSource = Join-Path $dlg.SelectedPath 'data\user\sly2'
        if ($installs.Count -eq 0) {
            throw "No installed Sly 2 under $($dlg.SelectedPath)\data\installations. Install Sly 2 in SafeHouse first."
        }
    }
    $install = Choose 'Several Sly 2 regions are installed. Which one should VR play?' $installs { param($d) $d.Name }
    Write-Host "Sly 2 found: $($install.FullName)"

    # 2. the package copied to its own folder (an existing install keeps user\, logs\, backup\, Sly2VR.ini)
    $dst = $Script:InstallDir
    if ($Script:Root -ne $dst) {
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        foreach ($item in 'bin', 'scripts', 'licenses') {
            $from = Join-Path $Script:Root $item
            if (Test-Path $from) {
                $to = Join-Path $dst $item
                if (Test-Path $to) { Remove-Item -Recurse -Force $to }
                Copy-Item -Recurse -Force $from $to
            }
        }
        Get-ChildItem -Path $Script:Root -File | Where-Object { $_.Name -ne 'config.json' -and $_.Name -ne 'Sly2VR.ini' } |
            ForEach-Object { Copy-Item -Force $_.FullName (Join-Path $dst $_.Name) }
    }
    foreach ($d in 'logs', 'updates', 'backup') { New-Item -ItemType Directory -Force -Path (Join-Path $dst $d) | Out-Null }
    # (files from a downloaded zip carry Windows' "from the internet" mark: you chose to install them)
    Get-ChildItem -Path $dst -Recurse -File -Include *.exe, *.dll, *.ps1, *.bat |
        Where-Object { $_.FullName -notlike (Join-Path $dst 'user\*') } | Unblock-File -ErrorAction SilentlyContinue

    # 3. the VR profile: a copy of your saves and settings, made once (a reinstall keeps it); the Mods folder linked
    $vrUser = Join-Path $dst 'user'
    if (-not (Test-Path $vrUser)) {
        New-Item -ItemType Directory -Force -Path $vrUser | Out-Null
        if (Test-Path $userSource) {
            Get-ChildItem -Path $userSource -Force | Where-Object { $_.Name -ne 'Mods' } |
                ForEach-Object { Copy-Item -Recurse -Force $_.FullName (Join-Path $vrUser $_.Name) }
            $mods = Join-Path $userSource 'Mods'
            if (Test-Path $mods) { New-Item -ItemType Junction -Path (Join-Path $vrUser 'Mods') -Target $mods | Out-Null }
            Write-Host "Your saves and settings were copied into the VR profile ($vrUser)."
        } else {
            Write-Host 'No Sly 2 saves found yet: the VR profile starts empty.'
        }
    } else {
        Write-Host "Keeping your existing VR profile ($vrUser)."
    }

    # 4. the settings the launchers read
    $cfgRoot = $Script:Root
    $Script:Root = $dst
    Write-Config ([pscustomobject]@{ dataDir = $install.FullName; userSource = $userSource; installed = (Get-Date).ToString('s') })
    $Script:Root = $cfgRoot

    # 5. shortcuts (Start menu folder "Sly 2 VR", and the first-person one on the desktop)
    Set-Sly2VRShortcuts $dst

    # 6. the OpenXR runtime
    $rt = Get-OpenXRRuntime
    Write-Host ''
    Write-Host "Installed Sly 2 VR $version to $dst" -ForegroundColor Green
    if ($rt) { Write-Host "VR runtime: $(Describe-Runtime $rt)" }
    else {
        Write-Host 'No OpenXR runtime is set up yet. For a Quest 3: install the Meta Quest Link app and turn on' -ForegroundColor Yellow
        Write-Host '"Set Meta Quest Link as active OpenXR runtime" in its Settings > General, or use SteamVR or Virtual Desktop.' -ForegroundColor Yellow
    }
    Write-Host ''
    Write-Host 'Next: put your headset on (Quest: start Link / Air Link), then open "Sly 2 VR - First run (check the hands)"'
    Write-Host 'from the Start menu. README-FIRST.txt has the full checklist.'
    Start-Process explorer.exe $dst
} catch {
    Write-Host ''
    Write-Host "Install failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    Pause-IfInteractive
}
