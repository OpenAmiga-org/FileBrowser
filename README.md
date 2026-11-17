# FileBrowser

FileBrowser: listbrowser.gadget subclass for AmigaOS 4 with an icon (cell)
view like a Workbench drawer, added to your program as source. Public
domain.

Version 1.2 (2026-11-16).

In the default details mode the gadget *is* a ListBrowser: every method goes
to listbrowser.gadget, so you can replace a ListBrowser with a FileBrowser
and nothing changes. One attribute switches to the icon view:

    SetGadgetAttrs(gad, win, NULL, FILEBROWSER_ViewMode, FBVM_ICONS, TAG_DONE);

The list, the column info and your event handling stay as they are, in both
views. The icon view lays the nodes out as cells (image or icon on top,
column texts below), with click, Shift-click, double-click, a selection box,
the mouse wheel and dragging, drawn through an offscreen bitmap.

`package/FileBrowser.readme` is the release readme with the full
description, how to use the class and the version history;
`documentation/FileBrowser.doc` is the Autodoc for every attribute.

## Using it

Add `src/FileBrowser.c` and `src/FileBrowser.h` to your program's build,
open the interfaces the class uses (IExec, IIntuition, IGraphics, ILayers,
IUtility, IIcon, IListBrowser), make the class with `MakeFileBrowserClass()`
and free it with `FreeFileBrowserClass()` when the windows are gone.

## Building

`src/makefile` builds the demo, `FileBrowserDemo`, with `ppc-amigaos-gcc`
(`make CC=gcc` with an SDK that only has `gcc`). `tools/make_lha.sh` makes
the developer release archive `FileBrowser.lha` (demo cross-built in the
walkero AmigaOS 4 GCC docker image), and `tools/os4depot_submit.py` submits
it to OS4Depot.

## Contents

- `src/`: the class (`FileBrowser.c`, `FileBrowser.h`), the demo and the
  makefile.
- `documentation/FileBrowser.doc`: the Autodoc.
- `package/`: the release readme and the icons for the archive.
- `tools/`: release and OS4Depot scripts.
- `LICENSE` (The Unlicense), `contributors.txt`.

## History

The project was created on OpenAmiga.org in 2009. FileBrowser was written
in November 2026 for Filer, released as 1.0 and 1.1 (on OS4Depot) and 1.2,
in the OpenAmiga.org Subversion repository. Moved to GitHub in November
2026, with its full commit history.
