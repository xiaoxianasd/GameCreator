@echo off
rem Launch GameCreator (Qt runtime is deployed into the build dir by build.bat)
cd /d "%~dp0build"
start "" GameCreator.exe
