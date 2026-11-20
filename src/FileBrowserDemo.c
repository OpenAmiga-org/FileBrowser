/*
** File:        FileBrowserDemo.c
** Description: Example and test program for the FileBrowser gadget class.
**              Lists the entries of a drawer (default SYS:) with name, size
**              and date columns; the button toggles between details mode
**              and icon mode. Usage: FileBrowserDemo [drawer]
**
** OpenAmiga.org
** This file is part of the OpenAmiga "FileBrowser" project.
**
** For more information look at:
** http://openamiga.org/?function=gitproject&name=FileBrowser
**
** Public domain: see LICENSE in the release archive.
**
** Change history:
**  2026-11-14 orgin, created
**  2026-11-15 orgin, 1.2 with FileBrowser 1.0
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/datetime.h>
#include <workbench/icon.h>
#include <intuition/intuition.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/listbrowser.h>

#include "FileBrowser.h"

static const char __attribute__((used)) verstag[] =
 "\0$VER: FileBrowserDemo 1.2 (15.11.2026)\0";

/* Interfaces used by the FileBrowser class and by this program */
struct Library *IntuitionBase, *GfxBase, *LayersBase, *UtilityBase, *IconBase;
struct IntuitionIFace *IIntuition;
struct GraphicsIFace *IGraphics;
struct LayersIFace *ILayers;
struct UtilityIFace *IUtility;
struct IconIFace *IIcon;

struct Library *WindowBase, *LayoutBase, *ButtonBase, *ListBrowserBase;
struct WindowIFace *IWindow;
struct LayoutIFace *ILayout;
struct ButtonIFace *IButton;
struct ListBrowserIFace *IListBrowser;

Class *WindowClass, *LayoutClass, *ButtonClass, *ListBrowserClass;

enum
{
 GID_LIST = 1,
 GID_MODE
};

#define MAX_ENTRIES 1000

/* Icons of the listed entries, index = position in the list. They are all
   loaded while listing: the icon hook must not do any DOS I/O. */
static struct DiskObject *icons[MAX_ENTRIES];

/*
** Open a library and get its main interface.
**
** *base receives the library base.
** Returns the interface or NULL.
*/
static struct Interface *OpenIFace(CONST_STRPTR name, uint32 version, struct Library **base)
{
 *base = IExec->OpenLibrary(name, version);
 if(*base == NULL) return NULL;
 return IExec->GetInterface(*base, "main", 1, NULL);
}

/*
** Open a BOOPSI class library and get its main interface.
**
** *cl receives the class pointer for NewObject() and *base the library.
** Returns the interface or NULL.
*/
static struct Interface *OpenClassIFace(CONST_STRPTR name, uint32 version, Class **cl,
                                        struct Library **base)
{
 *base = (struct Library *)IIntuition->OpenClass(name, version, cl);
 if(*base == NULL) return NULL;
 return IExec->GetInterface(*base, "main", 1, NULL);
}

/*
** Close everything OpenLibs() opened. Safe after a partial OpenLibs().
*/
static void CloseLibs(void)
{
 if(IIntuition != NULL)
 {
  IExec->DropInterface((struct Interface *)IListBrowser);
  IExec->DropInterface((struct Interface *)IButton);
  IExec->DropInterface((struct Interface *)ILayout);
  IExec->DropInterface((struct Interface *)IWindow);
  IIntuition->CloseClass((struct ClassLibrary *)ListBrowserBase);
  IIntuition->CloseClass((struct ClassLibrary *)ButtonBase);
  IIntuition->CloseClass((struct ClassLibrary *)LayoutBase);
  IIntuition->CloseClass((struct ClassLibrary *)WindowBase);
 }
 IExec->DropInterface((struct Interface *)IIcon);
 IExec->CloseLibrary(IconBase);
 IExec->DropInterface((struct Interface *)IUtility);
 IExec->CloseLibrary(UtilityBase);
 IExec->DropInterface((struct Interface *)ILayers);
 IExec->CloseLibrary(LayersBase);
 IExec->DropInterface((struct Interface *)IGraphics);
 IExec->CloseLibrary(GfxBase);
 IExec->DropInterface((struct Interface *)IIntuition);
 IExec->CloseLibrary(IntuitionBase);
}

