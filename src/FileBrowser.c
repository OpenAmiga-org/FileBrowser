/*
** File:        FileBrowser.c
** Description: FileBrowser gadget class, a subclass of listbrowser.gadget
**              with an icon (cell) view mode.
**
** OpenAmiga.org
** This file is part of the OpenAmiga "Listbrowser gadget with cell mode"
** project.
**
** For more information look at:
** http://openamiga.org/?function=viewproject&projectid=52
**
**  FBVM_DETAILS (default): every method goes to listbrowser.gadget, so the
**  gadget looks and behaves exactly like a ListBrowser.
**
**  FBVM_ICONS: each list node is drawn as a cell (image on top, one line per
**  text column below), cells flow left to right like text and the view
**  scrolls vertically. Rendering, hit testing and input are handled here; the
**  attributes an application reads or sets on a ListBrowser (Labels,
**  Selected, SelectedNode, NumSelected, RelEvent, Top, MakeVisible,
**  Position, MultiSelect, PersistSelect) keep working, so switching modes
**  needs no other changes in the application. OM_SET/OM_UPDATE still reach
**  the listbrowser in icon mode (with no GadgetInfo, so it can't draw), which
**  keeps its state current for switching back.
**
**  The class uses the global interface pointers of the application:
**  IExec, IIntuition, IGraphics, ILayers, IUtility, IIcon and IListBrowser
**  must be open before MakeFileBrowserClass() is called.
**
** Change history:
**  2026-11-14 orgin, created
**  2026-11-14 orgin, FILEBROWSER_IconSize
**  2026-11-14 orgin, FILEBROWSER_IconSize sets a fixed image box
*/

#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <intuition/cghooks.h>
#include <intuition/screens.h>
#include <graphics/gfxmacros.h>
#include <graphics/regions.h>
#include <workbench/icon.h>
#include <gadgets/listbrowser.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <proto/listbrowser.h>

#include "FileBrowser.h"

#define FB_MAXCOLS   16
#define FB_PAD        3   /* space around cell contents */
#define FB_SBWIDTH   16   /* own scroll bar in icon mode */
#define FB_MAXIMAGE 256   /* taller images are clipped */

struct FBData
{
 uint32 mode;
 struct List *labels;
 uint32 cellChars;
 uint32 hiddenColumns;
 int32  bgPen;
 struct Hook *iconHook;
 uint32 iconSize;                 /* FILEBROWSER_IconSize, 0 = original size */
 BOOL   multiSelect;
 BOOL   persistSelect;

 /* icon mode layout, recalculated before each render */
 struct IBox box, area, sb;
 int32  slotW, cellW, cellH, lineH, imgH;
 int32  cols, rows, total, contentH, topY;
 int32  textCols[FB_MAXCOLS], numTextCols;

 /* icon mode selection / events */
 int32  relEvent;
 int32  selIndex;
 struct Node *selNode;
 struct Node *lastClickNode;
 uint32 lastSecs, lastMics;
 BOOL   moved, collapseOnRelease;
 BOOL   sbDrag;
 int32  sbGrab;
 BOOL   lasso, lassoAdd;          /* rubber band selection from empty space */
 struct BitMap *buf;              /* offscreen buffer for the cell area */
 int32  bufW, bufH;
 int32  lx0, ly0, lx1, ly1;       /* lasso corners, content coordinates */
};

/****************************************************************************/
/* helpers                                                                  */
/****************************************************************************/

