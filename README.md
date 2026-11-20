# FileBrowser

A listbrowser.gadget subclass for AmigaOS 4 that can also show its list
as icons, like a Workbench drawer window. You add it to your program as
source.

In the default details mode the gadget *is* a ListBrowser: every method
goes to listbrowser.gadget, so you can replace a ListBrowser with a
FileBrowser and nothing changes. One attribute switches to the icon view:

    SetGadgetAttrs(gad, win, NULL, FILEBROWSER_ViewMode, FBVM_ICONS, TAG_DONE);

The list, the column info and your event handling stay as they are, in
both views. In the icon view each node is a cell (image or icon on top,
column texts below), with click, Shift-click, double-click, a selection
box, the mouse wheel and dragging.

Built on listbrowser.gadget, scroller.gadget and the AmigaOS interfaces
only (no C library).

Download: [FileBrowser on OS4Depot](https://os4depot.net/?function=showfile&file=development/library/gui/filebrowser.lha)
(developer release: source, Autodoc, ready-built demo).

## Layout

- `src/`            the class (`FileBrowser.c`, `FileBrowser.h`), the demo
                    (`FileBrowserDemo.c`: lists a drawer, a button switches
                    views) and its `makefile`
- `documentation/`  `FileBrowser.doc`: the Autodoc for every attribute
- `package/`        the release readme (`FileBrowser.readme`: description,
                    how to use the class, version history) and the icons
                    for the archive
- `tools/`          `make_lha.sh` (release archive), `os4depot_submit.py`,
                    `update_readme.py` (the changelog below, from the
                    readme)

## Using it

Add `src/FileBrowser.c` and `src/FileBrowser.h` to your program's build,
open the interfaces the class uses (IExec, IIntuition, IGraphics,
ILayers, IUtility, IIcon, IListBrowser), make the class with
`MakeFileBrowserClass()`, make gadgets with `NewObject(cl, NULL, ...)`
and free the class with `FreeFileBrowserClass()` when the windows are
gone. The class is private: objects are made with the class pointer, not
by name.

## Building

    cd src && make                 # FileBrowserDemo with ppc-amigaos-gcc
                                   # (make CC=gcc with an SDK that has only gcc)
    tools/make_lha.sh              # release archive -> ~/build/filebrowser/FileBrowser.lha
    tools/os4depot_submit.py       # what would go to OS4Depot; --send sends it
                                   # (Replace file form, OS4DEPOT_EMAIL set)

`make_lha.sh` cross-builds the demo in the walkero AmigaOS 4 GCC docker
image (`walkero/amigagccondocker:os4-gcc15-3.6.1`), checks that the
readme's version matches `FILEBROWSER_VERSION`/`FILEBROWSER_REVISION` in
`FileBrowser.h`, sets the Amiga protection bits with arcedit and updates
the changelog here.

## Design notes

- Details mode: every method goes to listbrowser.gadget. After OM_SET the
  class asks it to redraw (BOOPSI classes render after OM_SET only when
  they are the object's own class, so a ListBrowser would not redraw
  itself as a superclass).
- Icon mode: GM_RENDER, GM_HITTEST, GM_GOACTIVE, GM_HANDLEINPUT,
  GM_HANDLESCROLL and GM_GOINACTIVE are handled by the class, and OM_GET
  answers the selection attributes (Selected, SelectedNode, NumSelected,
  RelEvent, Top, TotalNodes) itself. OM_SET still reaches the ListBrowser,
  without a GadgetInfo so it cannot draw, which keeps its state current.
  Back in details mode the labels are attached again, so the ListBrowser
  counts the selection made in icon mode.
- Cells are drawn into an offscreen bitmap (a friend of the window's,
  with one row of room above and below for partly shown rows) and copied
  in one blit, so scrolling does not flicker. Without memory for it the
  cells are drawn directly, clipped to the cell area.
- Everything icon mode draws is clipped (a layers clip region) to the
  gadget and to the inside of the window's borders. The thin gaps between
  the frame, the cells and the scroll bar are painted too, so nothing of
  the list view shows through after switching.
- The scroll bar is a scroller.gadget, as in the list view; input on it is
  passed on with the mouse position moved to the scroller's box. Without
  gadgets/scroller.gadget the class draws its own bar.
- Icons come from your `FILEBROWSER_IconHook` (a `struct DiskObject` you
  own; it may be called from input.device, so no DOS I/O in it), drawn
  with `DrawIconState()` bottom aligned like Workbench. With
  `FILEBROWSER_IconSize` larger icons are scaled down to the box, keeping
  their aspect ratio (icon.library caches the scaled imagery). Without an
  icon the node's `LBNCA_Image` is drawn.

## Changelog

<!-- changelog start: made by tools/update_readme.py from package/FileBrowser.readme -->
- **1.2** (2026-11-16):
  - Icon view uses a scroller.gadget for its scroll bar, so it looks and works like the one in details mode (arrows, knob, GUI settings). The class opens gadgets/scroller.gadget; without it the old bar is drawn.
  - Everything icon view draws is clipped to the gadget and to the inside of the window's borders: a scroll bar piece was once left on a window's status bar and border.
- **1.1** (2026-11-15):
  - Dragging a selection box past the top or bottom of the view (which scrolls it) no longer draws lines of the box outside the view. The box's corner stays inside the view while it scrolls, so the box is always seen.
  - The mouse wheel also scrolls while a selection box is dragged.
  - Icon view paints the thin gaps around the cells and the scroll bar; bits of the list view showed through there after switching views.
- **1.0** (2026-11-15): First release.
<!-- changelog end -->

## History

The project was created on OpenAmiga.org in 2009. FileBrowser was written
in November 2026 for Filer and released as 1.0, 1.1 and 1.2 from the
OpenAmiga.org Subversion repository. It moved to GitHub in November 2026
with its full commit history.

Public domain, see `LICENSE` (The Unlicense).
