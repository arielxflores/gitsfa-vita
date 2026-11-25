#!/usr/bin/env bash
# Makes GITSFA-Vita-DataBuilder-win64.zip again from an EARLIER package and today's tree: the third-party parts of the
# earlier zip are kept as they are (python\, ffmpeg\, source\, font\ - nothing of ours), our code (tools\, vm\) comes from
# git HEAD's public tree, the C tools are cross-built again (mingw-w64 GCC 13), the .bat and the READMEs come from this
# folder. For when make_package.sh's PKG_DIR (the downloads and the ffmpeg cross build) is not at hand.
# usage: repackage.sh OLD_ZIP OUT_DIR
set -e
Z="$(cd "$(dirname "${1:?usage: repackage.sh OLD_ZIP OUT_DIR}")" && pwd)/$(basename "$1")"; O="$(mkdir -p "${2:?}" && cd "$2" && pwd)"
R="$(cd "$(dirname "$0")/../../.." && pwd)"; NAME=GITSFA-Vita-DataBuilder; B="$O/stage/$NAME"
rm -rf "$O/stage"; mkdir -p "$O/stage"; ( cd "$O/stage" && unzip -q "$Z" )
[ -d "$B/python" ] && [ -f "$B/ffmpeg/ffmpeg.exe" ] && [ -d "$B/source" ] && [ -d "$B/font" ] || { echo "the old zip has not the expected folders"; exit 1; }
OLDW="$O/oldwin64"; rm -rf "$OLDW"; mv "$B/tools/databuilder/bin/win64" "$OLDW"
rm -rf "$B/tools" "$B/vm" "$B/Build GITS data.bat" "$B/README.txt" "$B/README_FIRST.txt" "$B/LICENSE.txt"
crlf() { sed -e 's/\r$//' -e 's/$/\r/' "$1" > "$2"; }
git -C "$R" archive --format=tar HEAD tools vm | tar -x -C "$B"
cp "$B/tools/databuilder/win/sitecustomize.py" "$B/python/sitecustomize.py"   # (b122: ours - the script's folder on sys.path, '\n' line ends)
rm -rf "$B/tools/databuilder/win"
bad=$(cd "$B" && find tools vm -type f \( -iname '*.g[a-z][a-z]' -o -iname '*.gab' -o -iname '*.dds' -o -iname '*.tga' -o -iname '*.nif' -o -iname '*.pkl' \
      -o -iname '*.npy' -o -iname '*.sdf' -o -iname '*.wav' -o -iname '*.mp4' -o -iname '*.jpg' -o -iname '*.png' -o -iname '*.fsb' -o -iname '*.dat' \) | sort)
if [ -n "$bad" ]; then echo "NOT CLEAN:"; echo "$bad"; exit 1; fi
for f in tools/databuilder/extract.py tools/databuilder/datarc.py tools/databuilder/extract_list.txt tools/databuilder/check/extract_ref.txt; do
  [ -s "$B/$f" ] || { echo "missing $f"; exit 1; }
done
W="$B/tools/databuilder/bin/win64"; mkdir -p "$W"
# (the posix-threads variant of mingw-w64: gchk uses clock_gettime, which the win32 variant has not)
CC=x86_64-w64-mingw32-gcc; CXX=x86_64-w64-mingw32-g++; command -v $CC-posix >/dev/null && { CC=$CC-posix; CXX=$CXX-posix; }
( cd "$R/tools/databuilder" && python3 ctools.py build-all "$W" $CC )
$CXX -O2 -shared -static -o "$W/meshopt.dll" -DMESHOPTIMIZER_API='__declspec(dllexport)' \
    "$R/tools/lib/meshopt/simplifier.cpp" "$R/tools/lib/meshopt/allocator.cpp"
$CC -O2 -shared -static -o "$W/adpcm.dll" "$R/tools/audio/adpcm.c"
x86_64-w64-mingw32-strip "$W"/*.exe "$W"/*.dll
for f in "$OLDW"/*; do n="$(basename "$f")"; [ -e "$W/$n" ] || case "$n" in *.txt) cp "$f" "$W/";; *) echo "the old package had $n and today's tools do not build it"; exit 1;; esac; done
rm -rf "$OLDW"
grep -q introrender.exe "$W/README.txt" 2>/dev/null || sed -i 's/navedges.exe, navgen.exe\r\{0,1\}$/navedges.exe, navgen.exe, glz.exe, vmvis.exe, introrender.exe/' "$W/README.txt"   # (b119: the list of our own tools)
crlf "$R/tools/databuilder/win/Build GITS data.bat" "$B/Build GITS data.bat"
crlf "$R/tools/databuilder/win/README.txt" "$B/README.txt"
crlf "$R/tools/databuilder/win/README_FIRST.txt" "$B/README_FIRST.txt"
crlf "$R/LICENSE" "$B/LICENSE.txt"   # (our code in tools\ and vm\: GNU GPL v3)
# nothing of the game, of Sony, or of this machine
bad=$(cd "$B" && find . -type f \( -iname '*.suprx' -o -iname '*.skprx' -o -iname '*.self' -o -iname 'eboot.bin' -o -iname '*.vpk' -o -iname '*.dat' -o -iname '*.fsb' -o -iname '*.nif' -o -iname '*.dds' \) | sort)
if [ -n "$bad" ]; then echo "NOT CLEAN:"; echo "$bad"; exit 1; fi
find "$B" -exec touch -h -d '2026-11-01 00:00:00' {} +
rm -f "$O/$NAME-win64.zip"; ( cd "$O/stage" && find "$NAME" -type f | LC_ALL=C sort | zip -q -X -9 -@ "$O/$NAME-win64.zip" )
echo "$O/$NAME-win64.zip: $(du -h "$O/$NAME-win64.zip" | cut -f1) ($(unzip -l "$O/$NAME-win64.zip" | tail -1 | awk '{print $2}') files, $(du -sh "$B" | cut -f1) unpacked)"
