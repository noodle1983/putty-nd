/*
 * wintab.c - the implementation of the tabbar and its page.
 */

#ifndef NO_MULTIMON
#include <multimon.h>
#endif

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include "putty.h"
#include "terminal.h"
#include "storage.h"
#include "win_res.h"
#include "wintab.h"
#include "wintabdraw.h"

extern HINSTANCE hinst;
extern int fullscr_on_max;
extern Config cfg;

#define TID_POLLMOUSE 100
#define MOUSE_POLL_DELAY 100
#define BORDER_SIZE 3

extern void show_mouseptr(wintabitem *tabitem, int show);
extern int on_menu(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
extern int on_mouse_move(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
extern int on_nc_mouse_move(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
extern int process_clipdata(HGLOBAL clipdata, int unicode);
extern int on_key(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
extern int on_button(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);

const char* const WINTAB_PAGE_CLASS = "WintabPage";
int wintabpage_registed = 0;
const int MIN_BTN_WIDTH = 32;
const int MAX_BTN_WIDTH = 32;
const int CLS_BTN_WIDTH = 36;
const int SYS_BTN_HEIGHT = 15;

//-----------------------------------------------------------------------
// tabbar related
//-----------------------------------------------------------------------
/*
 * return 0 if succeed, -1 if failed.
 */
int wintab_init(wintab *wintab, HWND hwndParent)
{
    RECT rc; 
    int i;

    /* create tabar */
    INITCOMMONCONTROLSEX icce;
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_TAB_CLASSES;
	InitCommonControlsEx(&icce);
    
    GetClientRect(hwndParent, &rc);    
    wintab->hwndTab = CreateWindow(WC_TABCONTROL, "", 
        WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED | CS_DBLCLKS , 
        0, 0, rc.right, rc.bottom, 
        hwndParent, NULL, hinst, NULL); 
    if (wintab->hwndTab == NULL){
        ErrorExit("CreateWindow(WC_TABCONTROL...)"); 
    }

    win_bind_data(wintab->hwndTab, wintab);
    wintab->defWndProc = (WNDPROC)SetWindowLongPtr(wintab->hwndTab, GWL_WNDPROC, (long)WintabWndProc);
    
    wintab->hwndParent = hwndParent;
    wintab->end = 0;
    wintab->cur = 0;
    wintab->bg_col = RGB(67, 115, 203);
    wintab->sel_col = GetSysColor(COLOR_BTNFACE);//RGB(240, 240, 240);
    wintab->nosel_col = RGB(161, 199, 244);
    wintab->on_col = RGB(204, 224, 248);
    wintab->hl_col = RGB(193, 53, 53);
    wintab->bd_col = RGB(54, 83, 129);
    for (i = 0; i < sizeof(wintab->hSysRgn)/sizeof (HRGN); i++)
        wintab->hSysRgn[i] = NULL;
    wintab_create_toolbar(wintab);
    wintab_create_sysbtn(wintab);
 
    if (wintabitem_creat(wintab, &cfg) != 0){
        ErrorExit("wintabitem_creat(...)"); 
    }
    
    //init the extra size
    wintab_resize(wintab, &rc);
    
    //resize according to term
    int index = wintab->cur;
    int term_width = wintab->items[index]->font_width * wintab->items[index]->term->cols;
    int term_height = wintab->items[index]->font_height * wintab->items[index]->term->rows;
    wintabitem_require_resize(wintab->items[index], term_width, term_height);
    return 0; 
}

//-----------------------------------------------------------------------

int wintab_fini(wintab *wintab)
{
    int index = 0;
    for (; index < wintab->end; index ++)
        wintabitem_fini(wintab->items[index]);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_create_tab(wintab *wintab, Config *cfg)
{ 
    return wintabitem_creat(wintab, cfg); 
}

//-----------------------------------------------------------------------

int wintab_create_sysbtn(wintab *wintab)
{
    wintab->hMinBtn = CreateWindowEx(
        WS_EX_TOPMOST|WS_EX_STATICEDGE|WS_EX_TRANSPARENT, 
        WC_BUTTON, "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
        0, 0, MIN_BTN_WIDTH, SYS_BTN_HEIGHT,
        wintab->hwndTab, NULL, hinst, NULL); 
    wintab->hMaxBtn = CreateWindowEx(
        WS_EX_TOPMOST|WS_EX_STATICEDGE|WS_EX_TRANSPARENT, 
        WC_BUTTON, "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
        0, 0, MAX_BTN_WIDTH, SYS_BTN_HEIGHT,
        wintab->hwndTab, NULL, hinst, NULL); 
    wintab->hClsBtn = CreateWindowEx(
        WS_EX_TOPMOST|WS_EX_STATICEDGE|WS_EX_TRANSPARENT, 
        WC_BUTTON, "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
        0, 0, CLS_BTN_WIDTH, SYS_BTN_HEIGHT,
        wintab->hwndTab, NULL, hinst, NULL); 
    return 0;
}

//-----------------------------------------------------------------------

int wintab_create_toolbar(wintab *wintab)
{
    const int ImageListID = 0;
    const DWORD buttonStyles = BTNS_AUTOSIZE;
    const int bitmapSize = 16;
    HICON hicon; 
    
    wintab->hToolBar = CreateWindowEx(
        WS_EX_TOPMOST,
        TOOLBARCLASSNAME, "",
        WS_CHILD | TBSTYLE_TRANSPARENT | CCS_NORESIZE | CCS_NOPARENTALIGN | CCS_NODIVIDER, 
        0, 0, 100, 23,
        wintab->hwndTab, NULL, hinst, NULL); 

    // Create the imagelist.
    wintab->hImageList = 
        (HWND)ImageList_Create(bitmapSize, bitmapSize, ILC_MASK, 7, 0);

    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_MAINICON)); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);  
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_DUP_SESS)); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_SESS_CFG    )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_RESET_SESS  )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_SFTP        )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_SHOW_LOG    )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_COYP_ALL    )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_PASTE       )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_CLEAR_SB    )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_FULL_SCREEN )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    hicon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_ABOUT       )); 
    ImageList_AddIcon((HIMAGELIST)wintab->hImageList, hicon);
    
    // Set the image list.
    SendMessage(wintab->hToolBar, TB_SETIMAGELIST, (WPARAM)ImageListID, 
        (LPARAM)wintab->hImageList);

    // Load the button images.
    SendMessage(wintab->hToolBar, TB_LOADIMAGES, (WPARAM)IDB_STD_SMALL_COLOR, 
        (LPARAM)HINST_COMMCTRL);

    // Initialize button info.
    TBBUTTON tbButtons[] = 
    {
        { 0, IDM_NEWSESS, TBSTATE_ENABLED,buttonStyles, {0}, 0, (INT_PTR)NULL },
        { 1, IDM_DUPSESS, TBSTATE_ENABLED,  buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 5, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0, (INT_PTR)NULL},
        { 2, IDM_RECONF, TBSTATE_ENABLED, buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 3, IDM_RESTART, TBSTATE_ENABLED, buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 4, IDM_SFTP, 0,  buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 5, IDM_SHOWLOG, TBSTATE_ENABLED, buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 5, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0, (INT_PTR)NULL},
        { 6, IDM_COPYALL, TBSTATE_ENABLED, buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 7, IDM_PASTE, TBSTATE_ENABLED,  buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 8, IDM_CLRSB, TBSTATE_ENABLED, buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 9, IDM_FULLSCREEN, 0, buttonStyles, {0}, 0, (INT_PTR)NULL},
        { 5, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0, (INT_PTR)NULL},
        { 10, IDM_ABOUT, TBSTATE_ENABLED, buttonStyles, {0}, 0, (INT_PTR)NULL}
    };

    const int numButtons = sizeof(tbButtons)/sizeof(TBBUTTON);
    // Add buttons.
    SendMessage(wintab->hToolBar, TB_BUTTONSTRUCTSIZE, 
        (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(wintab->hToolBar, TB_ADDBUTTONS, (WPARAM)numButtons, 
        (LPARAM)&tbButtons);

    // Tell the toolbar to resize itself, and show it.
    //SendMessage(wintab->hToolBar, TB_AUTOSIZE, 0, 0); 
    ShowWindow(wintab->hToolBar, TRUE);
    
    return 0;
}

//-----------------------------------------------------------------------

int wintab_del_tab(wintab *wintab, const int index)
{ 
    int i;
    if (wintab->end  == 1){
        PostMessage(hwnd, WM_CLOSE, 0, 0L);
        return 0;
    }
    
    char *str;
    show_mouseptr(wintab->items[index], 1);
    str = dupprintf("%s Exit Confirmation", wintab->items[index]->cfg.host);
    if (!( wintabitem_can_close(wintab->items[index])||
            MessageBox(wintab->hwndTab,
            	   "Are you sure you want to close this session?",
            	   str, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1)
            == IDOK)){
        return 0;
    }
    
    if (index == wintab->cur){
        wintab->next = (index == (wintab->end -1)) ? (index -1) : (index + 1);
        wintab_swith_tab(wintab);
    }
    wintabitem_fini(wintab->items[index]);
    sfree(wintab->items[index]);
    for (i = index; i < (wintab->end - 1); i++){
        wintab->items[i] = wintab->items[i+1];
    }
    if (wintab->cur > index) wintab->cur -= 1;
    wintab->end -= 1;
    wintab->items[wintab->end] = NULL;
    InvalidateRect(wintab->hwndTab, NULL, TRUE);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_swith_tab(wintab *wintab)
{
    int index = wintab->next;
    if (index == -1)
        return -1;

    ShowWindow(wintab->items[wintab->cur]->page.hwndCtrl, SW_HIDE);
    wintab->cur = index;
    ShowWindow(wintab->items[wintab->cur]->page.hwndCtrl, SW_SHOW);
    SetWindowText(hwnd, wintab->items[wintab->cur]->window_name);
    
    //init the extra size
    RECT rc;
    GetClientRect(wintab->hwndParent, &rc);    
    wintab_resize(wintab, &rc);
    return 0;
}
//-----------------------------------------------------------------------

int wintab_resize(wintab *wintab, const RECT *rc)
{
    RECT wr;
    int xPos;
    int index = wintab->cur;
    int tab_width = rc->right - rc->left;
    int tab_height = rc->bottom - rc->top;
    int border =  (IsZoomed(hwnd)) ? 0 : BORDER_SIZE;
    SetWindowPos(wintab->hwndTab, HWND_BOTTOM, 0, 0, 
        tab_width, tab_height, SWP_NOMOVE);
    
    GetWindowRect(wintab->hwndParent, &wr);
    wintab->extra_width = wr.right - wr.left - tab_width;
    wintab->extra_height = wr.bottom - wr.top - tab_height;

    xPos = rc->right - border - CLS_BTN_WIDTH;
    SetWindowPos(wintab->hClsBtn, 0, xPos, 0, 
        0, 0, SWP_NOSIZE|SWP_NOZORDER);
    xPos -= MAX_BTN_WIDTH;
    SetWindowPos(wintab->hMaxBtn, 0, xPos, 0, 
        0, 0, SWP_NOSIZE|SWP_NOZORDER);
    xPos -= MIN_BTN_WIDTH;
    SetWindowPos(wintab->hMinBtn, 0, xPos, 0, 
        0, 0, SWP_NOSIZE|SWP_NOZORDER);
    
    wintab_split_client_rect(wintab);
    SetWindowPos(wintab->hToolBar, 0, 
        wintab->rcToolBar.left, wintab->rcToolBar.top, 
        wintab->rcToolBar.right - wintab->rcToolBar.left,
        wintab->rcToolBar.bottom - wintab->rcToolBar.top, SWP_NOZORDER);
    wintabpage_resize(&wintab->items[index]->page, &wintab->rcPage, wintab->items[index]->cfg.window_border);
    return 0;
}

//-----------------------------------------------------------------------

void wintab_split_client_rect(wintab *wintab)
{
    RECT rc;
    GetClientRect(wintab->hwndTab, &rc);  

    if (win_fullscr_on_max()&& IsZoomed(hwnd)){
        wintab->rcTabBar.left = 0;
        wintab->rcTabBar.top  = 0;
        wintab->rcTabBar.right = 0;
        wintab->rcTabBar.bottom = 0;
        wintab->rcSysBtn = wintab->rcTabBar;
        wintab->rcToolBar = wintab->rcTabBar;
        wintab->rcPage = rc;
        return;
    }
    int border =  (IsZoomed(hwnd)) ? 0 : BORDER_SIZE;
    wintab->rcTabBar.left = rc.left + border;
    wintab->rcTabBar.top  = rc.top + border;
    wintab->rcTabBar.right = rc.right - border - 100;
    wintab->rcTabBar.bottom = IsZoomed(hwnd) ? 
        (wintab->rcTabBar.top + 25) : (wintab->rcTabBar.top + 35);

    wintab->rcSysBtn = wintab->rcTabBar;
    wintab->rcSysBtn.left = wintab->rcTabBar.right;
    wintab->rcSysBtn.right = rc.right - border;

    wintab->rcToolBar = wintab->rcTabBar;
    wintab->rcToolBar.right = wintab->rcSysBtn.right;
    wintab->rcToolBar.top = wintab->rcTabBar.bottom;
    wintab->rcToolBar.bottom = wintab->rcToolBar.top + 25;

    wintab->rcPage = wintab->rcToolBar;
    wintab->rcPage.top = wintab->rcToolBar.bottom;
    wintab->rcPage.bottom = rc.bottom - border;
     
}

//-----------------------------------------------------------------------

void wintab_onsize(wintab *wintab, HWND hwndParent, LPARAM lParam)
{
    RECT rc; 

    SetRect(&rc, 0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); 
    wintab_resize(wintab, &rc);
    return;
}
//-----------------------------------------------------------------------

int  wintab_can_close(wintab *wintab)
{
    int index = 0;
    for (; index < wintab->end; index++){
        if (!wintabitem_can_close(wintab->items[index]))
            return FALSE;
    }
    return TRUE;
}

//-----------------------------------------------------------------------

void wintab_check_closed_session(wintab *wintab)
{
    wintabitem_check_closed_session(wintab->items[wintab->cur]);
}

//-----------------------------------------------------------------------

void wintab_term_paste(wintab *wintab)
{
    if (wintab->cur < 0 || wintab->cur > wintab->end)
        return;
    wintabitem* tabitem = wintab->items[wintab->cur];
    if (tabitem->back 
        && tabitem->session_closed == FALSE
        && tabitem->term) {
        term_paste(tabitem->term);
    }
}

//-----------------------------------------------------------------------

void wintab_term_set_focus(wintab *wintab, int has_focus)
{
    if (wintab->cur < 0 || wintab->cur > wintab->end)
        return;
    wintabitem* tabitem = wintab->items[wintab->cur];
    if (tabitem->back 
        && tabitem->session_closed == FALSE
        && tabitem->term) {
        term_set_focus(tabitem->term, has_focus);
    }
}

//-----------------------------------------------------------------------

wintabitem* wintab_get_active_item(wintab *wintab)
{
    return wintab->items[wintab->cur];
}

//-----------------------------------------------------------------------

int wintab_del_rgn(wintab *wintab)
{
    int i;
    for (i = 0; i < sizeof(wintab->hSysRgn)/sizeof (HRGN); i++){
        if (wintab->hSysRgn[i])
            DeleteObject(wintab->hSysRgn[i]);
        wintab->hSysRgn[i] = NULL;
    }
    return 0;
}

//-----------------------------------------------------------------------

void wintab_require_resize(wintab *wintab, int tab_width, int tab_height)
{
    int parent_width = tab_width + wintab->extra_width;
    int parent_height = tab_height + wintab->extra_height;
    int border =  (IsZoomed(hwnd)) ? 0 : BORDER_SIZE;
    
    SetWindowPos(wintab->hwndParent, NULL, 0, 0, 
        parent_width, parent_height, SWP_NOMOVE | SWP_NOZORDER); 

    SetWindowPos(wintab->hwndTab, NULL, 0, 0, 
        tab_width, tab_height, SWP_NOMOVE | SWP_NOZORDER); 

    SetWindowPos(wintab->hToolBar, NULL, 0, 0, 
        tab_width - 6, 25, SWP_NOMOVE | SWP_NOZORDER);

    int xPos = tab_width - border - CLS_BTN_WIDTH;
    SetWindowPos(wintab->hClsBtn, 0, xPos, 0, 
        0, 0, SWP_NOSIZE|SWP_NOZORDER);
    xPos -= MAX_BTN_WIDTH;
    SetWindowPos(wintab->hMaxBtn, 0, xPos, 0, 
        0, 0, SWP_NOSIZE|SWP_NOZORDER);
    xPos -= MIN_BTN_WIDTH;
    SetWindowPos(wintab->hMinBtn, 0, xPos, 0, 
        0, 0, SWP_NOSIZE|SWP_NOZORDER);
}

//-----------------------------------------------------------------------

void wintab_get_extra_size(wintab *wintab, int *extra_width, int *extra_height)
{
    *extra_width = wintab->extra_width;
    *extra_height = wintab->extra_height;
}

//-----------------------------------------------------------------------

void wintab_calc_item_size(wintab *wintab, HDC hdc)
{
    if (wintab->end == 0) return ;
    
    int index = 0;   
    RECT rc = wintab->rcTabBar;
    rc.top = rc.bottom - 25;
    int left = rc.left;
    int average_size = (wintab->rcTabBar.right - wintab->rcTabBar.left)/wintab->end;
    wintabitem* tabitem;
    for (index = 0; index < wintab->end; index++){
        tabitem = wintab->items[index];
        rc.left = left;
        rc.right = average_size * (index + 1);

        snprintf(tabitem->disName, 256,  "%d. %s", index+1, tabitem->cfg.host);
        wintabitem_adjust_text_rect(tabitem, hdc, &rc);
        left = tabitem->rcDis.right;
    }
}

//-----------------------------------------------------------------------

int wintab_drawitems(wintab *wintab)
{
    int index = 0;

    if (wintab->end <=0) 
        return -1;
    
    HDC hdc = GetDC(wintab->hwndTab);

    wintab_calc_item_size(wintab, hdc);
    
    for (index = wintab->end - 1; index >= 0; index--){
        if (index == wintab->cur) continue;
        wintab_drawitem(wintab, hdc, index);
    }
    wintab_drawitem(wintab, hdc, wintab->cur);
    
    RECT curRc = wintab->items[wintab->cur]->rcDis;
    DrawLinec(hdc, wintab->rcTabBar.left, wintab->rcTabBar.bottom-1, 
        curRc.left+3, curRc.bottom-1, wintab->bd_col);
    int spread = ((float)(curRc.bottom - curRc.top)) * 2/3;
    DrawLinec(hdc, curRc.right + spread-2, curRc.bottom-1,
        wintab->rcTabBar.right, wintab->rcTabBar.bottom-1, wintab->bd_col);
    ReleaseDC(wintab->hwndTab, hdc);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_drawitem(wintab *wintab, HDC hdc, const int index)
{
    RECT rc;
    COLORREF col, old_col;
    
    //TabCtrl_GetItemRect(wintab->hwndTab, index, &rc);
    wintabitem* tabitem = wintab->items[index];
    rc = tabitem->rcDis;
    const char* name = tabitem->disName;
    int name_len = strlen(name);

    col = (index == wintab->cur)? wintab->sel_col: wintab->nosel_col;
    
    HRGN hRgn = DrawChromeFrame(hdc, &rc,  wintab->bd_col, col);
    wintabitem_set_rgn(tabitem, hRgn);
    old_col = SetBkColor(hdc, col);
    TextOut(hdc, rc.left, rc.top + 4, name, name_len);
    SetBkColor(hdc, old_col);

    hRgn = DrawCloseButton(hdc, rc.right-4, (rc.top + rc.bottom)/2, 
            wintab->bd_col, col);
    wintabitem_set_closer_rgn(tabitem, hRgn);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_draw_sysbtn(wintab *wintab)
{
    RECT rc;
    HDC hdc = GetDC(wintab->hwndTab);
    int border =  (IsZoomed(hwnd)) ? 0 : BORDER_SIZE;

    GetClientRect(wintab->hwndTab, &rc);
    rc.right -= border;
    rc.left = (rc.right - 100) > rc.left ? (rc.right - 100): rc.left ;
    rc.bottom = rc.top + 15;
    wintab_del_rgn(wintab);
    DrawSysButtonFrame(hdc, &rc, wintab->bd_col, wintab->bg_col, wintab->hSysRgn);
    SetWindowRgn(wintab->hMinBtn, wintab->hSysRgn[0], FALSE);
    SetWindowRgn(wintab->hMaxBtn, wintab->hSysRgn[1], FALSE);
    SetWindowRgn(wintab->hClsBtn, wintab->hSysRgn[2], FALSE);
    DrawSysButton(hdc, &rc, wintab->on_col); 
    ReleaseDC(wintab->hwndTab, hdc);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_on_paint(wintab* wintab, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    PAINTSTRUCT p;
    HBRUSH hBrush = NULL;
    HBRUSH hSelBrush = NULL;
    HBRUSH hOldBrush;
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 

    hdc = BeginPaint(hwnd, &p);
    
    hBrush = CreateSolidBrush (wintab->bg_col);
    hOldBrush = SelectObject(hdc, hBrush);
    FillRect(hdc, &p.rcPaint, hBrush); 

    hSelBrush = CreateSolidBrush (wintab->sel_col);
    hOldBrush = SelectObject(hdc, hSelBrush);
    
    hPen = CreatePen(PS_SOLID, 1, wintab->sel_col);
    hOldPen = SelectObject(hdc, hPen); 
    RoundRect(hdc, wintab->rcToolBar.left+1, wintab->rcToolBar.top,
        wintab->rcToolBar.right+1, wintab->rcPage.bottom, 5, 5);
    
    SelectObject(hdc, hOldBrush); 
    DeleteObject(hBrush);
    DeleteObject(hSelBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    wintab_drawitems(wintab);
    //wintab_draw_sysbtn(wintab);
    EndPaint(hwnd, &p);

    InvalidateRect(wintab->hToolBar, NULL, TRUE);
    InvalidateRect(wintab->hMaxBtn, NULL, TRUE);
    InvalidateRect(wintab->hMinBtn, NULL, TRUE);
    InvalidateRect(wintab->hClsBtn, NULL, TRUE);
    

    return 0;
}

//-----------------------------------------------------------------------

int wintab_hit_tab(wintab *wintab, const int x, const int y)
{
    int index = 0;

    if (PtInRegion(wintab->items[wintab->cur]->hRgn, x, y)){
            return wintab->cur;
    }
    for (; index < wintab->end; index++){
        if (index == wintab->cur) continue;
        if (PtInRegion(wintab->items[index]->hRgn, x, y)){
            return index;
        }
    }
    return -1;

}

//-----------------------------------------------------------------------

int wintab_is_dbclick()
{
    static long lasttime = 0;
    long thistime = GetMessageTime();
    if (thistime - lasttime < 200){
        lasttime = thistime;
        return TRUE;
    }
    lasttime = thistime;
    return FALSE;
}

//-----------------------------------------------------------------------

int wintab_on_lclick(wintab* wintab, HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);   
    int is_dbclick = wintab_is_dbclick();
    int index = wintab_hit_tab(wintab, x, y);
    if (index < 0 || index >= wintab->end){
        if (is_dbclick){
            ShowWindow(hwnd, IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);
            return 0;
        }
        return -1;
    }
    
    if (is_dbclick || PtInRegion(wintab->items[index]->hCloserRgn, x, y)){
        wintab_del_tab(wintab, index);
        return 0;
    }
    
    if (wintab->cur == index) 
        return 0;
    
    wintab->next = index;
    wintab_swith_tab(wintab);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_on_drawbtn(wintab* wintab, HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam ;
    RECT rc = dis->rcItem;
    HDC hdc = GetDC(dis->hwndItem);//dis->hDC;

    COLORREF bg_col = (dis->itemAction != ODA_FOCUS)? wintab->bg_col : wintab->nosel_col;
    COLORREF btn_col = (dis->itemAction != ODA_FOCUS)? wintab->nosel_col : wintab->sel_col;
    if (dis->hwndItem == wintab->hMinBtn){
        DrawMinButton(hdc, &rc, wintab->bd_col, bg_col, btn_col);
    }else if (dis->hwndItem == wintab->hMaxBtn){
        DrawMaxButton(hdc, &rc, wintab->bd_col, bg_col, btn_col, IsZoomed(hwnd));
    }else if (dis->hwndItem == wintab->hClsBtn){
        DrawClsButton(hdc, &rc, wintab->bd_col, bg_col, btn_col);
    }
    ReleaseDC(dis->hwndItem, hdc);
    return 0;
}

//-----------------------------------------------------------------------

int wintab_handle_button(wintab* wintab, HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    HWND hitBtn = (HWND)lParam;
    if (hitBtn == NULL) return 0;
    
    if (hitBtn == wintab->hMinBtn){
        ShowWindow(hwnd, SW_MINIMIZE);
    }else if (hitBtn == wintab->hMaxBtn){
        ShowWindow(hwnd, IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);
    }else if (hitBtn == wintab->hClsBtn){
        PostMessage(hwnd, WM_CLOSE, 0, 0L);
    }else {
        on_menu(wintab_get_active_item(wintab), hWnd, message, wParam, lParam);
    }
    //button would hold the focus
    SetFocus(hwnd);
    return 0;
}

//-----------------------------------------------------------------------
int wintab_move_window(wintab* wintab, HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    static int moving = FALSE;
    static RECT rc;
    static POINT ptStart;

    if (IsZoomed(hwnd)) return -1;

    if (message == WM_LBUTTONDOWN){
        moving = TRUE;
        GetWindowRect(hwnd, &rc);
        GetCursorPos(&ptStart);
        SetCapture(wintab->hwndTab); 
        return 0;
    }
    if (message == WM_MOUSEMOVE){
        if (!moving) return -1;
        POINT ptCur;
        GetCursorPos(&ptCur);
        SetWindowPos(hwnd, 0, rc.left + ptCur.x - ptStart.x, 
                rc.top + ptCur.y - ptStart.y, 0 , 0, SWP_NOSIZE|SWP_NOZORDER);
        return 0;
    }
    if (message == WM_LBUTTONUP){
        if (!moving) return -1;
        ReleaseCapture();
        moving = FALSE;
        return 0;
    }
    return -1;
}

//-----------------------------------------------------------------------
typedef enum{
    RESIZE_NONE = 0, 
    RESIZE_X = 1, 
    RESIZE_Y = 2, 
    RESIZE_XY = 3
} ResizeType;

int wintab_resize_window(wintab* wintab, HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    static RECT wc;
    static POINT ptStart;
    static ResizeType resize_type = RESIZE_NONE;

    if (IsZoomed(hwnd)) return -1;
    
    if (message == WM_LBUTTONDOWN){
        GetWindowRect(wintab->hwndTab, &wc);
        GetCursorPos(&ptStart);
        ScreenToClient(wintab->hwndTab, &ptStart);
        int width = wc.right - wc.left;
        int height = wc.bottom - wc.top;
        resize_type = ((ptStart.x >= width -BORDER_SIZE && ptStart.x <= width && ptStart.y >= 0 && ptStart.y <= BORDER_SIZE)
                 ||(ptStart.x >= 0 && ptStart.x <= BORDER_SIZE && ptStart.y >= height -BORDER_SIZE && ptStart.y <= height)
                 ||(ptStart.x >= 0 && ptStart.x <= BORDER_SIZE && ptStart.y >= 0 && ptStart.y <= BORDER_SIZE)
                 ||(ptStart.x >= width -BORDER_SIZE && ptStart.x <= width&& ptStart.y >= height -BORDER_SIZE && ptStart.y <= height)) ? RESIZE_XY
             :((ptStart.x >= 0 && ptStart.x <= BORDER_SIZE) ||(ptStart.x >= width -BORDER_SIZE && ptStart.x <= width))? RESIZE_X
             :((ptStart.y >= 0 && ptStart.y <= BORDER_SIZE) ||(ptStart.y >= height -BORDER_SIZE && ptStart.y <= height))?RESIZE_Y
             : RESIZE_NONE;
        if (resize_type == RESIZE_NONE) 
            return -1;
        SendMessage(hwnd, WM_ENTERSIZEMOVE, wParam, lParam);
        SetCapture(wintab->hwndTab); 
        return 0;
    }
    if (message == WM_MOUSEMOVE){
        if (!resize_type) return -1;
        POINT ptCur;
        GetCursorPos(&ptCur);
        ScreenToClient(wintab->hwndTab, &ptCur);
        int x_offset = (resize_type == RESIZE_Y) ? 0 : (ptCur.x - ptStart.x);
        int y_offset = (resize_type == RESIZE_X) ? 0 : (ptCur.y - ptStart.y);
        wintab_require_resize(wintab, 
            wc.right - wc.left + x_offset, 
            wc.bottom - wc.top + y_offset);
        return 0;
    }
    if (message == WM_LBUTTONUP){
        if (!resize_type) return -1;
        ReleaseCapture();
        resize_type = RESIZE_NONE;
        SendMessage(hwnd, WM_EXITSIZEMOVE, wParam, lParam);
        return 0;
    }
    return -1;
}

//-----------------------------------------------------------------------

VOID CALLBACK wintab_timerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (IsZoomed(hwnd)) {
        KillTimer(hWnd,TID_POLLMOUSE);
        return ;
    }
    
    RECT wc;
    POINT pt;
    wintab* wintab = win_get_data(hWnd);
    
    GetWindowRect(wintab->hwndTab, &wc);
    GetCursorPos(&pt);
    ScreenToClient(wintab->hwndTab, &pt);
    int width = wc.right - wc.left;
    int height = wc.bottom - wc.top;
    
    if ((pt.x >= width -BORDER_SIZE && pt.x <= width && pt.y >= 0 && pt.y <= BORDER_SIZE)
         ||(pt.x >= 0 && pt.x <= BORDER_SIZE && pt.y >= height -BORDER_SIZE && pt.y <= height)){
        SetCursor(LoadCursor(NULL, IDC_SIZENESW));
        return;
    }else if ((pt.x >= 0 && pt.x <= BORDER_SIZE && pt.y >= 0 && pt.y <= BORDER_SIZE)
         ||(pt.x >= width -BORDER_SIZE && pt.x <= width&& pt.y >= height -BORDER_SIZE && pt.y <= height)){
        SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
        return;
    }else if ((pt.x >= 0 && pt.x <= BORDER_SIZE)
         ||(pt.x >= width -BORDER_SIZE && pt.x <= width)){
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        return;
    }else if ((pt.y >= 0 && pt.y <= BORDER_SIZE)
         ||(pt.y >= height -BORDER_SIZE && pt.y <= height)){
        SetCursor(LoadCursor(NULL, IDC_SIZENS));
        return;
    }

    KillTimer(hWnd,TID_POLLMOUSE);

}
//-----------------------------------------------------------------------

LRESULT CALLBACK WintabWndProc(HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{ 
    wintab* tab = win_get_data(hWnd);
    if (tab == NULL) return 0;
    //debug(("[WintabWndProc]%s:%s\n", hWnd == tab->hwndTab? "TabMsg"
    //                        : "UnknowMsg", TranslateWMessage(message))); 
    
    switch (message) {
        case WM_DRAWITEM:
            wintab_on_drawbtn(tab, hWnd, message, wParam, lParam);
            return TRUE;
        case WM_NCHITTEST:
            wintab_timerProc(hWnd, WM_TIMER, TID_POLLMOUSE, 0);
            SetTimer(hWnd,TID_POLLMOUSE,MOUSE_POLL_DELAY,wintab_timerProc);
            break;
        case WM_PAINT:
            wintab_on_paint(tab, hWnd, message, wParam, lParam);
            return 0;

        case WM_LBUTTONDOWN:
            if (wintab_on_lclick(tab, hWnd, message, wParam, lParam) == -1){
                //if on the edge
                if (wintab_resize_window(tab, hWnd, message, wParam, lParam) == -1){
                    //down on the caption
                    wintab_move_window(tab, hWnd, message, wParam, lParam);
                }
                break;
            }
            return 0;
        case WM_MOUSEMOVE:
            wintab_resize_window(tab, hWnd, message, wParam, lParam);
            wintab_move_window(tab, hWnd, message, wParam, lParam);
            return 0;
        case WM_LBUTTONUP:
            wintab_resize_window(tab, hWnd, message, wParam, lParam);
            wintab_move_window(tab, hWnd, message, wParam, lParam);
            return 0;
            
        case WM_COMMAND:
            wintab_handle_button(tab, hWnd, message, wParam, lParam);
            return 0;
    }
    return( CallWindowProc( tab->defWndProc, hWnd, message, wParam, lParam));
}

//-----------------------------------------------------------------------
//tabbar item related
//-----------------------------------------------------------------------

int wintabitem_init(wintab *wintab, wintabitem *tabitem, Config *cfg)
{
    tabitem->hdc = NULL;
    tabitem->send_raw_mouse = 0;
    tabitem->wheel_accumulator = 0;
    tabitem->busy_status = BUSY_NOT;
    tabitem->compose_state = 0;
    tabitem->wm_mousewheel = WM_MOUSEWHEEL;
    tabitem->offset_width = tabitem->offset_height = tabitem->cfg.window_border;
    tabitem->caret_x = -1; 
    tabitem->caret_y = -1;
    tabitem->n_specials = 0;
    tabitem->specials = NULL;
    tabitem->specials_menu = NULL;
    tabitem->extra_width = 25;
    tabitem->extra_height = 28;
    tabitem->font_width = 10;
    tabitem->font_height = 20;
    tabitem->offset_width = tabitem->offset_height = cfg->window_border;
    tabitem->lastact = MA_NOTHING;
    tabitem->lastbtn = MBT_NOTHING;
    tabitem->dbltime = GetDoubleClickTime();
    tabitem->offset_width = cfg->window_border;
    tabitem->offset_height = cfg->window_border;
    tabitem->ignore_clip = FALSE;
    tabitem->hRgn = NULL;
    tabitem->hCloserRgn = NULL;
    tabitem->window_name = tabitem->icon_name = NULL;
    set_title(tabitem, cfg->session_name);
    set_icon(tabitem, cfg->session_name);
    tabitem->close_mutex= CreateMutex(NULL, FALSE, NULL);
    
    tabitem->parentTab = wintab;
    wintabpage_init(&tabitem->page, cfg, wintab->hwndParent);
    tabitem->page.hwndTab = wintab->hwndTab;
    win_bind_data(tabitem->page.hwndCtrl, tabitem); 
    
    adjust_host(cfg);
    tabitem->cfg = *cfg;
    wintabitem_cfgtopalette(tabitem);

    
    memset(&tabitem->ucsdata, 0, sizeof(tabitem->ucsdata));
    tabitem->term = term_init(&tabitem->cfg, &tabitem->ucsdata, tabitem);
    tabitem->logctx = log_init(tabitem, &tabitem->cfg);
    term_provide_logctx(tabitem->term, tabitem->logctx);
    term_size(tabitem->term, tabitem->cfg.height, 
        tabitem->cfg.width, tabitem->cfg.savelines);   
    wintabitem_init_fonts(tabitem, 0, 0);

    wintabitem_CreateCaret(tabitem);
    wintabpage_init_scrollbar(&tabitem->page, tabitem->term);
    wintabitem_init_mouse(tabitem);
    if (wintabitem_start_backend(tabitem) != 0){
        MessageBox(NULL, "failed to start backend!", TEXT("Error"), MB_OK); 
        return -1;
    }

    ShowWindow(tabitem->page.hwndCtrl, SW_SHOW);
//    SetForegroundWindow(tabitem->page.hwndCtrl);

    tabitem->pal = NULL;
    tabitem->logpal = NULL;
    wintabitem_init_palette(tabitem);
    term_set_focus(tabitem->term, TRUE);
    return 0;
}
//-----------------------------------------------------------------------

void wintabitem_fini(wintabitem *tabitem)
{
    wintabitem_close_session(tabitem);

    CloseHandle(tabitem->close_mutex);
    
    wintabpage_fini(&tabitem->page);
    term_free(tabitem->term);
    tabitem->term = NULL;
    log_free(tabitem->logctx);
    tabitem->logctx = NULL;
    
    wintabitem_deinit_fonts(tabitem);
    sfree(tabitem->logpal);
    tabitem->logpal = NULL;
    sfree(tabitem->window_name);tabitem->window_name = NULL;
    sfree(tabitem->icon_name);tabitem->icon_name = NULL;
    if (tabitem->pal)
    	DeleteObject(tabitem->pal);
    if (tabitem->cfg.protocol == PROT_SSH) {
    	random_save_seed();
#ifdef MSCRYPTOAPI
    	crypto_wrapup();
#endif
    }

}
//-----------------------------------------------------------------------

int wintabitem_creat(wintab *wintab, Config *cfg)
{
    int index = wintab->end;
    if (index >= sizeof(wintab->items)/sizeof(wintabitem*)){
        MessageBox(NULL, "reach max tab size", TEXT("Error"), MB_OK); 
        return -1;
    }

    wintab->items[index] = snew(wintabitem);
    if (wintabitem_init(wintab, wintab->items[index], cfg) != 0){
        wintabitem_fini(wintab->items[index]);
        sfree(wintab->items[index]);
        return -1;
    }
    UpdateWindow(wintab->items[index]->page.hwndCtrl);
    
    wintab->next = index;
    wintab_swith_tab(wintab);
    wintab->end++;
    InvalidateRect(wintab->hwndTab, NULL, TRUE);
    return 0;
}

//-----------------------------------------------------------------------


/*
 * Copy the colour palette from the configuration data into defpal.
 * This is non-trivial because the colour indices are different.
 */
void wintabitem_cfgtopalette(wintabitem *tabitem)
{
    int i;
    static const int ww[] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };

    for (i = 0; i < 22; i++) {
	int w = ww[i];
	tabitem->defpal[w].rgbtRed = tabitem->cfg.colours[i][0];
	tabitem->defpal[w].rgbtGreen = tabitem->cfg.colours[i][1];
	tabitem->defpal[w].rgbtBlue = tabitem->cfg.colours[i][2];
    }
    for (i = 0; i < NEXTCOLOURS; i++) {
	if (i < 216) {
	    int r = i / 36, g = (i / 6) % 6, b = i % 6;
	    tabitem->defpal[i+16].rgbtRed = r ? r * 40 + 55 : 0;
	    tabitem->defpal[i+16].rgbtGreen = g ? g * 40 + 55 : 0;
	    tabitem->defpal[i+16].rgbtBlue = b ? b * 40 + 55 : 0;
	} else {
	    int shade = i - 216;
	    shade = shade * 10 + 8;
	    tabitem->defpal[i+16].rgbtRed = tabitem->defpal[i+16].rgbtGreen =
		tabitem->defpal[i+16].rgbtBlue = shade;
	}
    }

    /* Override with system colours if appropriate */
    if (tabitem->cfg.system_colour)
        wintabitem_systopalette(tabitem);
}

//-----------------------------------------------------------------------

/*
 * Override bit of defpal with colours from the system.
 * (NB that this takes a copy the system colours at the time this is called,
 * so subsequent colour scheme changes don't take effect. To fix that we'd
 * probably want to be using GetSysColorBrush() and the like.)
 */
void wintabitem_systopalette(wintabitem *tabitem)
{
    int i;
    static const struct { int nIndex; int norm; int bold; } or[] =
    {
	{ COLOR_WINDOWTEXT,	256, 257 }, /* Default Foreground */
	{ COLOR_WINDOW,		258, 259 }, /* Default Background */
	{ COLOR_HIGHLIGHTTEXT,	260, 260 }, /* Cursor Text */
	{ COLOR_HIGHLIGHT,	261, 261 }, /* Cursor Colour */
    };

    for (i = 0; i < (sizeof(or)/sizeof(or[0])); i++) {
    	COLORREF colour = GetSysColor(or[i].nIndex);
    	tabitem->defpal[or[i].norm].rgbtRed =
    	   tabitem->defpal[or[i].bold].rgbtRed = GetRValue(colour);
    	tabitem->defpal[or[i].norm].rgbtGreen =
    	   tabitem->defpal[or[i].bold].rgbtGreen = GetGValue(colour);
    	tabitem->defpal[or[i].norm].rgbtBlue =
    	   tabitem->defpal[or[i].bold].rgbtBlue = GetBValue(colour);
    }
}

//-----------------------------------------------------------------------

void wintabitem_init_fonts(wintabitem *tabitem, const int pick_width, const int pick_height)
{
    TEXTMETRIC tm;
    CPINFO cpinfo;
    int fontsize[3];
    int i;
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i = 0; i < FONT_MAXNO; i++)
    	tabitem->fonts[i] = NULL;

    tabitem->bold_mode = tabitem->cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
    tabitem->und_mode = UND_FONT;

    if (tabitem->cfg.font.isbold) {
    	fw_dontcare = FW_BOLD;
    	fw_bold = FW_HEAVY;
    } else {
    	fw_dontcare = FW_DONTCARE;
    	fw_bold = FW_BOLD;
    }

    hdc = GetDC(tabitem->page.hwndCtrl);

    if (pick_height)
    	tabitem->font_height = pick_height;
    else {
    	tabitem->font_height = tabitem->cfg.font.height;
    	if (tabitem->font_height > 0) {
    	    tabitem->font_height =
    		-MulDiv(tabitem->font_height, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    	}
    }
    tabitem->font_width = pick_width;

#define f(i,c,w,u) \
    tabitem->fonts[i] = CreateFont (tabitem->font_height, tabitem->font_width, 0, 0, w, FALSE, u, FALSE, \
			   c, OUT_DEFAULT_PRECIS, \
		           CLIP_DEFAULT_PRECIS, FONT_QUALITY(tabitem->cfg.font_quality), \
			   FIXED_PITCH | FF_DONTCARE, tabitem->cfg.font.name)

    f(FONT_NORMAL, tabitem->cfg.font.charset, fw_dontcare, FALSE);

    SelectObject(hdc, tabitem->fonts[FONT_NORMAL]);
    GetTextMetrics(hdc, &tm);

    GetObject(tabitem->fonts[FONT_NORMAL], sizeof(LOGFONT), &tabitem->lfont);

    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm.tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        tabitem->font_varpitch = FALSE;
        tabitem->font_dualwidth = (tm.tmAveCharWidth != tm.tmMaxCharWidth);
    } else {
        tabitem->font_varpitch = TRUE;
        tabitem->font_dualwidth = TRUE;
    }
    if (pick_width == 0 || pick_height == 0) {
    	tabitem->font_height = tm.tmHeight;
        tabitem->font_width = wintabitem_get_font_width(tabitem, hdc, &tm);
    }

#ifdef RDB_DEBUG_PATCH
    debug(23, "Primary font H=%d, AW=%d, MW=%d",
	    tm.tmHeight, tm.tmAveCharWidth, tm.tmMaxCharWidth);
#endif

    {
	CHARSETINFO info;
	DWORD cset = tm.tmCharSet;
	memset(&info, 0xFF, sizeof(info));

	/* !!! Yes the next line is right */
	if (cset == OEM_CHARSET)
	    tabitem->ucsdata.font_codepage = GetOEMCP();
	else
	    if (TranslateCharsetInfo ((DWORD *) cset, &info, TCI_SRCCHARSET))
    		tabitem->ucsdata.font_codepage = info.ciACP;
	else
	    tabitem->ucsdata.font_codepage = -1;

    	GetCPInfo(tabitem->ucsdata.font_codepage, &cpinfo);
    	tabitem->ucsdata.dbcs_screenfont = (cpinfo.MaxCharSize > 1);
    }

    f(FONT_UNDERLINE, tabitem->cfg.font.charset, fw_dontcare, TRUE);

    /*
     * Some fonts, e.g. 9-pt Courier, draw their underlines
     * outside their character cell. We successfully prevent
     * screen corruption by clipping the text output, but then
     * we lose the underline completely. Here we try to work
     * out whether this is such a font, and if it is, we set a
     * flag that causes underlines to be drawn by hand.
     *
     * Having tried other more sophisticated approaches (such
     * as examining the TEXTMETRIC structure or requesting the
     * height of a string), I think we'll do this the brute
     * force way: we create a small bitmap, draw an underlined
     * space on it, and test to see whether any pixels are
     * foreground-coloured. (Since we expect the underline to
     * go all the way across the character cell, we only search
     * down a single column of the bitmap, half way across.)
     */
    {
	HDC und_dc;
	HBITMAP und_bm, und_oldbm;
	int i, gotit;
	COLORREF c;

	und_dc = CreateCompatibleDC(hdc);
	und_bm = CreateCompatibleBitmap(hdc, tabitem->font_width, tabitem->font_height);
	und_oldbm = SelectObject(und_dc, und_bm);
	SelectObject(und_dc, tabitem->fonts[FONT_UNDERLINE]);
	SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
	SetTextColor(und_dc, RGB(255, 255, 255));
	SetBkColor(und_dc, RGB(0, 0, 0));
	SetBkMode(und_dc, OPAQUE);
	ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
	gotit = FALSE;
	for (i = 0; i < tabitem->font_height; i++) {
	    c = GetPixel(und_dc, tabitem->font_width / 2, i);
	    if (c != RGB(0, 0, 0))
		gotit = TRUE;
	}
	SelectObject(und_dc, und_oldbm);
	DeleteObject(und_bm);
	DeleteDC(und_dc);
	if (!gotit) {
	    tabitem->und_mode = UND_LINE;
	    DeleteObject(tabitem->fonts[FONT_UNDERLINE]);
	    tabitem->fonts[FONT_UNDERLINE] = 0;
	}
    }

    if (tabitem->bold_mode == BOLD_FONT) {
	f(FONT_BOLD, tabitem->cfg.font.charset, fw_bold, FALSE);
    }
#undef f

    tabitem->descent = tm.tmAscent + 1;
    if (tabitem->descent >= tabitem->font_height)
    	tabitem->descent = tabitem->font_height - 1;

    for (i = 0; i < 3; i++) {
	if (tabitem->fonts[i]) {
	    if (SelectObject(hdc, tabitem->fonts[i]) && GetTextMetrics(hdc, &tm))
    		fontsize[i] = wintabitem_get_font_width(tabitem, hdc, &tm) + 256 * tm.tmHeight;
	    else
		fontsize[i] = -i;
	} else
	    fontsize[i] = -i;
    }

    ReleaseDC(tabitem->page.hwndCtrl, hdc);

    if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
	tabitem->und_mode = UND_LINE;
	DeleteObject(tabitem->fonts[FONT_UNDERLINE]);
	tabitem->fonts[FONT_UNDERLINE] = 0;
    }

    if (tabitem->bold_mode == BOLD_FONT &&
        	fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
    	tabitem->bold_mode = BOLD_SHADOW;
    	DeleteObject(tabitem->fonts[FONT_BOLD]);
    	tabitem->fonts[FONT_BOLD] = 0;
    }
    tabitem->fontflag[0] = tabitem->fontflag[1] = tabitem->fontflag[2] = 1;
    init_ucs(&tabitem->cfg, &tabitem->ucsdata);
}

void wintabitem_deinit_fonts(wintabitem *tabitem)
{
    int i;
    for (i = 0; i < FONT_MAXNO; i++) {
    	if (tabitem->fonts[i])
    	    DeleteObject(tabitem->fonts[i]);
    	tabitem->fonts[i] = 0;
    	tabitem->fontflag[i] = 0;
    }
}
//-----------------------------------------------------------------------

int wintabitem_get_font_width(wintabitem *tabitem, HDC hdc, const TEXTMETRIC *tm)
{
    int ret;
    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm->tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        ret = tm->tmAveCharWidth;
    } else {
#define FIRST '0'
#define LAST '9'
        ABCFLOAT widths[LAST-FIRST + 1];
        int j;

        tabitem->font_varpitch = TRUE;
        tabitem->font_dualwidth = TRUE;
        if (GetCharABCWidthsFloat(hdc, FIRST, LAST, widths)) {
            ret = 0;
            for (j = 0; j < lenof(widths); j++) {
                int width = (int)(0.5 + widths[j].abcfA +
                                  widths[j].abcfB + widths[j].abcfC);
                if (ret < width)
                    ret = width;
            }
        } else {
            ret = tm->tmMaxCharWidth;
        }
#undef FIRST
#undef LAST
    }
    return ret;
}

//-----------------------------------------------------------------------

int wintabitem_CreateCaret(wintabitem *tabitem)
{
    /*
     * Set up a caret bitmap, with no content.
     */
	char *bits;
	int size = (tabitem->font_width + 15) / 16 * 2 * tabitem->font_height;
	bits = snewn(size, char);
	memset(bits, 0, size);
	tabitem->caretbm = CreateBitmap(tabitem->font_width, tabitem->font_height, 1, 1, bits);
	sfree(bits);

    CreateCaret(tabitem->page.hwndCtrl, tabitem->caretbm, 
        tabitem->font_width, tabitem->font_height);
    return 0;
}
//-----------------------------------------------------------------------

int wintabitem_init_mouse(wintabitem *tabitem)
{
    tabitem->lastact = MA_NOTHING;
    tabitem->lastbtn = MBT_NOTHING;
    tabitem->dbltime = GetDoubleClickTime();
    return 0;
}

//-----------------------------------------------------------------------

int wintabitem_start_backend(wintabitem *tabitem)
{
    const char *error;
    char msg[1024] ;
    char *realhost; 
    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    tabitem->back = backend_from_proto(tabitem->cfg.protocol);
    if (tabitem->back == NULL) {
    	char *str = dupprintf("%s Internal Error", appname);
    	MessageBox(NULL, "Unsupported protocol number found",
    		   str, MB_OK | MB_ICONEXCLAMATION);
    	sfree(str);
	    return -1;
    }

    error = tabitem->back->init(tabitem, &tabitem->backhandle, &tabitem->cfg,
		       tabitem->cfg.host, tabitem->cfg.port, &realhost, tabitem->cfg.tcp_nodelay,
		       tabitem->cfg.tcp_keepalives);
    tabitem->back->provide_logctx(tabitem->backhandle, tabitem->logctx);
    if (error) {
    	char *str = dupprintf("%s Error", appname);
    	sprintf(msg, "Unable to open connection to\n"
    		"%.800s\n" "%s", cfg_dest(&cfg), error);
    	MessageBox(NULL, msg, str, MB_ICONERROR | MB_OK);
    	sfree(str);
	    return -1;
    }

    sfree(realhost);

    /*
     * Connect the terminal to the backend for resize purposes.
     */
    term_provide_resize_fn(tabitem->term, tabitem->back->size, tabitem->backhandle);

    /*
     * Set up a line discipline.
     */
    tabitem->ldisc = ldisc_create(&tabitem->cfg, tabitem->term
                    , tabitem->back, tabitem->backhandle, NULL);

    tabitem->must_close_session = FALSE;
    tabitem->session_closed = FALSE;
    return 0;
}

//-----------------------------------------------------------------------

void wintabitem_init_palette(wintabitem *tabitem)
{
    int i;
    HDC hdc = GetDC(tabitem->page.hwndCtrl);
    if (hdc) {
	if (tabitem->cfg.try_palette && GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
	    /*
	     * This is a genuine case where we must use smalloc
	     * because the snew macros can't cope.
	     */
	    tabitem->logpal = smalloc(sizeof(*tabitem->logpal)
			     - sizeof(tabitem->logpal->palPalEntry)
			     + NALLCOLOURS * sizeof(PALETTEENTRY));
	    tabitem->logpal->palVersion = 0x300;
	    tabitem->logpal->palNumEntries = NALLCOLOURS;
	    for (i = 0; i < NALLCOLOURS; i++) {
    		tabitem->logpal->palPalEntry[i].peRed = tabitem->defpal[i].rgbtRed;
    		tabitem->logpal->palPalEntry[i].peGreen = tabitem->defpal[i].rgbtGreen;
    		tabitem->logpal->palPalEntry[i].peBlue = tabitem->defpal[i].rgbtBlue;
    		tabitem->logpal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
	    }
	    tabitem->pal = CreatePalette(tabitem->logpal);
	    if (tabitem->pal) {
    		SelectPalette(hdc, tabitem->pal, FALSE);
    		RealizePalette(hdc);
    		SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
	    }
	}
	ReleaseDC(tabitem->page.hwndCtrl, hdc);
    }
    if (tabitem->pal){
    	for (i = 0; i < NALLCOLOURS; i++)
    	    tabitem->colours[i] = PALETTERGB(tabitem->defpal[i].rgbtRed,
    				    tabitem->defpal[i].rgbtGreen,
    				    tabitem->defpal[i].rgbtBlue);
    } else {
    	for (i = 0; i < NALLCOLOURS; i++)
    	    tabitem->colours[i] = RGB(tabitem->defpal[i].rgbtRed,
    			     tabitem->defpal[i].rgbtGreen, tabitem->defpal[i].rgbtBlue);
    }
}

//-----------------------------------------------------------------------

void wintabitem_check_closed_session(wintabitem *tabitem)
{
    if (tabitem->must_close_session)
		wintabitem_close_session(tabitem);
    
}

//-----------------------------------------------------------------------

void wintabitem_close_session(wintabitem *tabitem)
{
    if (!tabitem->back)  return;
    WaitForSingleObject(tabitem->close_mutex, INFINITE);

    if (!tabitem->back)  {
        ReleaseMutex(tabitem->close_mutex);
        return;
    }
    //char morestuff[100];
    //int i;


    //sprintf(morestuff, "%.70s (inactive)", appname);
    //set_icon(NULL, morestuff);
    //set_title(NULL, morestuff);
    
    
    tabitem->session_closed = TRUE;
    if (tabitem->ldisc) {
    	ldisc_free(tabitem->ldisc);
    	tabitem->ldisc = NULL;
    }
    if (tabitem->back) {
    	tabitem->back->free(tabitem->backhandle);
    	tabitem->backhandle = NULL;
    	tabitem->back = NULL;
        term_provide_resize_fn(tabitem->term, NULL, NULL);
        update_specials_menu(tabitem);
	
    }

    /*
     * Show the Restart Session menu item. Do a precautionary
     * delete first to ensure we never end up with more than one.
     */
    //for (i = 0; i < lenof(popup_menus); i++) {
	//DeleteMenu(popup_menus[i].menu, IDM_RESTART, MF_BYCOMMAND);
	//InsertMenu(popup_menus[i].menu, IDM_DUPSESS, MF_BYCOMMAND | MF_ENABLED,
	//	   IDM_RESTART, "&Restart Session");
    //}
    ReleaseMutex(tabitem->close_mutex);
}

//-----------------------------------------------------------------------

void wintabitem_require_resize(wintabitem *tabitem, int term_width, int term_height)
{
    int page_width = term_width + tabitem->page.extra_page_width;
    int page_height = term_height + tabitem->page.extra_page_height;
    int parent_width = page_width + tabitem->page.extra_width;
    int parent_height = page_height + tabitem->page.extra_height;
    
    wintab_require_resize(tabitem->parentTab, parent_width, parent_height); 

    SetWindowPos(tabitem->page.hwndCtrl, NULL, 0, 0, 
        page_width, page_height, SWP_NOMOVE | SWP_NOZORDER); 
    //MoveWindow(tabitem->page.hwndCtrl, rc->left, rc->top, 
    //    rc->right - rc->left, 
    //    rc->bottom - rc->top, TRUE);
}

//-----------------------------------------------------------------------

void wintabitem_get_extra_size(wintabitem *tabitem, int *extra_width, int *extra_height)
{
    wintab_get_extra_size(tabitem->parentTab, extra_width, extra_height);
    *extra_width += tabitem->page.extra_page_width + tabitem->page.extra_width;
    *extra_height += tabitem->page.extra_page_height + tabitem->page.extra_height;
}

//-----------------------------------------------------------------------

int wintabitem_can_close(wintabitem *tabitem)
{
    if (tabitem->cfg.warn_on_close && !tabitem->session_closed)
        return FALSE;
    return TRUE;
}

//-----------------------------------------------------------------------

void wintabitem_set_rgn(wintabitem *tabitem, HRGN hRgn)
{
    if (tabitem->hRgn)
        DeleteObject(tabitem->hRgn);
    tabitem->hRgn = hRgn;
}

//-----------------------------------------------------------------------

void wintabitem_set_closer_rgn(wintabitem *tabitem, HRGN hRgn)
{
    if (tabitem->hCloserRgn)
        DeleteObject(tabitem->hCloserRgn);
    tabitem->hCloserRgn = hRgn;
}

//-----------------------------------------------------------------------

void wintabitem_adjust_text_rect(wintabitem *tabitem, HDC hdc, const RECT* rc)
{
    const int padding = 35;
    
    int len = strlen(tabitem->disName);
    int fitNum;
    SIZE sz;
    SIZE dot3sz;
    
    tabitem->rcDis = *rc;
    int nMaxExtent = rc->right - rc->left - padding;
    if(GetTextExtentExPoint(hdc, tabitem->disName, len,
            nMaxExtent, &fitNum, NULL, &sz)!=0){
        
		if(fitNum < len){	
            if(fitNum > 0){
                GetTextExtentPoint(hdc, "...", strlen("..."), &dot3sz);
                nMaxExtent -= dot3sz.cx;
                
                //too narrow to have a char
                if (nMaxExtent <= 0){
                    strcpy(tabitem->disName, ".");
                    return;
                }
                
				if(GetTextExtentExPoint(hdc, tabitem->disName, len,
                    nMaxExtent, &fitNum, NULL, &sz)==0) {
                    //at lest it can show the dots.
                    strcpy(tabitem->disName, "...");	 
                	return ;
				}
                //append the dots.
                tabitem->disName[fitNum] = '\0';
                strcat(tabitem->disName, "...");
                return;
			}	else{
				strcpy(tabitem->disName, ".");
                return;
			}
		} else{
		    //too wide
		    tabitem->rcDis.right = tabitem->rcDis.left + sz.cx + padding;
		    return ;
        }
        
	}

}

//-----------------------------------------------------------------------

int wintabitem_on_scroll(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    switch (LOWORD(wParam)) {
	  case SB_BOTTOM:
	    term_scroll(tabitem->term, -1, 0);
	    break;
	  case SB_TOP:
	    term_scroll(tabitem->term, +1, 0);
	    break;
	  case SB_LINEDOWN:
	    term_scroll(tabitem->term, 0, +1);
	    break;
	  case SB_LINEUP:
	    term_scroll(tabitem->term, 0, -1);
	    break;
	  case SB_PAGEDOWN:
	    term_scroll(tabitem->term, 0, +tabitem->term->rows / 2);
	    break;
	  case SB_PAGEUP:
	    term_scroll(tabitem->term, 0, -tabitem->term->rows / 2);
	    break;
	  case SB_THUMBPOSITION:
	  case SB_THUMBTRACK:
	    term_scroll(tabitem->term, 1, HIWORD(wParam));
	    break;
	}
    return 0;
}

//-----------------------------------------------------------------------

int wintabitem_on_paint(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    wintabitem tmp_tabitem = *tabitem;
    PAINTSTRUCT p;

    HideCaret(tmp_tabitem.page.hwndCtrl);
    tmp_tabitem.hdc = BeginPaint(tmp_tabitem.page.hwndCtrl, &p);
    if (tmp_tabitem.pal) {
    	SelectPalette(tmp_tabitem.hdc, tmp_tabitem.pal, TRUE);
    	RealizePalette(tmp_tabitem.hdc);
    }

    /*
     * We have to be careful about term_paint(). It will
     * set a bunch of character cells to INVALID and then
     * call do_paint(), which will redraw those cells and
     * _then mark them as done_. This may not be accurate:
     * when painting in WM_PAINT context we are restricted
     * to the rectangle which has just been exposed - so if
     * that only covers _part_ of a character cell and the
     * rest of it was already visible, that remainder will
     * not be redrawn at all. Accordingly, we must not
     * paint any character cell in a WM_PAINT context which
     * already has a pending update due to terminal output.
     * The simplest solution to this - and many, many
     * thanks to Hung-Te Lin for working all this out - is
     * not to do any actual painting at _all_ if there's a
     * pending terminal update: just mark the relevant
     * character cells as INVALID and wait for the
     * scheduled full update to sort it out.
     * 
     * I have a suspicion this isn't the _right_ solution.
     * An alternative approach would be to have terminal.c
     * separately track what _should_ be on the terminal
     * screen and what _is_ on the terminal screen, and
     * have two completely different types of redraw (one
     * for full updates, which syncs the former with the
     * terminal itself, and one for WM_PAINT which syncs
     * the latter with the former); yet another possibility
     * would be to have the Windows front end do what the
     * GTK one already does, and maintain a bitmap of the
     * current terminal appearance so that WM_PAINT becomes
     * completely trivial. However, this should do for now.
     */
    term_paint(tmp_tabitem.term, (Context)&tmp_tabitem, 
	       (p.rcPaint.left-tmp_tabitem.offset_width)/tmp_tabitem.font_width,
	       (p.rcPaint.top-tmp_tabitem.offset_height)/tmp_tabitem.font_height,
	       (p.rcPaint.right-tmp_tabitem.offset_width-1)/tmp_tabitem.font_width,
	       (p.rcPaint.bottom-tmp_tabitem.offset_height-1)/tmp_tabitem.font_height,
	       !tmp_tabitem.term->window_update_pending);

    if (p.fErase ||
        p.rcPaint.left  < tmp_tabitem.offset_width  ||
	p.rcPaint.top   < tmp_tabitem.offset_height ||
	p.rcPaint.right >= tmp_tabitem.offset_width + tmp_tabitem.font_width*tmp_tabitem.term->cols ||
	p.rcPaint.bottom>= tmp_tabitem.offset_height + tmp_tabitem.font_height*tmp_tabitem.term->rows)
    {
    	HBRUSH fillcolour, oldbrush;
    	HPEN   edge, oldpen;
    	fillcolour = CreateSolidBrush (
    			    tmp_tabitem.colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
    	oldbrush = SelectObject(tmp_tabitem.hdc, fillcolour);
    	edge = CreatePen(PS_SOLID, 0, 
    			    tmp_tabitem.colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
    	oldpen = SelectObject(tmp_tabitem.hdc, edge);

    	/*
    	 * Jordan Russell reports that this apparently
    	 * ineffectual IntersectClipRect() call masks a
    	 * Windows NT/2K bug causing strange display
    	 * problems when the PuTTY window is taller than
    	 * the primary monitor. It seems harmless enough...
    	 */
    	IntersectClipRect(tmp_tabitem.hdc,
    		p.rcPaint.left, p.rcPaint.top,
    		p.rcPaint.right, p.rcPaint.bottom);

    	ExcludeClipRect(tmp_tabitem.hdc, 
    		tmp_tabitem.offset_width, tmp_tabitem.offset_height,
    		tmp_tabitem.offset_width+tmp_tabitem.font_width*tmp_tabitem.term->cols,
    		tmp_tabitem.offset_height+tmp_tabitem.font_height*tmp_tabitem.term->rows);

    	Rectangle(tmp_tabitem.hdc, p.rcPaint.left, p.rcPaint.top, 
    		  p.rcPaint.right, p.rcPaint.bottom);

    	/* SelectClipRgn(hdc, NULL); */

    	SelectObject(tmp_tabitem.hdc, oldbrush);
    	DeleteObject(fillcolour);
    	SelectObject(tmp_tabitem.hdc, oldpen);
    	DeleteObject(edge);
    }
    SelectObject(tmp_tabitem.hdc, GetStockObject(SYSTEM_FONT));
    SelectObject(tmp_tabitem.hdc, GetStockObject(WHITE_PEN));
    EndPaint(tmp_tabitem.page.hwndCtrl, &p);
    ShowCaret(tmp_tabitem.page.hwndCtrl);

    return 0;
}

//-----------------------------------------------------------------------
//page related
//-----------------------------------------------------------------------
int wintabpage_init(wintabpage *page, const Config *cfg, HWND hwndParent)
{
    wintabpage_register();

    int winmode = WS_CHILD | WS_VSCROLL ;
	if (!cfg->scrollbar)
	    winmode &= ~(WS_VSCROLL);
	page->hwndCtrl = CreateWindowEx(
                    WS_EX_TOPMOST, 
                    WINTAB_PAGE_CLASS, 
                    WINTAB_PAGE_CLASS,
			        winmode, 
			        0, 0, 0, 0,
			        hwndParent, 
			        NULL,   /* hMenu */
			        hinst, 
			        NULL);  /* lpParam */

    if (page->hwndCtrl == NULL){
        ErrorExit("CreatePage");
        ExitProcess(2); 
    }
    
    return 0;

}

//-----------------------------------------------------------------------

void wintabpage_init_scrollbar(wintabpage *page, Terminal *term)
{
    SCROLLINFO si;

	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = term->rows - 1;
	si.nPage = term->rows;
	si.nPos = 0;
	SetScrollInfo(page->hwndCtrl, SB_VERT, &si, FALSE);
}

//-----------------------------------------------------------------------

int wintabpage_fini(wintabpage *page)
{
    DestroyWindow(page->hwndCtrl);
    return 0;
}

//-----------------------------------------------------------------------

int wintabpage_resize(wintabpage *page, const RECT *rc, const int cfg_winborder)
{
    RECT tc, pc;
    int page_width = rc->right - rc->left;
    int page_height = rc->bottom - rc->top;
    MoveWindow(page->hwndCtrl, rc->left, rc->top, 
        page_width, 
        page_height, TRUE);
    
    GetWindowRect(page->hwndTab, &tc);
    page->extra_width = tc.right - tc.left - page_width;
    page->extra_height = tc.bottom - tc.top - page_height;

    GetClientRect(page->hwndCtrl, &pc);
    page->extra_page_width = page_width - (pc.right - pc.left) + cfg_winborder*2;
    page->extra_page_height = page_height - (pc.bottom - pc.top) + cfg_winborder*2;
    return 0;
}

//-----------------------------------------------------------------------


/*
 * not thread safe
 */
int wintabpage_register()
{
    if (wintabpage_registed)
        return 0;
    wintabpage_registed = 1;
    /*
    WNDCLASS wndclass;
    wndclass.style = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;;
	wndclass.lpfnWndProc = WintabpageWndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hinst;
	wndclass.hIcon = NULL;
	wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM);
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = WINTAB_PAGE_CLASS;
	RegisterClass(&wndclass))
        ErrorExit();
        */

    WNDCLASSEX wndclass;
	wndclass.cbSize = sizeof(wndclass);
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = WintabpageWndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hinst;
	wndclass.hIcon = NULL;
	wndclass.hCursor = NULL;
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = WINTAB_PAGE_CLASS;
	wndclass.hIconSm = 0;
	RegisterClassEx(&wndclass);
    return 0;
}

//-----------------------------------------------------------------------


int wintabpage_unregister()
{
    return UnregisterClass(WINTAB_PAGE_CLASS, hinst);
}

//-----------------------------------------------------------------------

void win_bind_data(HWND hwnd, void *data)
{
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
}

//-----------------------------------------------------------------------

void* win_get_data(HWND hwnd)
{
    return (void *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

//-----------------------------------------------------------------------

void wintabpage_get_term_size(wintabpage *page, int *term_width, int *term_height)
{
    RECT rc;
    GetWindowRect(page->hwndCtrl, &rc);
    
    *term_width = rc.right - rc.left - page->extra_page_width;
    *term_height = rc.bottom - rc.top - page->extra_page_height;
}

//-----------------------------------------------------------------------


LRESULT CALLBACK WintabpageWndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    //extern wintab tab;
    //debug(("[WintabpageWndProc]%s:%s\n", hwnd == tab.items[tab.cur]->page.hwndCtrl ? "PageMsg"
    //                        : "UnknowMsg", TranslateWMessage(message)));
    wintabitem* tabitem = win_get_data(hwnd);
    if (tabitem == NULL){
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    
    switch (message) {
        case WM_COMMAND:
            on_menu(tabitem, hwnd, message, wParam, lParam);
            break;
        
        case WM_VSCROLL:
	        wintabitem_on_scroll(tabitem, hwnd, message, wParam, lParam);
        	break;
        case WM_NCPAINT:
        case WM_PAINT:
            wintabitem_on_paint(tabitem, hwnd, message,wParam, lParam);
    		break;

        //mouse button
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
	        on_button(tabitem, hwnd, message, wParam, lParam);
            return 0;
        case WM_MOUSEMOVE:
            on_mouse_move(tabitem, hwnd, message, wParam, lParam);
        	return 0;
        case WM_NCMOUSEMOVE:
        	on_nc_mouse_move(tabitem, hwnd, message, wParam, lParam);
        	break;

        //paste       
        case WM_GOT_CLIPDATA:
        	if (process_clipdata((HGLOBAL)lParam, wParam))
    	        term_do_paste(tabitem->term);
        	return 0;
        case WM_IGNORE_CLIP:
        	tabitem->ignore_clip = wParam;	       /* don't panic on DESTROYCLIPBOARD */
        	break;
        case WM_DESTROYCLIPBOARD:
        	if (!tabitem->ignore_clip)
        	    term_deselect(tabitem->term);
        	tabitem->ignore_clip = FALSE;
        	return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
	        if (on_key(tabitem, hwnd, message,wParam, lParam))
                break;
        default:
            break;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

//-----------------------------------------------------------------------
//Config post handling
//-----------------------------------------------------------------------

void ltrim(char* str)
{
    int space = strspn(str, " \t");
    memmove(str, str+space, 1+strlen(str)-space);
}

//-----------------------------------------------------------------------
/* 
 * See if host is of the form user@host
 */
void takeout_username_from_host(Config *cfg)
{
	if (cfg->host[0] != '\0') {
	    char *atsign = strrchr(cfg->host, '@');
	    /* Make sure we're not overflowing the user field */
	    if (atsign) {
    		if (atsign - cfg->host < sizeof cfg->username) {
    		    strncpy(cfg->username, cfg->host, atsign - cfg->host);
    		    cfg->username[atsign - cfg->host] = '\0';
    		}
    		memmove(cfg->host, atsign + 1, 1 + strlen(atsign + 1));
	    }
	}
}

//-----------------------------------------------------------------------
/*
 * Trim a colon suffix off the hostname if it's there. In
 * order to protect IPv6 address literals against this
 * treatment, we do not do this if there's _more_ than one
 * colon.
 */
void handle_host_colon(char *host)
{
    char *c = strchr(host, ':');

    if (c) {
	char *d = strchr(c+1, ':');
	if (!d)
	    *c = '\0';
    }

}

//-----------------------------------------------------------------------
/*
 * Remove any remaining whitespace from the hostname.
 */
void rm_whitespace(char *host)
{
    int p1 = 0, p2 = 0;
    while (host[p2] != '\0') {
    	if (host[p2] != ' ' && host[p2] != '\t') {
    	    host[p1] = host[p2];
    	    p1++;
    	}
    	p2++;
    }
    host[p1] = '\0';
}

//-----------------------------------------------------------------------

void adjust_host(Config *cfg)
{
    /*
	 * Trim leading whitespace off the hostname if it's there.
	 */
    ltrim(cfg->host);
	
	/* See if host is of the form user@host */
	takeout_username_from_host(cfg);

	/*
	 * Trim a colon suffix off the hostname if it's there. In
	 * order to protect IPv6 address literals against this
	 * treatment, we do not do this if there's _more_ than one
	 * colon.
	 */
	handle_host_colon(cfg->host);

	/*
	 * Remove any remaining whitespace from the hostname.
	 */
	rm_whitespace(cfg->host);
}

//-----------------------------------------------------------------------
//colour palette related
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
//debug related
//-----------------------------------------------------------------------
void ErrorExit(char * str) 
{ 
    LPVOID lpMsgBuf;
    char* buf;
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );
    buf = dupprintf("%s failed with error %d: %s", str, dw, lpMsgBuf);

    MessageBox(NULL, (LPCTSTR)buf, TEXT("Error"), MB_OK); 

    sfree(buf);
    LocalFree(lpMsgBuf);
    ExitProcess(dw); 
}

//-----------------------------------------------------------------------
//end
//-----------------------------------------------------------------------