/*
** Node at position index (0 = first) of list.
**
** Returns NULL if the list is shorter.
*/
static struct Node *NodeAt(struct List *list, int32 index)
{
 struct Node *n;
 int32 i = 0;
 if(list==NULL || index<0) return NULL;
 for(n = list->lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
 {
  if(i==index) return n;
 }
 return NULL;
}

/*
** Number of nodes in list. NULL or ~0 lists count as empty.
*/
static int32 CountNodes(struct List *list)
{
 struct Node *n;
 int32 i = 0;
 if(list==NULL) return 0;
 for(n = list->lh_Head; n->ln_Succ; n = n->ln_Succ) i++;
 return i;
}

/*
** TRUE if the listbrowser node is selected.
*/
static BOOL IsSelected(struct Node *node)
{
 uint32 sel = 0;
 IListBrowser->GetListBrowserNodeAttrs(node, LBNA_Selected, &sel, TAG_DONE);
 return sel ? TRUE : FALSE;
}

/*
** Select or deselect a listbrowser node (no redraw).
*/
static void SetSelected(struct Node *node, BOOL sel)
{
 IListBrowser->SetListBrowserNodeAttrs(node, LBNA_Selected, sel, TAG_DONE);
}

/*
** Deselect every node of the gadget's list (no redraw).
*/
static void SelectNone(struct FBData *d)
{
 struct Node *n;
 if(d->labels==NULL) return;
 for(n = d->labels->lh_Head; n->ln_Succ; n = n->ln_Succ)
 {
  if(IsSelected(n)) SetSelected(n, FALSE);
 }
}

/*
** Number of selected nodes in the gadget's list.
*/
static int32 CountSelected(struct FBData *d)
{
 struct Node *n;
 int32 cnt = 0;
 if(d->labels==NULL) return 0;
 for(n = d->labels->lh_Head; n->ln_Succ; n = n->ln_Succ)
 {
  if(IsSelected(n)) cnt++;
 }
 return cnt;
}

/*
** Image of a node: the first column that has one, or NULL.
*/
static struct Image *NodeImage(struct Node *node)
{
 uint32 numcols = 0, c;
 Object *image = NULL;
 IListBrowser->GetListBrowserNodeAttrs(node, LBNA_NumColumns, &numcols, TAG_DONE);
 for(c = 0; c < numcols && c < FB_MAXCOLS; c++)
 {
  image = NULL;
  IListBrowser->GetListBrowserNodeAttrs(node, LBNA_Column, c, LBNCA_Image, &image, TAG_DONE);
  if(image) return (struct Image *)image;
 }
 return NULL;
}

/*
** Full size icon for a node from the application's
** FILEBROWSER_IconHook, or NULL when there is no hook or no icon.
**
** The icon is owned by the application.
*/
static struct DiskObject *NodeIcon(struct FBData *d, Object *o, struct Node *node)
{
 if(d->iconHook == NULL) return NULL;
 return (struct DiskObject *)IUtility->CallHookPkt(d->iconHook, o, node);
}

/*
** Size of what is drawn for a node: its icon (at original size, or scaled
** down to fit FILEBROWSER_IconSize), else its image. Returns 0x0 for a node
** with neither.
**
** Returns TRUE if the icon must be drawn scaled to w x h.
*/
static BOOL NodeVisualSize(struct FBData *d, Object *o, struct RastPort *rp, struct Node *node, int32 *w, int32 *h)
{
 struct DiskObject *dobj = NodeIcon(d, o, node);
 *w = *h = 0;
 if(dobj)
 {
  struct Rectangle r;
  if(IIcon->GetIconRectangle(rp, dobj, NULL, &r, ICONDRAWA_Borderless, TRUE, TAG_DONE))
  {
   int32 max = (int32)d->iconSize;

   *w = r.MaxX - r.MinX + 1;
   *h = r.MaxY - r.MinY + 1;
   /* larger icons are scaled down, keeping the aspect ratio; smaller ones
      are never scaled up */
   if(max > 0 && (*w > max || *h > max))
   {
    if(*w >= *h)
    {
     *h = *h * max / *w;
     *w = max;
    }
    else
    {
     *w = *w * max / *h;
     *h = max;
    }
    if(*w < 1) *w = 1;
    if(*h < 1) *h = 1;
    return TRUE;
   }
   return FALSE;
  }
 }
 {
  struct Image *image = NodeImage(node);
  if(image)
  {
   *w = image->Width;
   *h = image->Height;
  }
 }
 return FALSE;
}

/****************************************************************************/
/* layout                                                                   */
/****************************************************************************/

/*
** Recalculate the icon mode layout from the gadget box, font and list:
** cell size, columns, rows, scroll bar and the visible text columns.
**
** Must be called before rendering or hit testing in icon mode.
*/
static void Layout(struct FBData *d, Object *o, struct RastPort *rp)
{
 struct Gadget *g = (struct Gadget *)o;
 struct TextFont *font = rp->Font;
 struct Node *n;
 int32 avg, areaH, maxTop;
 uint32 numcols = 0, c;

 d->box.Left = g->LeftEdge;
 d->box.Top = g->TopEdge;
 d->box.Width = g->Width;
 d->box.Height = g->Height;

 d->sb.Width = FB_SBWIDTH;
 d->sb.Height = d->box.Height - 4;
 d->sb.Left = d->box.Left + d->box.Width - 2 - FB_SBWIDTH;
 d->sb.Top = d->box.Top + 2;

 d->area.Left = d->box.Left + 2;
 d->area.Top = d->box.Top + 2;
 d->area.Width = d->sb.Left - d->area.Left - 1;
 d->area.Height = d->box.Height - 4;
 if(d->area.Width < 1) d->area.Width = 1;
 if(d->area.Height < 1) d->area.Height = 1;

 d->lineH = font->tf_YSize + 1;
 avg = IGraphics->TextLength(rp, "abcdefghijklmnopqrstuvwxyz", 26) / 26;
 if(avg < 1) avg = font->tf_XSize ? font->tf_XSize : 8;
 d->cellW = avg * d->cellChars + 2 * FB_PAD;

 /* text columns (from the first node) and the tallest image */
 d->numTextCols = 0;
 d->imgH = 0;
 d->total = 0;
 if(d->labels)
 {
  n = d->labels->lh_Head;
  if(n->ln_Succ)
  {
   IListBrowser->GetListBrowserNodeAttrs(n, LBNA_NumColumns, &numcols, TAG_DONE);
   for(c = 0; c < numcols && c < FB_MAXCOLS; c++)
   {
    STRPTR text = NULL;
    IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Column, c, LBNCA_Text, &text, TAG_DONE);
    if(text != NULL && !(d->hiddenColumns & (1UL << c)))
    {
     d->textCols[d->numTextCols++] = c;
    }
   }
  }
  if(d->iconSize > 0)
  {
   /* fixed image box: the cells keep their size while icons load or change */
   d->imgH = d->iconSize;
   if(d->iconSize + 2 * FB_PAD > (uint32)d->cellW) d->cellW = d->iconSize + 2 * FB_PAD;
   d->total = CountNodes(d->labels);
  }
  else
  {
   /* image box from the largest image or icon shown */
   for(n = d->labels->lh_Head; n->ln_Succ; n = n->ln_Succ)
   {
    int32 w, h;
    NodeVisualSize(d, o, rp, n, &w, &h);
    if(h > d->imgH) d->imgH = h;
    if(w + 2 * FB_PAD > d->cellW) d->cellW = w + 2 * FB_PAD; /* wide icons widen all cells */
    d->total++;
   }
  }
 }
 if(d->imgH > FB_MAXIMAGE) d->imgH = FB_MAXIMAGE;

 d->cellH = 2 * FB_PAD + d->imgH + (d->imgH ? FB_PAD : 0) + d->numTextCols * d->lineH;

 d->cols = d->area.Width / d->cellW;
 if(d->cols < 1) d->cols = 1;
 d->slotW = d->area.Width / d->cols;
 d->rows = (d->total + d->cols - 1) / d->cols;
 d->contentH = d->rows * d->cellH;

 areaH = d->area.Height;
 maxTop = d->contentH - areaH;
 if(maxTop < 0) maxTop = 0;
 if(d->topY > maxTop) d->topY = maxTop;
 if(d->topY < 0) d->topY = 0;
}

/*
** Highest valid scroll position (in pixels) of the icon view.
*/
static int32 MaxTop(struct FBData *d)
{
 int32 m = d->contentH - d->area.Height;
 return m < 0 ? 0 : m;
}

