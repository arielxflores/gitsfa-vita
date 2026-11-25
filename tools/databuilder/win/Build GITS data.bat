@echo off
rem GITS: SAC First Assault (PS Vita port) - builds the game data (the gitsfa folder for ux0:data/) on Windows,
rem from YOUR OWN install of Ghost in the Shell: Stand Alone Complex - First Assault Online. Everything it needs is in this folder.
setlocal EnableExtensions
title GITS:FA Vita data builder
cd /d "%~dp0"
set "PATH=%~dp0python;%~dp0ffmpeg;%PATH%"
set "PYTHONUTF8=1"
set "PYTHONHOME="
set "PYTHONPATH="
if not exist "%~dp0work\tmp" mkdir "%~dp0work\tmp"
set "TMPDIR=%~dp0work\tmp"
set "TEMP=%~dp0work\tmp"
set "TMP=%~dp0work\tmp"
set "GITSFA_FFMPEG=%~dp0ffmpeg\ffmpeg.exe"
set "GITSFA_FONT_BOLD=%~dp0font\DejaVuSans-Bold.ttf"
echo GITS: SAC First Assault (PS Vita) - game data builder
echo.
echo This makes the Vita game's data from your own copy of the PC game. It needs about 8 GB of free space
echo and takes about an hour (30 to 90 minutes). You can stop it at any time: running it again continues where it was.
echo.
rem ---- the game: the "Game Files" folder (dropped on this file, remembered from last time, or asked for)
set "GAME=%~1"
if "%GAME%"=="" if exist "%~dp0work\game.txt" set /p GAME=<"%~dp0work\game.txt"
if "%GAME%"=="" (
  echo Where is the game? Drag its "Game Files" folder ^(the one that holds the Data folder^) into this window,
  set /p GAME=then press Enter: 
)
if defined GAME set "GAME=%GAME:"=%"
rem (no trailing backslashes: before a closing quote they would escape it in the command line below)
:strip
if defined GAME if "%GAME:~-1%"=="\" (set "GAME=%GAME:~0,-1%" & goto strip)
rem (the Data folder itself, or the folder above "Game Files", dropped by mistake: use the right one)
if defined GAME if exist "%GAME%\map.dat" for %%I in ("%GAME%\..") do set "GAME=%%~fI"
if defined GAME if exist "%GAME%\Game Files\Data\map.dat" set "GAME=%GAME%\Game Files"
if not exist "%GAME%\Data\map.dat" (
  echo.
  echo Could not find Data\map.dat in "%GAME%".
  echo Choose the "Game Files" folder of the install: it holds a folder named Data with map.dat, ui.dat and others.
  if exist "%~dp0work\game.txt" del "%~dp0work\game.txt"
  pause
  exit /b 1
)
> "%~dp0work\game.txt" echo %GAME%
rem ---- the intro movie is drawn from the game's own scene files (the game plays it live; there is no video of it to convert).
rem      Optional second argument: your own video recording of the intro, used instead (see README.txt).
set "INTRO=%~2"
if defined INTRO set "INTRO=%INTRO:"=%"
set "INTROARGS="
if not "%INTRO%"=="" (
  set "START=23.7"
  set /p START=At how many seconds into that video does the intro start? ^(Enter = 23.7^) 
  call set "INTROARGS=--intro-video "%%INTRO%%" --intro-start %%START%%"
)
echo.
rem (%~dp0 ends with a backslash, which before a closing quote would escape it: "%~dp0." is the same folder)
"%~dp0python\python.exe" "%~dp0tools\databuilder\build_data.py" --game "%GAME%" --out "%~dp0." --work "%~dp0work" %INTROARGS%
if errorlevel 1 (
  echo.
  echo Something went wrong: see the messages above. Running this again continues where it stopped.
  pause
  exit /b 1
)
echo.
echo Done. Copy the "gitsfa" folder in this folder to ux0:data/ on your Vita (so that you have
echo ux0:data/gitsfa/hud.ghud and the other files), install the game's VPK, and start it.
echo The "work" folder can be deleted now.
explorer "%~dp0."
pause