/*
** Open every library, class and interface the program needs.
**
** Returns FALSE if something is missing (call CloseLibs() anyway).
*/
static BOOL OpenLibs(void)
{
 IIntuition = (struct IntuitionIFace *)OpenIFace("intuition.library", 53, &IntuitionBase);
 IGraphics = (struct GraphicsIFace *)OpenIFace("graphics.library", 53, &GfxBase);
 ILayers = (struct LayersIFace *)OpenIFace("layers.library", 53, &LayersBase);
 IUtility = (struct UtilityIFace *)OpenIFace("utility.library", 53, &UtilityBase);
 IIcon = (struct IconIFace *)OpenIFace("icon.library", 53, &IconBase);
 if(IIntuition == NULL || IGraphics == NULL || ILayers == NULL || IUtility == NULL || IIcon == NULL)
 {
  return FALSE;
 }

 IWindow = (struct WindowIFace *)OpenClassIFace("window.class", 53, &WindowClass, &WindowBase);
 ILayout = (struct LayoutIFace *)OpenClassIFace("gadgets/layout.gadget", 53, &LayoutClass, &LayoutBase);
 IButton = (struct ButtonIFace *)OpenClassIFace("gadgets/button.gadget", 53, &ButtonClass, &ButtonBase);
 IListBrowser = (struct ListBrowserIFace *)OpenClassIFace("gadgets/listbrowser.gadget", 53,
                                                          &ListBrowserClass, &ListBrowserBase);
 return IWindow != NULL && ILayout != NULL && IButton != NULL && IListBrowser != NULL;
}

/*
** FILEBROWSER_IconHook: the full size icon of a node (stored in the node's
** user data as an index into icons[]). No DOS I/O happens here.
*/
static uint32 IconHookFunc(struct Hook *hook, APTR gadget, APTR node)
{
 uint32 index = MAX_ENTRIES;

 IListBrowser->GetListBrowserNodeAttrs((struct Node *)node, LBNA_UserData, &index, TAG_DONE);
 if(index < MAX_ENTRIES) return (uint32)icons[index];
 return 0;
}

/*
** Fill list with one node per entry of drawer: name, size and date. The
** icon of each entry (its .info or a default icon) goes into icons[].
**
** Returns the number of entries added.
*/
static int32 ReadDrawer(CONST_STRPTR drawer, struct List *list)
{
 APTR context;
 struct ExamineData *dat;
 int32 count = 0;
 char path[1024], size[32], date[LEN_DATSTRING];

 context = IDOS->ObtainDirContextTags(EX_StringNameInput, drawer,
                                      EX_DataFields, EXF_NAME | EXF_SIZE | EXF_DATE | EXF_TYPE,
                                      TAG_DONE);
 if(context == NULL) return 0;

 while(count < MAX_ENTRIES && (dat = IDOS->ExamineDir(context)) != NULL)
 {
  struct Node *node;
  struct DateTime dt;
  int32 len = IUtility->Strlen(dat->Name);

  /* the .info files are shown as the icons of their entries */
  if(len > 5 && IUtility->Stricmp(dat->Name + len - 5, ".info") == 0) continue;

  if(EXD_IS_DIRECTORY(dat)) IUtility->Strlcpy(size, "Drawer", sizeof(size));
  else IUtility->SNPrintf(size, sizeof(size), "%llu", dat->FileSize);

  IUtility->ClearMem(&dt, sizeof(dt));
  dt.dat_Stamp = dat->Date;
  dt.dat_Format = FORMAT_DOS;
  dt.dat_StrDate = date;
  IDOS->DateToStr(&dt);

  IUtility->Strlcpy(path, drawer, sizeof(path));
  IDOS->AddPart(path, dat->Name, sizeof(path));
  icons[count] = IIcon->GetIconTags(path, ICONGETA_FailIfUnavailable, FALSE, TAG_DONE);

  node = IListBrowser->AllocListBrowserNode(3,
                                            LBNA_UserData, count,
                                            LBNA_Column, 0,
                                             LBNCA_CopyText, TRUE,
                                             LBNCA_Text, dat->Name,
                                            LBNA_Column, 1,
                                             LBNCA_CopyText, TRUE,
                                             LBNCA_Text, size,
                                             LBNCA_Justification, LCJ_RIGHT,
                                            LBNA_Column, 2,
                                             LBNCA_CopyText, TRUE,
                                             LBNCA_Text, date,
                                            TAG_DONE);
  if(node == NULL)
  {
   IIcon->FreeDiskObject(icons[count]);
   icons[count] = NULL;
   break;
  }
  IExec->AddTail(list, node);
  count++;
 }
 IDOS->ReleaseDirContext(context);
 return count;
}