/*
** Box of cell index in content coordinates (not scrolled).
*/
static void CellBox(struct FBData *d, int32 index, struct IBox *b)
{
 int32 col = index % d->cols, row = index / d->cols;
 b->Left = d->area.Left + col * d->slotW + (d->slotW - d->cellW) / 2;
 b->Top = d->area.Top + row * d->cellH - d->topY;
 b->Width = d->cellW;
 b->Height = d->cellH;
}

/*
** Cell index at gadget relative position x/y, or -1 for empty space.
*/
static int32 IndexAt(struct FBData *d, int32 x, int32 y)
{
 int32 col, row, index;
 struct IBox b;
 if(x < d->area.Left || x >= d->area.Left + d->area.Width ||
    y < d->area.Top || y >= d->area.Top + d->area.Height) return -1;
 col = (x - d->area.Left) / d->slotW;
 row = (y - d->area.Top + d->topY) / d->cellH;
 if(col >= d->cols) return -1;
 index = row * d->cols + col;
 if(index < 0 || index >= d->total) return -1;
 CellBox(d, index, &b);
 if(x < b.Left || x >= b.Left + b.Width) return -1;
 return index;
}

/*
** Scroll the icon view so that cell index is fully visible.
*/
static void MakeVisible(struct FBData *d, int32 index)
{
 int32 y;
 if(index < 0 || d->cols < 1) return;
 y = (index / d->cols) * d->cellH;
 if(y < d->topY) d->topY = y;
 else if(y + d->cellH > d->topY + d->area.Height) d->topY = y + d->cellH - d->area.Height;
 if(d->topY > MaxTop(d)) d->topY = MaxTop(d);
 if(d->topY < 0) d->topY = 0;
}

/****************************************************************************/
/* rendering                                                                */
/****************************************************************************/

/*
** Draw text at left/top, cropped to width pixels as "Long str...".
*/
static void DrawText(struct RastPort *rp, CONST_STRPTR text, int32 left, int32 top, int32 width)
{
 struct TextExtent te;
 int32 len = IUtility->Strlen(text), fit, w;
 char buf[256];

 if(len == 0) return;
 if(len > 250) len = 250;
 w = IGraphics->TextLength(rp, text, len);
 if(w > width)
 {
  int32 dots = IGraphics->TextLength(rp, "...", 3);
  fit = IGraphics->TextFit(rp, text, len, &te, NULL, 1, width > dots ? width - dots : 0, 32767);
  IExec->CopyMem(text, buf, fit);
  IExec->CopyMem("...", buf + fit, 3);
  len = fit + 3;
  text = buf;
  w = IGraphics->TextLength(rp, text, len);
 }
 IGraphics->Move(rp, left + (width - w) / 2, top + rp->Font->tf_Baseline);
 IGraphics->Text(rp, text, len);
}

/*
** Draw a raised or recessed bevel around box b.
*/
static void Bevel(struct RastPort *rp, struct DrawInfo *dri, struct IBox *b, BOOL recessed)
{
 uint16 tl = dri->dri_Pens[recessed ? SHADOWPEN : SHINEPEN];
 uint16 br = dri->dri_Pens[recessed ? SHINEPEN : SHADOWPEN];
 int32 x0 = b->Left, y0 = b->Top, x1 = b->Left + b->Width - 1, y1 = b->Top + b->Height - 1;
 IGraphics->SetAPen(rp, tl);
 IGraphics->Move(rp, x0, y1); IGraphics->Draw(rp, x0, y0); IGraphics->Draw(rp, x1, y0);
 IGraphics->SetAPen(rp, br);
 IGraphics->Move(rp, x1, y0 + 1); IGraphics->Draw(rp, x1, y1); IGraphics->Draw(rp, x0 + 1, y1);
}

/*
** Draw the icon mode scroll bar (frame and knob).
*/
static void RenderScrollBar(struct FBData *d, struct RastPort *rp, struct DrawInfo *dri)
{
 struct IBox knob;
 int32 maxTop = MaxTop(d);

 IGraphics->SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
 IGraphics->RectFill(rp, d->sb.Left, d->sb.Top, d->sb.Left + d->sb.Width - 1, d->sb.Top + d->sb.Height - 1);
 Bevel(rp, dri, &d->sb, TRUE);

 knob.Left = d->sb.Left + 1;
 knob.Width = d->sb.Width - 2;
 if(d->contentH <= d->area.Height || maxTop == 0)
 {
  knob.Top = d->sb.Top + 1;
  knob.Height = d->sb.Height - 2;
 }
 else
 {
  knob.Height = (d->sb.Height - 2) * d->area.Height / d->contentH;
  if(knob.Height < 8) knob.Height = 8;
  knob.Top = d->sb.Top + 1 + (int32)((int64)d->topY * (d->sb.Height - 2 - knob.Height) / maxTop);
 }
 IGraphics->SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
 IGraphics->RectFill(rp, knob.Left, knob.Top, knob.Left + knob.Width - 1, knob.Top + knob.Height - 1);
 Bevel(rp, dri, &knob, FALSE);
}

/*
** Box of the scroll bar knob for the current scroll position.
*/
static void KnobBox(struct FBData *d, struct IBox *knob)
{
 int32 maxTop = MaxTop(d);
 knob->Left = d->sb.Left + 1;
 knob->Width = d->sb.Width - 2;
 if(d->contentH <= d->area.Height || maxTop == 0)
 {
  knob->Top = d->sb.Top + 1;
  knob->Height = d->sb.Height - 2;
 }
 else
 {
  knob->Height = (d->sb.Height - 2) * d->area.Height / d->contentH;
  if(knob->Height < 8) knob->Height = 8;
  knob->Top = d->sb.Top + 1 + (int32)((int64)d->topY * (d->sb.Height - 2 - knob->Height) / maxTop);
 }
}

