#!/usr/bin/env bash
# Makes GITSFA-Vita-DataBuilder-win64.zip: our tools (the public tree's tools/ and vm/, no game data), the embeddable Python
# with the wheels it needs, ffmpeg.exe (+ its licence and corresponding source), the C tools cross-built with mingw-w64,
# the DejaVu Sans Bold font, "Build GITS data.bat", README_FIRST.txt (the short version) and README.txt.
# usage: make_package.sh PKG_DIR OUT_DIR
#   PKG_DIR holds: dl/python-3.13.16-embed-amd64.zip, wheels/*.whl (numpy, pillow, scipy, etcpak, opencv-python-headless,
#   win_amd64 / cp313), ffmpeg.exe (made by build_ffmpeg.sh from dl/ffmpeg-6.1.1.tar.xz + dl/x264_*.orig.tar.gz),
#   dl/*debian.tar.xz (the patch sets), build_ffmpeg.sh
set -e
P="$(cd "${1:?usage: make_package.sh PKG_DIR OUT_DIR}" && pwd)"; O="$(mkdir -p "${2:?}" && cd "$2" && pwd)"
R="$(cd "$(dirname "$0")/../../.." && pwd)"; NAME=GITSFA-Vita-DataBuilder; B="$O/stage/$NAME"
rm -rf "$O/stage"; mkdir -p "$B"
crlf() { sed -e 's/\r$//' -e 's/$/\r/' "$1" > "$2"; }

# ---- our code: the public tree (git archive honours .gitattributes export-ignore), tools/ and vm/ only
git -C "$R" archive --format=tar HEAD tools vm | tar -x -C "$B"
rm -rf "$B/tools/databuilder/win"
bad=$(cd "$B" && find . -type f \( -iname '*.g[a-z][a-z]' -o -iname '*.gab' -o -iname '*.dds' -o -iname '*.tga' -o -iname '*.nif' -o -iname '*.pkl' \
      -o -iname '*.npy' -o -iname '*.sdf' -o -iname '*.wav' -o -iname '*.mp4' -o -iname '*.jpg' -o -iname '*.png' -o -iname '*.fsb' -o -iname '*.dat' \) | sort)
if [ -n "$bad" ]; then echo "NOT CLEAN:"; echo "$bad"; exit 1; fi

for f in tools/databuilder/extract.py tools/databuilder/datarc.py tools/databuilder/extract_list.txt tools/databuilder/check/extract_ref.txt; do
  [ -s "$B/$f" ] || { echo "missing $f"; exit 1; }
done

# ---- C tools, cross-built (ctools.py build-all with x86_64-w64-mingw32-gcc -static; meshoptimizer 0.23 as meshopt.dll)
W="$B/tools/databuilder/bin/win64"; mkdir -p "$W"
( cd "$R/tools/databuilder" && python3 ctools.py build-all "$W" x86_64-w64-mingw32-gcc )
x86_64-w64-mingw32-g++ -O2 -shared -static -o "$W/meshopt.dll" -DMESHOPTIMIZER_API='__declspec(dllexport)' \
    "$R/tools/lib/meshopt/simplifier.cpp" "$R/tools/lib/meshopt/allocator.cpp"
x86_64-w64-mingw32-gcc -O2 -shared -static -o "$W/adpcm.dll" "$R/tools/audio/adpcm.c" 2>/dev/null || cp "$P/winbin/adpcm.dll" "$W/"
x86_64-w64-mingw32-strip "$W"/*.exe "$W"/*.dll
cp "$R/tools/lib/meshopt/LICENSE.txt" "$W/meshoptimizer-LICENSE.txt"
cp /usr/share/doc/mingw-w64-common/copyright "$W/mingw-w64-runtime-LICENSE.txt"
cat > "$W/README.txt" <<'EOF'
Prebuilt Windows versions of the port's own C tools (sources in tools\): gchk.exe, portraits.exe, navedges.exe, navgen.exe,
glz.exe, vmvis.exe, introrender.exe (tools\databuilder\ctools.py builds them), adpcm.dll (tools\audio\adpcm.c) and meshopt.dll (meshoptimizer 0.23, MIT licence:
meshoptimizer-LICENSE.txt). Cross-built with mingw-w64 GCC 13, statically linked: the mingw-w64 runtime and winpthreads
notices are in mingw-w64-runtime-LICENSE.txt; the GCC runtime parts are under the GCC Runtime Library Exception.
EOF

# ---- Python 3.13 embeddable + wheels
Y="$B/python"; mkdir -p "$Y/Lib/site-packages"
( cd "$Y" && unzip -q "$P/dl/python-3.13.16-embed-amd64.zip" )
printf 'python313.zip\r\n.\r\nLib\\site-packages\r\nimport site\r\n' > "$Y/python313._pth"
cp "$R/tools/databuilder/win/sitecustomize.py" "$Y/sitecustomize.py"   # (the script's folder on sys.path; b122: '\n' line ends)
for w in "$P"/wheels/*.whl; do ( cd "$Y/Lib/site-packages" && unzip -q -o "$w" ); done
find "$Y/Lib/site-packages" -name '__pycache__' -prune -exec rm -rf {} + ; rm -rf "$Y/Lib/site-packages"/*/tests "$Y/Lib/site-packages/numpy"/*/tests
rm -rf "$Y/Lib/site-packages/scipy"/*/tests "$Y/Lib/site-packages/scipy"/*/*/tests
rm -f "$Y/Lib/site-packages/cv2"/opencv_videoio_ffmpeg*.dll   # (OpenCV's video I/O backend: not used, 31 MB)

