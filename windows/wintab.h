/*
 * wintab.h define the tabbar and its page.
 * 
 */
#ifndef WINTAB_H
#define WINTAB_H

typedef struct {
    HWND hwndTab;
    HWND hwndPage;
}wintab;

int wintab_init(wintab *wintab, HWND hwndParent);
int wintab_fini(wintab *wintab);
int wintab_resize(wintab *wintab, const RECT *rc);
void wintab_onsize(wintab *wintab, HWND hwndParent, LPARAM lParam);

#endif
