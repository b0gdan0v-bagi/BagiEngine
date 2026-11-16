@echo off
rem Starts the portable ComfyUI of this folder for this PC only (tools/artgen/CLAUDE.md in BagiEngine). install.ps1 copies it here.
cd /d "%~dp0"
.\python_embeded\python.exe -s ComfyUI\main.py --windows-standalone-build --listen 127.0.0.1 --port 8188 --disable-auto-launch %*
pause
