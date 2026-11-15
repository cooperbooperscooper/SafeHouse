# Sly 2 VR: run by Update.bat after an update was copied in: refreshes the Start menu shortcuts (new launchers).
. (Join-Path $PSScriptRoot 'common.ps1')
try { Set-Sly2VRShortcuts $Script:Root } catch { Write-Host "Shortcuts not refreshed: $($_.Exception.Message)" }
