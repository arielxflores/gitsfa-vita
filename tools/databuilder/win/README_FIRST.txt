GITS: SAC First Assault for PS Vita - game data builder (Windows)
================================================================

This makes the data for the Vita port on your PC, from your own copy of the PC game
Ghost in the Shell: Stand Alone Complex - First Assault Online.

1. Unzip this zip to a folder with a short path on a drive with enough free space,
   for example C:\GITSFA.
2. Double-click "Build GITS data.bat".
3. When it asks where the game is, drag your install's "Game Files" folder (the one with the
   Data folder inside) into the window and press Enter.
4. Wait until it says Done: about an hour (30 to 90 minutes), depending on the PC.
5. Copy the new "gitsfa" folder (it appears next to "Build GITS data.bat") to ux0:data/ on
   your Vita, so that you have ux0:data/gitsfa/hud.ghud and the other files.

Good to know
- It only reads the install's "Game Files" folder and never changes it. Nothing is downloaded
  or sent anywhere.
- Free disk space: about 8 GB on the drive you unzipped to. The "work" folder grows to about 7 GB
  (files unpacked from the game, plus working copies); the finished "gitsfa" folder is about
  850 MB. Delete the "work" folder when you are done.
- You can close the window at any time: run the .bat again and it continues where it stopped.
- More details, how the intro movie is made and what to do if a step fails: README.txt.