/*
** Fill (x0,y0)-(x1,y1) with the background and draw the visible cells,
** shifted by (-ox,-oy) from window coordinates.
**
** Used for both the offscreen buffer and the direct (unbuffered) path.
*/
static void DrawCells(struct FBData *d, Object *o, struct DrawInfo *dri, struct RastPort *rp, uint16 bg,
                      int32 ox, int32 oy, int32 x0, int32 y0, int32 x1, int32 y1)
{
 struct Node *n;
 int32 index, first, last;

 IGraphics->SetDrMd(rp, JAM1);
 IGraphics->SetAPen(rp, bg);
 IGraphics->RectFill(rp, x0, y0, x1, y1);

 first = (d->topY / d->cellH) * d->cols;
 last = ((d->topY + d->area.Height) / d->cellH + 1) * d->cols - 1;

 for(index = 0, n = d->labels ? d->labels->lh_Head : NULL; n && n->ln_Succ && index <= last; n = n->ln_Succ, index++)
 {
  struct IBox b;
  struct Image *image;
  BOOL sel;
  uint32 flags = 0;
  int32 y, i;

  if(index < first) continue;

  CellBox(d, index, &b);
  b.Left -= ox;
  b.Top -= oy;
  sel = IsSelected(n);
  IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Flags, &flags, TAG_DONE);

  if(sel)
  {
   IGraphics->SetAPen(rp, dri->dri_Pens[FILLPEN]);
   IGraphics->RectFill(rp, b.Left, b.Top, b.Left + b.Width - 1, b.Top + b.Height - 1);
  }

  y = b.Top + FB_PAD;
  {
   struct DiskObject *dobj = NodeIcon(d, o, n);
   int32 w, h;
   BOOL scaled = NodeVisualSize(d, o, rp, n, &w, &h);
   if(dobj && w > 0)
   {
    /* with its own alpha; bottom aligned like Workbench. icon.library
       caches the scaled imagery in the icon. */
    IIcon->DrawIconState(rp, dobj, NULL, b.Left + (b.Width - w) / 2, y + d->imgH - h,
                         sel ? IDS_SELECTED : IDS_NORMAL,
                         ICONDRAWA_Borderless, TRUE, ICONDRAWA_EraseBackground, FALSE,
                         scaled ? ICONDRAWA_Width : TAG_IGNORE, w,
                         scaled ? ICONDRAWA_Height : TAG_IGNORE, h,
                         TAG_DONE);
   }
   else if((image = NodeImage(n)))
   {
    /* bottom aligned like the icons, so nothing moves when an icon replaces it */
    IIntuition->DrawImageState(rp, image, b.Left + (b.Width - image->Width) / 2,
                               y + d->imgH - image->Height, sel ? IDS_SELECTED : IDS_NORMAL, dri);
   }
  }
  if(d->imgH) y += d->imgH + FB_PAD;

  for(i = 0; i < d->numTextCols; i++)
  {
   STRPTR text = NULL;
   uint32 fg = 0;
   IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Column, d->textCols[i], LBNCA_Text, &text, LBNCA_FGPen, &fg, TAG_DONE);
   if(text && text[0])
   {
    if(sel) IGraphics->SetAPen(rp, dri->dri_Pens[FILLTEXTPEN]);
    else IGraphics->SetAPen(rp, (flags & LBFLG_CUSTOMPENS) ? fg : dri->dri_Pens[TEXTPEN]);
    DrawText(rp, text, b.Left + FB_PAD, y, b.Width - 2 * FB_PAD);
   }
   y += d->lineH;
  }
 }
}

/*
** Make sure the offscreen buffer is at least w x h pixels, a friend
** of the rastport's bitmap.
**
** Returns FALSE if it could not be allocated (render unbuffered then).
*/
static BOOL EnsureBuffer(struct FBData *d, struct RastPort *rp, int32 w, int32 h)
{
 if(d->buf && d->bufW >= w && d->bufH >= h) return TRUE;
 if(d->buf)
 {
  IGraphics->FreeBitMap(d->buf);
  d->buf = NULL;
 }
 /* round up so resizing a window doesn't reallocate on every step */
 d->bufW = (w + 63) & ~63;
 d->bufH = (h + 63) & ~63;
 d->buf = IGraphics->AllocBitMap(d->bufW, d->bufH, IGraphics->GetBitMapAttr(rp->BitMap, BMA_DEPTH),
                                 BMF_MINPLANES, rp->BitMap);
 return d->buf != NULL;
}

/*
** Render the icon view: cells into the offscreen buffer, then blitted
** to the window in one go so scrolling does not flicker. Falls back to
** drawing directly into a clip region without a buffer.
*/
static void RenderIcons(struct FBData *d, Object *o, struct GadgetInfo *gi, struct RastPort *rp)
{
 struct DrawInfo *dri = gi ? gi->gi_DrInfo : NULL;
 uint16 bg;
 int32 bufH;

 if(dri == NULL) return;

 Layout(d, o, rp);

 /* background pen: given, or the first node's custom background */
 bg = dri->dri_Pens[BACKGROUNDPEN];
 if(d->bgPen >= 0)
 {
  bg = d->bgPen;
 }
 else if(d->labels && d->labels->lh_Head->ln_Succ)
 {
  uint32 flags = 0, pen = 0;
  IListBrowser->GetListBrowserNodeAttrs(d->labels->lh_Head, LBNA_Flags, &flags, LBNA_Column, 1, LBNCA_BGPen, &pen, TAG_DONE);
  if(flags & LBFLG_CUSTOMPENS) bg = pen;
 }

 /* outer bevel like a listbrowser frame */
 Bevel(rp, dri, &d->box, TRUE);

 /* Double buffered: draw the cells offscreen and copy them in one blit, so
    scrolling doesn't flicker. The buffer has one cell height of room above
    and below the visible area for partly visible rows. */
 bufH = d->area.Height + 2 * d->cellH;
 if(EnsureBuffer(d, rp, d->area.Width, bufH))
 {
  struct RastPort brp;
  IGraphics->InitRastPort(&brp);
  brp.BitMap = d->buf;
  IGraphics->SetFont(&brp, rp->Font);
  DrawCells(d, o, dri, &brp, bg, d->area.Left, d->area.Top - d->cellH, 0, 0, d->area.Width - 1, bufH - 1);
  IGraphics->BltBitMapRastPort(d->buf, 0, d->cellH, rp, d->area.Left, d->area.Top,
                               d->area.Width, d->area.Height, 0xC0);
 }
 else
 {
  /* no memory for a buffer: draw directly, clipped to the cell area */
  struct Region *region = IGraphics->NewRegion(), *oldregion = NULL;
  struct Layer *layer = rp->Layer;
  if(region)
  {
   struct Rectangle r;
   r.MinX = d->area.Left;
   r.MinY = d->area.Top;
   r.MaxX = d->area.Left + d->area.Width - 1;
   r.MaxY = d->area.Top + d->area.Height - 1;
   IGraphics->OrRectRegion(region, &r);
   if(layer) oldregion = ILayers->InstallClipRegion(layer, region);
  }
  DrawCells(d, o, dri, rp, bg, 0, 0, d->area.Left, d->area.Top,
            d->area.Left + d->area.Width - 1, d->area.Top + d->area.Height - 1);
  if(layer && region) ILayers->InstallClipRegion(layer, oldregion);
  if(region) IGraphics->DisposeRegion(region);
 }

 RenderScrollBar(d, rp, dri);
}

