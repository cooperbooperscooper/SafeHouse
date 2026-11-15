@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\update.ps1" -Zip "%~1"
