/*
 * wintab.c - the implementation of the tabbar and its page.
 */

#ifndef NO_MULTIMON
#include <multimon.h>
#endif

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include "wintab.h"

extern HINSTANCE hinst;
/*
 * return 0 if succeed, -1 if failed.
 */
int wintab_init(wintab *wintab, HWND hwndParent)
{
    RECT rc; 
    TCITEM tie; 

    GetClientRect(hwndParent, &rc); 

    /* create the page */
    wintab->hwndPage = CreateWindow(WC_STATIC, L"", 
        WS_CHILD | SS_OWNERDRAW  | WS_VISIBLE, 
        0, 0, rc.right, rc.bottom, 
        hwndParent, NULL, hinst, NULL);;
    if (wintab->hwndPage == NULL)
        return -1;

    /* create tabar */
    INITCOMMONCONTROLSEX icce;
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_TAB_CLASSES;
	InitCommonControlsEx(&icce);
    
    wintab->hwndTab = CreateWindow(WC_TABCONTROL, L"", 
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE, 
        0, 0, rc.right, rc.bottom, 
        hwndParent, NULL, hinst, NULL); 
    if (wintab->hwndTab == NULL)
        return -1; 
 
    tie.mask = TCIF_TEXT | TCIF_IMAGE; 
    tie.iImage = -1; 
    tie.pszText = "test"; 
    if (TabCtrl_InsertItem(wintab->hwndTab, 0, &tie) == -1) { 
        DestroyWindow(wintab->hwndTab); 
        return -1; 
    } 
    
    wintab_resize(wintab, &rc);
    return 0; 
}

int wintab_fini(wintab *wintab)
{
    return 0;
}

int wintab_resize(wintab *wintab, const RECT *rc)
{
    RECT rcPage = *rc;
    HDWP hdwp;
    
    TabCtrl_AdjustRect(wintab->hwndTab, FALSE, &rcPage);
    MoveWindow(wintab->hwndPage, rcPage.left, rcPage.top, rcPage.right, rcPage.bottom, TRUE);
    //InvalidateRect(wintab->hwndPage, NULL, TRUE);
    //UpdateWindow(wintab->hwndPage);
    
    hdwp = BeginDeferWindowPos(2);  
    DeferWindowPos(hdwp, wintab->hwndTab, NULL, rc->left, rc->top, rc->right, 
        rc->bottom, SWP_NOZORDER);     
    DeferWindowPos(hdwp, wintab->hwndPage, HWND_TOPMOST, rcPage.left, rcPage.top, 
        rcPage.right - rcPage.left, rcPage.bottom - rcPage.top, SWP_NOMOVE); 
    EndDeferWindowPos(hdwp); 
    return 0;
}

void wintab_onsize(wintab *wintab, HWND hwndParent, LPARAM lParam)
{
    HDWP hdwp; 
    RECT rc; 

    SetRect(&rc, 0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); 
    wintab_resize(wintab, &rc);

    return;
}