/*
** Redraw the icon view through the GadgetInfo (from input handling),
** including the lasso rectangle while one is active.
*/
static void Redraw(struct FBData *d, Object *o, struct GadgetInfo *gi)
{
 struct RastPort *rp;
 if(gi == NULL) return;
 if((rp = IIntuition->ObtainGIRPort(gi)))
 {
  RenderIcons(d, o, gi, rp);
  if(d->lasso && gi->gi_DrInfo)
  {
   int32 x0 = d->lx0 < d->lx1 ? d->lx0 : d->lx1, x1 = d->lx0 < d->lx1 ? d->lx1 : d->lx0;
   int32 y0 = (d->ly0 < d->ly1 ? d->ly0 : d->ly1) - d->topY + d->area.Top;
   int32 y1 = (d->ly0 < d->ly1 ? d->ly1 : d->ly0) - d->topY + d->area.Top;
   int32 top = d->area.Top, bottom = d->area.Top + d->area.Height - 1;
   if(y0 < top) y0 = top;
   if(y1 > bottom) y1 = bottom;
   IGraphics->SetAPen(rp, gi->gi_DrInfo->dri_Pens[TEXTPEN]);
   IGraphics->Move(rp, x0, y0); IGraphics->Draw(rp, x1, y0); IGraphics->Draw(rp, x1, y1);
   IGraphics->Draw(rp, x0, y1); IGraphics->Draw(rp, x0, y0);
  }
  IIntuition->ReleaseGIRPort(rp);
 }
}

/****************************************************************************/
/* attributes                                                               */
/****************************************************************************/

/*
** Handle the attributes shared by both modes.
**
** modeChanged is set to TRUE if FILEBROWSER_ViewMode changed.
** Returns TRUE if the icon view must be redrawn.
*/
static BOOL SetAttrs(struct FBData *d, struct TagItem *tags, BOOL *modeChanged)
{
 struct TagItem *tstate = tags, *tag;
 BOOL redraw = FALSE;
 BOOL iconMode = d->mode == FBVM_ICONS;

 while((tag = IUtility->NextTagItem(&tstate)))
 {
  switch(tag->ti_Tag)
  {
   case FILEBROWSER_ViewMode:
    if(tag->ti_Data != d->mode)
    {
     d->mode = tag->ti_Data == FBVM_ICONS ? FBVM_ICONS : FBVM_DETAILS;
     if(modeChanged) *modeChanged = TRUE;
     redraw = TRUE;
    }
    break;
   case FILEBROWSER_CellChars:
    d->cellChars = tag->ti_Data < 4 ? 4 : tag->ti_Data;
    redraw = TRUE;
    break;
   case FILEBROWSER_HiddenColumns:
    d->hiddenColumns = tag->ti_Data;
    redraw = TRUE;
    break;
   case FILEBROWSER_BackgroundPen:
    d->bgPen = (int32)tag->ti_Data;
    redraw = TRUE;
    break;
   case FILEBROWSER_IconHook:
    d->iconHook = (struct Hook *)tag->ti_Data;
    redraw = TRUE;
    break;
   case FILEBROWSER_IconSize:
    d->iconSize = tag->ti_Data;
    redraw = TRUE;
    break;
   case LISTBROWSER_Labels:
    d->labels = (tag->ti_Data == (uint32)~0) ? NULL : (struct List *)tag->ti_Data;
    d->selIndex = -1;
    d->selNode = NULL;
    d->lastClickNode = NULL;
    redraw = TRUE;
    break;
   case LISTBROWSER_MultiSelect:
    d->multiSelect = tag->ti_Data ? TRUE : FALSE;
    break;
   case LISTBROWSER_PersistSelect:
    d->persistSelect = tag->ti_Data ? TRUE : FALSE;
    break;
   case LISTBROWSER_Selected:
    if(iconMode)
    {
     int32 idx = (int32)tag->ti_Data;
     struct Node *n = NodeAt(d->labels, idx);
     if(!d->multiSelect || idx < 0) SelectNone(d);
     if(n) SetSelected(n, TRUE);
     d->selIndex = n ? idx : -1;
     d->selNode = n;
     redraw = TRUE;
    }
    break;
   case LISTBROWSER_MakeVisible:
    if(iconMode)
    {
     MakeVisible(d, (int32)tag->ti_Data);
     redraw = TRUE;
    }
    break;
   case LISTBROWSER_Top:
    if(iconMode && d->cols > 0)
    {
     d->topY = ((int32)tag->ti_Data / d->cols) * d->cellH;
     redraw = TRUE;
    }
    break;
   case LISTBROWSER_Position:
    if(iconMode)
    {
     switch(tag->ti_Data)
     {
      case LBP_TOP:      d->topY = 0; break;
      case LBP_BOTTOM:   d->topY = 0x7fffffff / 2; break;
      case LBP_LINEUP:   d->topY -= d->cellH; break;
      case LBP_LINEDOWN: d->topY += d->cellH; break;
      case LBP_PAGEUP:   d->topY -= d->area.Height; break;
      case LBP_PAGEDOWN: d->topY += d->area.Height; break;
      default: break;
     }
     redraw = TRUE; /* Layout() clamps topY */
    }
    break;
   default:
    break;
  }
 }
 return redraw;
}