# ---- fribidi (LGPL 2.1) for Pillow's text layout (raqm): without it Pillow on Windows falls back to its basic layout and
#      the "HOLD" label of tut_v1 / tut_v4 is kerned differently from Linux (which uses the system's libfribidi 1.0.13)
X="$O/fribidi-build"; rm -rf "$X"; mkdir -p "$X"; tar -xf "$P/dl/fribidi_1.0.13.orig.tar.xz" -C "$X"
( cd "$X/fribidi-1.0.13" && ./configure --host=x86_64-w64-mingw32 --enable-shared --disable-static --disable-docs > ../cfg.log 2>&1 && make -j2 -C lib > ../make.log 2>&1 )
x86_64-w64-mingw32-strip -o "$Y/libfribidi-0.dll" "$X/fribidi-1.0.13/lib/.libs/libfribidi-0.dll"
cp "$X/fribidi-1.0.13/COPYING" "$Y/libfribidi-LICENSE.txt"; rm -rf "$X"

# ---- ffmpeg (GPL: FFmpeg 6.1.1 + libx264) and its licence; the complete corresponding source of it and of fribidi
F="$B/ffmpeg"; mkdir -p "$F" "$B/source"
cp "$P/ffmpeg.exe" "$F/"
tar -xOf "$P/dl/ffmpeg-6.1.1.tar.xz" ffmpeg-6.1.1/COPYING.GPLv2 > "$F/LICENSE.txt"
cp "$P"/dl/ffmpeg-6.1.1.tar.xz "$P"/dl/x264_*.orig.tar.gz "$P"/dl/x264_*debian.tar.xz "$P/build_ffmpeg.sh" "$P"/dl/fribidi_1.0.13.orig.tar.xz "$B/source/"
cat > "$F/SOURCE.txt" <<EOF
ffmpeg.exe: FFmpeg 6.1.1 with libx264 0.164.3108+git31e19f9 (the versions the reference build used), cross-built for
64-bit Windows with mingw-w64 GCC 13 by ..\\source\\build_ffmpeg.sh. Licence: GNU GPL version 2 or later (LICENSE.txt),
as a build with --enable-gpl and libx264. Its complete corresponding source is in the source folder (SOURCES.txt there).
EOF
cat > "$B/source/SOURCES.txt" <<EOF
The complete corresponding source of the GPL / LGPL programs in this package:
  ffmpeg-6.1.1.tar.xz           FFmpeg 6.1.1 (https://ffmpeg.org/releases/ffmpeg-6.1.1.tar.xz)          -> ffmpeg\\ffmpeg.exe
  $(cd "$P/dl" && ls x264_*.orig.tar.gz)  the x264 snapshot as packaged by Debian / Ubuntu      -> ffmpeg\\ffmpeg.exe
  $(cd "$P/dl" && ls x264_*debian.tar.xz)  its packaging (the build applies properly_detect_x32.patch)
  build_ffmpeg.sh               the exact configure options and build steps of ffmpeg.exe (mingw-w64 GCC 13)
  fribidi_1.0.13.orig.tar.xz    GNU FriBidi 1.0.13 (LGPL 2.1), built with ./configure --host=x86_64-w64-mingw32
                                --enable-shared --disable-static --disable-docs; make -C lib      -> python\\libfribidi-0.dll
sha256:
$(cd "$B/source" && sha256sum *.tar.* | sed 's/^/  /')
EOF

# ---- font
mkdir -p "$B/font"; cp /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf "$B/font/"
cp /usr/share/doc/fonts-dejavu-core/copyright "$B/font/LICENSE.txt"

# ---- the .bat and the README (Windows line ends)
crlf "$R/tools/databuilder/win/Build GITS data.bat" "$B/Build GITS data.bat"
crlf "$R/tools/databuilder/win/README.txt" "$B/README.txt"
crlf "$R/tools/databuilder/win/README_FIRST.txt" "$B/README_FIRST.txt"
crlf "$R/LICENSE" "$B/LICENSE.txt"   # (our code in tools\ and vm\: GNU GPL v3)
for f in "$Y/sitecustomize.py" "$W/README.txt" "$F/SOURCE.txt" "$B/source/SOURCES.txt"; do crlf "$f" "$f.crlf" && mv "$f.crlf" "$f"; done

# ---- zip (deterministic order and times)
find "$B" -exec touch -h -d '2026-11-01 00:00:00' {} +
rm -f "$O/$NAME-win64.zip"; ( cd "$O/stage" && find "$NAME" -type f | LC_ALL=C sort | zip -q -X -9 -@ "$O/$NAME-win64.zip" )
echo "$O/$NAME-win64.zip: $(du -h "$O/$NAME-win64.zip" | cut -f1) ($(unzip -l "$O/$NAME-win64.zip" | tail -1 | awk '{print $2}') files, $(du -sh "$B" | cut -f1) unpacked)"
