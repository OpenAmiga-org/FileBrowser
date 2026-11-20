#!/bin/sh
# Build the FileBrowser developer release: $OUT/FileBrowser.lha
#
#   tools/make_lha.sh
#
# FileBrowser/
#   FileBrowser.readme, LICENSE
#   Autodocs/FileBrowser.doc
#   Include/FileBrowser.h
#   Source/FileBrowser.c
#   Demo/FileBrowserDemo (built here), FileBrowserDemo.c, makefile
#
# The demo is cross-built in the walkero OS4 gcc docker image.
# LHA:     LHa for UNIX (level 2 headers), default ~/linux_archivers/lha/lha
# ARCEDIT: host arcedit, sets Amiga protection bits (LHa for UNIX stores Unix ones)
# OUT:     output drawer, default ~/build/filebrowser
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LHA="${LHA:-$HOME/linux_archivers/lha/lha}"
ARCEDIT="${ARCEDIT:-$HOME/arcedit/bin/arcedit}"
OUT="${OUT:-$HOME/build/filebrowser}"
IMAGE=walkero/amigagccondocker:os4-gcc15-3.6.1
I="$ROOT/package/icons"

ver=$(sed -n 's/^#define FILEBROWSER_VERSION *\([0-9]*\).*/\1/p' "$ROOT/src/FileBrowser.h").$(sed -n 's/^#define FILEBROWSER_REVISION *\([0-9]*\).*/\1/p' "$ROOT/src/FileBrowser.h")
grep -q "^version: $ver\$" "$ROOT/package/FileBrowser.readme" || { echo "FileBrowser.readme is not version $ver" >&2; exit 1; }
# OS4Depot shows this as the homepage: the OpenAmiga.org project page
URL='http://openamiga.org/?function=gitproject&name=FileBrowser'
grep -qxF "url: $URL" "$ROOT/package/FileBrowser.readme" || { echo "FileBrowser.readme: url must be $URL" >&2; exit 1; }

STAGE="$OUT/stage"
D="$STAGE/FileBrowser"
rm -rf "$STAGE"
mkdir -p "$D/Autodocs" "$D/Include" "$D/Source" "$D/Demo"

cp "$ROOT/package/FileBrowser.readme" "$ROOT/LICENSE" "$D/"
cp "$ROOT/documentation/FileBrowser.doc" "$D/Autodocs/"
cp "$ROOT/src/FileBrowser.h" "$D/Include/"
cp "$ROOT/src/FileBrowser.c" "$D/Source/"
cp "$ROOT/src/FileBrowserDemo.c" "$D/Demo/"
# the demo's makefile, for this layout
cat > "$D/Demo/makefile" <<'MK'
#
# FileBrowserDemo - example for the FileBrowser gadget class
# http://openamiga.org/?function=viewproject&projectid=52
#
# "make" builds FileBrowserDemo from this drawer, ../Source/FileBrowser.c
# and ../Include/FileBrowser.h. With an SDK that has only "gcc":
# make CC=gcc
#

CC      = ppc-amigaos-gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -I../Include
LDFLAGS =
LIBS    =

all: FileBrowserDemo

FileBrowserDemo: FileBrowserDemo.o FileBrowser.o
	$(CC) $(LDFLAGS) -o $@ FileBrowserDemo.o FileBrowser.o $(LIBS)

FileBrowser.o: ../Source/FileBrowser.c ../Include/FileBrowser.h
	$(CC) $(CFLAGS) -c -o $@ ../Source/FileBrowser.c

FileBrowserDemo.o: FileBrowserDemo.c ../Include/FileBrowser.h
	$(CC) $(CFLAGS) -c -o $@ FileBrowserDemo.c

clean:
	rm -f *.o FileBrowserDemo

.PHONY: all clean
MK

# build the demo with that makefile, as a developer would
sudo docker run --rm -u "$(id -u):$(id -g)" -v "$D:/d" -w /d/Demo $IMAGE \
	sh -c 'make -s && ppc-amigaos-strip --strip-all -R .comment FileBrowserDemo && rm -f *.o'

# icons
cp "$I/drawer.info" "$STAGE/FileBrowser.info"
for d in Autodocs Include Source Demo; do cp "$I/drawer.info" "$D/$d.info"; done
cp "$I/text.info" "$D/FileBrowser.readme.info"
cp "$I/text.info" "$D/Autodocs/FileBrowser.doc.info"
cp "$I/FileBrowserDemo.info" "$D/Demo/"

ARC="$OUT/FileBrowser.lha"
rm -f "$ARC"
(cd "$STAGE" && "$LHA" aq2o5 "$ARC" FileBrowser.info FileBrowser)

L="$STAGE/protect"
"$LHA" lq "$ARC" | awk '{print $NF}' > "$L.all"
grep '/$' "$L.all" > "$L.dirs"
grep -v '/$' "$L.all" > "$L.files"
echo "FileBrowser/Demo/FileBrowserDemo" > "$L.exe"
# drawers first: PROTECT on a drawer name also sets everything inside it
"$ARCEDIT" "$ARC" FROM "$L.dirs" PROTECT rwed QUIET
"$ARCEDIT" "$ARC" FROM "$L.files" PROTECT rwd QUIET
"$ARCEDIT" "$ARC" FROM "$L.exe" PROTECT rwed QUIET
"$LHA" t "$ARC" > /dev/null
# the GitHub page's changelog, from the readme
python3 -I "$ROOT/tools/update_readme.py"
echo "FileBrowser $ver -> $ARC ($(stat -c %s "$ARC") bytes)"