/****************************************************************************/
/* input (icon mode)                                                        */
/****************************************************************************/

/*
** Select the cells inside the lasso rectangle. Cells outside it are
** deselected unless the lasso was started with Shift (adds to the
** selection).
*/
static void LassoSelect(struct FBData *d)
{
 struct Node *n;
 int32 i = 0;
 int32 x0 = d->lx0 < d->lx1 ? d->lx0 : d->lx1, x1 = d->lx0 < d->lx1 ? d->lx1 : d->lx0;
 int32 y0 = d->ly0 < d->ly1 ? d->ly0 : d->ly1, y1 = d->ly0 < d->ly1 ? d->ly1 : d->ly0;
 if(d->labels == NULL) return;
 for(n = d->labels->lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
 {
  struct IBox b;
  int32 cy;
  BOOL inside;
  CellBox(d, i, &b);
  cy = b.Top - d->area.Top + d->topY; /* content coordinates */
  inside = !(b.Left + b.Width <= x0 || b.Left > x1 || cy + b.Height <= y0 || cy > y1);
  if(inside && !IsSelected(n)) SetSelected(n, TRUE);
  else if(!inside && !d->lassoAdd && IsSelected(n)) SetSelected(n, FALSE);
 }
}

/*
** GM_GOACTIVE in icon mode: click on a cell (select, double-click,
** Shift toggles), the scroll bar, or empty space (starts a lasso).
**
** Returns the GMR_* result for Intuition.
*/
static uint32 GoActive(Class *cl, Object *o, struct gpInput *gpi)
{
 struct FBData *d = INST_DATA(cl, o);
 struct InputEvent *ie = gpi->gpi_IEvent;
 int32 x, y, idx;
 struct IBox knob;

 if(ie == NULL) return GMR_NOREUSE;

 /* like the listbrowser: GFLG_SELECTED while active (an application's drag & drop
    handler clears it when a drag starts) */
 ((struct Gadget *)o)->Flags |= GFLG_SELECTED;

 x = d->box.Left + gpi->gpi_Mouse.X;
 y = d->box.Top + gpi->gpi_Mouse.Y;

 d->sbDrag = FALSE;
 d->moved = FALSE;
 d->collapseOnRelease = FALSE;

 /* scroll bar */
 if(x >= d->sb.Left && x < d->sb.Left + d->sb.Width && y >= d->sb.Top && y < d->sb.Top + d->sb.Height)
 {
  KnobBox(d, &knob);
  if(y < knob.Top) d->topY -= d->area.Height;
  else if(y >= knob.Top + knob.Height) d->topY += d->area.Height;
  else
  {
   d->sbDrag = TRUE;
   d->sbGrab = y - knob.Top;
  }
  Redraw(d, o, gpi->gpi_GInfo);
  return GMR_MEACTIVE;
 }

 idx = IndexAt(d, x, y);
 if(idx < 0)
 {
  /* click on empty space: deselect, like the listbrowser; dragging from
     here draws a selection rectangle (Workbench-like) */
  BOOL shift = (ie->ie_Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? TRUE : FALSE;
  if(!d->persistSelect && !shift)
  {
   SelectNone(d);
  }
  if(d->multiSelect && x >= d->area.Left && x < d->area.Left + d->area.Width)
  {
   d->lasso = TRUE;
   d->lassoAdd = shift || d->persistSelect;
   d->lx0 = d->lx1 = x;
   d->ly0 = d->ly1 = y - d->area.Top + d->topY;
  }
  d->selIndex = -1;
  d->selNode = NULL;
  d->relEvent = LBRE_NORMAL;
  d->lastClickNode = NULL;
  Redraw(d, o, gpi->gpi_GInfo);
  return GMR_MEACTIVE;
 }
 else
 {
  struct Node *node = NodeAt(d->labels, idx);
  BOOL shift = (ie->ie_Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? TRUE : FALSE;
  BOOL dbl = (node == d->lastClickNode) &&
             IIntuition->DoubleClick(d->lastSecs, d->lastMics, ie->ie_TimeStamp.Seconds, ie->ie_TimeStamp.Microseconds);

  if(dbl)
  {
   SetSelected(node, TRUE);
   d->relEvent = LBRE_DOUBLECLICK;
   d->lastClickNode = NULL;
  }
  else
  {
   if(!d->multiSelect)
   {
    SelectNone(d);
    SetSelected(node, TRUE);
   }
   else if(d->persistSelect || shift)
   {
    SetSelected(node, !IsSelected(node));
   }
   else if(IsSelected(node) && CountSelected(d) > 1)
   {
    /* Workbench-like: keep a multi selection so it can be dragged;
       a plain click narrows it to this node on release */
    d->collapseOnRelease = TRUE;
   }
   else
   {
    SelectNone(d);
    SetSelected(node, TRUE);
   }
   d->relEvent = LBRE_NORMAL;
   d->lastClickNode = node;
   d->lastSecs = ie->ie_TimeStamp.Seconds;
   d->lastMics = ie->ie_TimeStamp.Microseconds;
  }

  d->selIndex = idx;
  d->selNode = node;
  /* dragging from a cell is the application's drag & drop, not a selection */
  Redraw(d, o, gpi->gpi_GInfo);
  return GMR_MEACTIVE;
 }
}

/*
** GM_HANDLEINPUT in icon mode: mouse moves while active (scroll bar
** drag, lasso, drag detection) and the button release.
**
** Returns the GMR_* result for Intuition.
*/
static uint32 HandleInput(Class *cl, Object *o, struct gpInput *gpi)
{
 struct FBData *d = INST_DATA(cl, o);
 struct InputEvent *ie = gpi->gpi_IEvent;
 int32 x = d->box.Left + gpi->gpi_Mouse.X;
 int32 y = d->box.Top + gpi->gpi_Mouse.Y;

 if(ie == NULL) return GMR_MEACTIVE;

 if(ie->ie_Class == IECLASS_RAWMOUSE)
 {
  switch(ie->ie_Code)
  {
   case IECODE_LBUTTON | IECODE_UP_PREFIX:
    if(d->sbDrag)
    {
     d->sbDrag = FALSE;
     return GMR_NOREUSE;
    }
    if(d->lasso)
    {
     d->lasso = FALSE;
     Redraw(d, o, gpi->gpi_GInfo); /* remove the rectangle */
    }
    if(d->collapseOnRelease && !d->moved && d->selNode)
    {
     SelectNone(d);
     SetSelected(d->selNode, TRUE);
     Redraw(d, o, gpi->gpi_GInfo);
    }
    *gpi->gpi_Termination = d->selIndex;
    return GMR_NOREUSE | GMR_VERIFY;

   case IECODE_RBUTTON:
    if(d->lasso)
    {
     d->lasso = FALSE;
     Redraw(d, o, gpi->gpi_GInfo);
    }
    d->sbDrag = FALSE;
    return GMR_REUSE;

   case IECODE_NOBUTTON:
    if(d->sbDrag)
    {
     int32 track = d->sb.Height - 2;
     struct IBox knob;
     KnobBox(d, &knob);
     if(track > knob.Height && MaxTop(d) > 0)
     {
      int32 pos = y - d->sb.Top - 1 - d->sbGrab;
      d->topY = (int32)((int64)pos * MaxTop(d) / (track - knob.Height));
      Redraw(d, o, gpi->gpi_GInfo);
     }
    }
    else
    {
     /* An application's drag & drop handler clears GFLG_SELECTED when a drag starts:
        then the selection must stay as it is (no narrowing on release) */
     BOOL active = (((struct Gadget *)o)->Flags & GFLG_SELECTED) != 0;
     if(!active || x < d->area.Left || x >= d->area.Left + d->area.Width ||
        y < d->area.Top || y >= d->area.Top + d->area.Height)
     {
      d->moved = TRUE;
     }
     if(d->lasso && active)
     {
      int32 ny;
      /* scroll when dragging past the top or bottom */
      if(y < d->area.Top) d->topY -= d->lineH;
      else if(y >= d->area.Top + d->area.Height) d->topY += d->lineH;
      if(d->topY > MaxTop(d)) d->topY = MaxTop(d);
      if(d->topY < 0) d->topY = 0;
      if(x < d->area.Left) x = d->area.Left;
      if(x >= d->area.Left + d->area.Width) x = d->area.Left + d->area.Width - 1;
      ny = y - d->area.Top + d->topY;
      if(ny < 0) ny = 0;
      if(ny > d->contentH) ny = d->contentH;
      d->lx1 = x;
      d->ly1 = ny;
      LassoSelect(d);
      Redraw(d, o, gpi->gpi_GInfo);
     }
    }
    break;

   default:
    break;
  }
 }
 return GMR_MEACTIVE;
}

/*
** GM_HANDLESCROLL in icon mode: mouse wheel scrolling.
*/
static uint32 HandleScroll(Class *cl, Object *o, struct gpInput *gpi)
{
 struct FBData *d = INST_DATA(cl, o);
 struct InputEvent *ie = gpi->gpi_IEvent;
 if(ie && ie->ie_Class == IECLASS_MOUSEWHEEL && ie->ie_Y != 0)
 {
  d->topY += ie->ie_Y * (d->cellH / 2 > 8 ? d->cellH / 2 : 8);
  Redraw(d, o, gpi->gpi_GInfo);
 }
 return GMR_NOREUSE;
}

/****************************************************************************/
/* dispatcher                                                               */
/****************************************************************************/

/*
** Class dispatcher. In details mode everything goes to the superclass;
** in icon mode rendering, hit testing, input and the selection related
** attributes are handled here.
*/
static uint32 Dispatch(Class *cl, Object *o, Msg msg)
{
 struct FBData *d;
 uint32 result;

 switch(msg->MethodID)
 {
  case OM_NEW:
   result = IIntuition->IDoSuperMethodA(cl, o, msg);
   if(result)
   {
    d = INST_DATA(cl, (Object *)result);
    IUtility->ClearMem(d, sizeof(*d));
    d->mode = FBVM_DETAILS;
    d->cellChars = 14;
    d->bgPen = -1;
    d->selIndex = -1;
    SetAttrs(d, ((struct opSet *)msg)->ops_AttrList, NULL);
   }
   return result;

  case OM_SET:
  case OM_UPDATE:
  {
   struct opSet *ops = (struct opSet *)msg;
   BOOL modeChanged = FALSE, redraw;
   d = INST_DATA(cl, o);

   if(d->mode == FBVM_ICONS)
   {
    /* keep the listbrowser's state current, but don't let it draw */
    struct opSet copy = *ops;
    copy.ops_GInfo = NULL;
    result = IIntuition->IDoSuperMethodA(cl, o, (Msg)&copy);
   }
   else
   {
    result = IIntuition->IDoSuperMethodA(cl, o, msg);
   }

   redraw = SetAttrs(d, ops->ops_AttrList, &modeChanged);

   if(ops->ops_GInfo)
   {
    if(d->mode == FBVM_DETAILS && !modeChanged && result != 0)
    {
     /* BOOPSI rule: a class only renders after OM_SET when it is the
        object's true class (OCLASS(o) == cl), so for a subclass the
        listbrowser doesn't redraw itself here. Do it for it. */
     struct RastPort *rp;
     if((rp = IIntuition->ObtainGIRPort(ops->ops_GInfo)))
     {
      IIntuition->IDoSuperMethod(cl, o, GM_RENDER, ops->ops_GInfo, rp, GREDRAW_UPDATE);
      IIntuition->ReleaseGIRPort(rp);
     }
    }
    else if(d->mode == FBVM_ICONS && (redraw || modeChanged))
    {
     Redraw(d, o, ops->ops_GInfo);
    }
    else if(modeChanged) /* back to details: let the listbrowser redraw */
    {
     struct RastPort *rp;
     struct List *labels = d->labels;
     /* reattach so the listbrowser recounts the selection made in icon mode */
     IIntuition->IDoSuperMethod(cl, o, OM_SET, NULL, LISTBROWSER_Labels, ~0, TAG_DONE);
     {
      struct TagItem tags[] = { { LISTBROWSER_Labels, (uint32)labels }, { TAG_DONE, 0 } };
      struct opSet set = { OM_SET, tags, NULL };
      IIntuition->IDoSuperMethodA(cl, o, (Msg)&set);
     }
     d->labels = labels;
     if((rp = IIntuition->ObtainGIRPort(ops->ops_GInfo)))
     {
      IIntuition->IDoSuperMethod(cl, o, GM_RENDER, ops->ops_GInfo, rp, GREDRAW_REDRAW);
      IIntuition->ReleaseGIRPort(rp);
     }
    }
   }
   return result;
  }

  case OM_GET:
  {
   struct opGet *opg = (struct opGet *)msg;
   d = INST_DATA(cl, o);
   switch(opg->opg_AttrID)
   {
    case FILEBROWSER_ViewMode:      *opg->opg_Storage = d->mode; return TRUE;
    case FILEBROWSER_CellChars:     *opg->opg_Storage = d->cellChars; return TRUE;
    case FILEBROWSER_HiddenColumns: *opg->opg_Storage = d->hiddenColumns; return TRUE;
    case FILEBROWSER_BackgroundPen: *opg->opg_Storage = (uint32)d->bgPen; return TRUE;
    case FILEBROWSER_IconHook:      *opg->opg_Storage = (uint32)d->iconHook; return TRUE;
    case FILEBROWSER_IconSize:      *opg->opg_Storage = d->iconSize; return TRUE;
   }
   if(d->mode == FBVM_ICONS)
   {
    switch(opg->opg_AttrID)
    {
     case LISTBROWSER_RelEvent:     *opg->opg_Storage = d->relEvent; return TRUE;
     case LISTBROWSER_Selected:     *opg->opg_Storage = (uint32)d->selIndex; return TRUE;
     case LISTBROWSER_SelectedNode: *opg->opg_Storage = (uint32)d->selNode; return TRUE;
     case LISTBROWSER_NumSelected:  *opg->opg_Storage = CountSelected(d); return TRUE;
     case LISTBROWSER_TotalNodes:   *opg->opg_Storage = CountNodes(d->labels); return TRUE;
     case LISTBROWSER_Top:
      *opg->opg_Storage = d->cellH > 0 ? (d->topY / d->cellH) * d->cols : 0;
      return TRUE;
    }
   }
   return IIntuition->IDoSuperMethodA(cl, o, msg);
  }

  case OM_DISPOSE:
   d = INST_DATA(cl, o);
   if(d->buf)
   {
    IGraphics->FreeBitMap(d->buf);
    d->buf = NULL;
   }
   break;

  case GM_RENDER:
   d = INST_DATA(cl, o);
   if(d->mode == FBVM_ICONS)
   {
    struct gpRender *gpr = (struct gpRender *)msg;
    RenderIcons(d, o, gpr->gpr_GInfo, gpr->gpr_RPort);
    return TRUE;
   }
   break;

  case GM_HITTEST:
   d = INST_DATA(cl, o);
   if(d->mode == FBVM_ICONS) return GMR_GADGETHIT;
   break;

  case GM_GOACTIVE:
   d = INST_DATA(cl, o);
   if(d->mode == FBVM_ICONS) return GoActive(cl, o, (struct gpInput *)msg);
   break;

  case GM_HANDLEINPUT:
   d = INST_DATA(cl, o);
   if(d->mode == FBVM_ICONS) return HandleInput(cl, o, (struct gpInput *)msg);
   break;

  case GM_HANDLESCROLL:
   d = INST_DATA(cl, o);
   if(d->mode == FBVM_ICONS) return HandleScroll(cl, o, (struct gpInput *)msg);
   break;

  case GM_GOINACTIVE:
   d = INST_DATA(cl, o);
   if(d->mode == FBVM_ICONS)
   {
    d->sbDrag = FALSE;
    d->lasso = FALSE;
    ((struct Gadget *)o)->Flags &= ~GFLG_SELECTED;
    return 0;
   }
   break;

  default:
   break;
 }
 return IIntuition->IDoSuperMethodA(cl, o, msg);
}

/*
** Create the FileBrowser class, a private subclass of
** listbrowser.gadget.
**
** The interfaces listed in the file header must be open.
** Returns the class for NewObject(cl, NULL, ...) or NULL on failure.
** Free it with FreeFileBrowserClass() after all objects are disposed.
*/
struct IClass *MakeFileBrowserClass(void)
{
 struct IClass *cl = IIntuition->MakeClass(NULL, NULL, IListBrowser->LISTBROWSER_GetClass(), sizeof(struct FBData), 0);
 if(cl)
 {
  cl->cl_Dispatcher.h_Entry = (HOOKFUNC)Dispatch;
 }
 return cl;
}

/*
** Free a class from MakeFileBrowserClass(). NULL is allowed.
*/
void FreeFileBrowserClass(struct IClass *cl)
{
 if(cl)
 {
  IIntuition->FreeClass(cl);
 }
}
