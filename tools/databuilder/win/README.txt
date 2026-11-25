GITS: SAC First Assault (PS Vita port) - game data builder for Windows
=====================================================================

The PS Vita port needs the game's data, which can't be given out with it. This tool makes that data
on your PC from your own copy of Ghost in the Shell: Stand Alone Complex - First Assault Online.
Nothing is downloaded and nothing is sent anywhere: everything comes from your install.

What you need
- Your install of the PC game. The builder reads its "Game Files" folder: the one that holds a
  folder named Data with map.dat, model.dat, weapon.dat, effect.dat, ui.dat, world.dat,
  worldEnv.dat, an anim folder and an audio folder. Your install is only read, never changed.
- About 8 GB of free disk space while it runs: the work folder grows to about 7 GB (files
  unpacked from the game's archives, plus working copies); the result itself is about 850 MB.
- About an hour (30 to 90 minutes), depending on the PC. Windows 10 or 11, 64-bit.

How to use it
1. Unzip this whole folder somewhere with a short path, for example C:\GITSFA. (Long paths, such
   as a folder deep inside Downloads, can make Windows refuse some files.)
2. Double-click "Build GITS data.bat". When it asks where the game is, drag the game's
   "Game Files" folder into the window and press Enter. (You can also drop the folder onto
   "Build GITS data.bat".) If you drop the Data folder or the folder above "Game Files" instead,
   it finds the right one by itself. It remembers the folder for next time.
3. Wait until it says Done. You can stop it at any time (close the window): run it again and it
   continues where it was.
4. Copy the "gitsfa" folder it made (next to "Build GITS data.bat") to ux0:data/ on your Vita with
   VitaShell (USB or FTP), so that you have ux0:data/gitsfa/hud.ghud and the other files.
5. Install the game's VPK if you haven't yet, and start the game.

Windows may warn about running a .bat file downloaded from the internet: choose "More info",
then "Run anyway". When everything works, the "work" folder can be deleted.

The intro movie
The original game draws its intro scene live, so your install has no video of it. The builder
draws that scene itself, on your PC, from the scene's own files in your install (the room, the
characters, the cameras, the music and voices), and makes the video the Vita plays. This takes a
few minutes and needs no graphics card. It follows the game's scene shot by shot; the light on
the characters is our own approximation of the game's.
If you would rather use your own video recording of the game's intro, drop the "Game Files"
folder and then the video file onto "Build GITS data.bat" as two arguments from a command prompt:
  "Build GITS data.bat" "C:\...\Game Files" "C:\...\my recording.mp4"
It asks for the time in seconds where the intro begins in the recording (Enter = 23.7), cuts 41
seconds from there and converts them.

Checking the result
At the end the builder compares every file it made with the reference build, by checksum only,
and says how many are fine. The start-up, tutorial, operator and intro videos can come out with slightly
different bytes on some PCs (the video encoder and the intro's drawing round a little differently); those are checked by playing
them through instead, and count as fine. If it lists a file as DIFFERENT, the data will most likely
still work, but please report it together with the log (see below).

If the intro movie cannot be made on your PC, the builder says "LEFT OUT intro movie" and finishes
the rest: the game then starts without the intro, and everything else works.

If something goes wrong
The window shows which step failed and the last lines of its log. Every step's full log is in
work\logs\<step>.log. Fix the cause (for example free some disk space) and run the .bat again:
finished steps are kept. To start completely afresh, delete the "work" and "gitsfa" folders.

What is included
- This package's own programs (tools\ and vm\, "Build GITS data.bat"): GNU General Public License,
  version 3 (LICENSE.txt). Their source is these folders and the project's repository.
- Python 3.13 (python.org embeddable package, PSF licence: python\LICENSE.txt) with numpy, Pillow,
  SciPy, etcpak (+ archspec) and opencv-python-headless (their licences are in python\Lib\site-packages).
- FFmpeg with libx264 (GPL: ffmpeg\LICENSE.txt) and GNU FriBidi (LGPL: python\libfribidi-LICENSE.txt),
  with their complete source code in the source folder.
- The DejaVu Sans Bold font (DejaVu fonts licence: font\LICENSE.txt).
- meshoptimizer 0.23 (MIT) and the port's own conversion tools (tools\), with small parameter
  tables of our own (tools\databuilder\params). No game data is included.
