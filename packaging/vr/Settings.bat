@echo off
if exist "%~dp0Sly2VR.ini" (
  start "" notepad "%~dp0Sly2VR.ini"
) else (
  echo Sly2VR.ini is written the first time the game starts in VR. Play once, then open Settings again.
  pause
)
