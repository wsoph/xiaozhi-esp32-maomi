@echo off
cd /d "%~dp0.."
python scripts\maomi_simulator.py
if errorlevel 1 pause