/*
** Open the window and run until it is closed.
*/
static void Run(Class *fbClass, CONST_STRPTR drawer)
{
 struct List list;
 struct Hook iconHook;
 struct ColumnInfo *ci;
 Object *win, *list_gad, *mode_gad;
 struct Window *window;
 uint32 mode = FBVM_DETAILS;
 BOOL done = FALSE;

 IExec->NewList(&list);
 ReadDrawer(drawer, &list);

 IUtility->ClearMem(&iconHook, sizeof(iconHook));
 iconHook.h_Entry = IconHookFunc;

 ci = IListBrowser->AllocLBColumnInfo(3,
                                      LBCIA_Column, 0, LBCIA_Title, "Name", LBCIA_Weight, 50,
                                      LBCIA_Column, 1, LBCIA_Title, "Size", LBCIA_Weight, 20,
                                      LBCIA_Column, 2, LBCIA_Title, "Date", LBCIA_Weight, 30,
                                      TAG_DONE);

 win = IIntuition->NewObject(WindowClass, NULL,
  WA_Title, "FileBrowser demo",
  WA_ScreenTitle, drawer,
  WA_Width, 520, WA_Height, 360,
  WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE, WA_CloseGadget, TRUE,
  WA_Activate, TRUE,
  WINDOW_Position, WPOS_CENTERSCREEN,
  WINDOW_Layout, IIntuition->NewObject(LayoutClass, NULL,
   LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
   LAYOUT_AddChild, list_gad = IIntuition->NewObject(fbClass, NULL,
    GA_ID, GID_LIST,
    GA_RelVerify, TRUE,
    LISTBROWSER_Labels, &list,
    LISTBROWSER_ColumnInfo, ci,
    LISTBROWSER_ColumnTitles, TRUE,
    LISTBROWSER_MultiSelect, TRUE,
    LISTBROWSER_ShowSelected, TRUE,
    FILEBROWSER_ViewMode, mode,
    FILEBROWSER_IconHook, &iconHook,
    TAG_DONE),
   LAYOUT_AddChild, mode_gad = IIntuition->NewObject(ButtonClass, NULL,
    GA_ID, GID_MODE,
    GA_RelVerify, TRUE,
    GA_Text, "_Icon view",
    TAG_DONE),
   CHILD_WeightedHeight, 0,
   TAG_DONE),
  TAG_DONE);

 if(win != NULL && (window = (struct Window *)IIntuition->IDoMethod(win, WM_OPEN)) != NULL)
 {
  uint32 sigmask = 0;

  IIntuition->GetAttr(WINDOW_SigMask, win, &sigmask);
  while(!done)
  {
   uint32 result;
   uint16 code;

   IExec->Wait(sigmask | SIGBREAKF_CTRL_C);
   while((result = IIntuition->IDoMethod(win, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
   {
    switch(result & WMHI_CLASSMASK)
    {
     case WMHI_CLOSEWINDOW:
      done = TRUE;
      break;

     case WMHI_GADGETUP:
      if((result & WMHI_GADGETMASK) == GID_MODE)
      {
       mode = (mode == FBVM_DETAILS) ? FBVM_ICONS : FBVM_DETAILS;
       IIntuition->SetGadgetAttrs((struct Gadget *)list_gad, window, NULL,
                                  FILEBROWSER_ViewMode, mode, TAG_DONE);
       IIntuition->SetGadgetAttrs((struct Gadget *)mode_gad, window, NULL,
                                  GA_Text, mode == FBVM_ICONS ? "_Details view" : "_Icon view",
                                  TAG_DONE);
      }
      else if((result & WMHI_GADGETMASK) == GID_LIST)
      {
       uint32 event = 0;
       struct Node *node = NULL;
       STRPTR name = NULL;

       IIntuition->GetAttrs(list_gad, LISTBROWSER_RelEvent, &event,
                            LISTBROWSER_SelectedNode, &node, TAG_DONE);
       if(event == LBRE_DOUBLECLICK && node != NULL)
       {
        IListBrowser->GetListBrowserNodeAttrs(node, LBNA_Column, 0, LBNCA_Text, &name, TAG_DONE);
        IDOS->Printf("Double-click: %s\n", name);
       }
      }
      break;

     default:
      break;
    }
   }
   if(IExec->SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) done = TRUE;
  }
 }

 IIntuition->DisposeObject(win);
 IListBrowser->FreeListBrowserList(&list);
 IListBrowser->FreeLBColumnInfo(ci);
}

/*
** Entry point. Returns RETURN_OK, or RETURN_FAIL if libraries or the class
** are missing.
*/
int main(int argc, char **argv)
{
 int rc = RETURN_FAIL;
 CONST_STRPTR drawer = (argc > 1) ? argv[1] : "SYS:";

 if(OpenLibs())
 {
  Class *fbClass = MakeFileBrowserClass();

  if(fbClass != NULL)
  {
   int32 i;

   Run(fbClass, drawer);
   FreeFileBrowserClass(fbClass);
   for(i = 0; i < MAX_ENTRIES; i++)
   {
    if(icons[i] != NULL) IIcon->FreeDiskObject(icons[i]);
   }
   rc = RETURN_OK;
  }
 }
 CloseLibs();
 return rc;
}
