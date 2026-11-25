The first public test of the PS Vita version of *Ghost in the Shell: Stand Alone Complex - First Assault Online*: the whole game offline with bots, and rooms for up to 8 players over ad-hoc, the same Wi-Fi or the internet (the host shows a room code, the others type it in).

**You need your own copy of the PC game.** Its data is not in this download: `GITSFA-Vita-DataBuilder-win64.zip` makes it on your PC from your install (about an hour), then the `gitsfa` folder goes to `ux0:data/` on the Vita. Also needed: `libshacccg.suprx` in `ur0:data/`.

1. Unzip the data builder, run `Build GITS data.bat`, drag your install's `Game Files` folder into the window.
2. Copy the `gitsfa` folder it makes to `ux0:data/`.
3. Install `GITSFA-Vita.vpk` with VitaShell.

This is a test release. It has run on one PS Vita, and multiplayer has only been tried with one Vita and a computer playing the second player. Everyone in a room needs this same release. Please report what breaks in the [issues](../../issues), with `ux0:data/gitsfa/boot.log` (delete its lines starting with `net:` first if you used an internet room: they hold your internet address).

The README has the controls, how internet rooms work and the known limits.
