@echo off
cd /d "%~dp0"
echo Starting Caring Node Edge AI Dashboard at http://localhost:8765/
echo Press Ctrl+C to stop this server.
python -m http.server 8765
