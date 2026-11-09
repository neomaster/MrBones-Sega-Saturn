@echo off
cd /d "%~dp0"
start "" "%~dp0build-static\mrbones.exe" "%~dp0disco1\Mr. Bones (USA) (Disc 1).cue" "%~dp0manifest\versions.txt" --disc2 "%~dp0disco2\Mr. Bones (USA) (Disc 2).cue"
