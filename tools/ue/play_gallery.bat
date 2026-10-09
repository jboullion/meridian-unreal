@echo off
rem Double-click to play the prop gallery (tools/ue/play_gallery.ps1; arguments pass through, e.g. -GameHour 23).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_gallery.ps1" %*
if errorlevel 1 pause
