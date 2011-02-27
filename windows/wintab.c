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

#define TIMING_TIMER_ID 1235

#define X_POS(l) ((int)(short)LOWORD(l))
#define Y_POS(l) ((int)(short)HIWORD(l))
#define TO_CHR_X(x) ((((x)<0 ? (x)-font_width+1 : (x))-offset_width) / font_width)
#define TO_CHR_Y(y) ((((y)<0 ? (y)-font_height+1: (y))-offset_height) / font_height)

extern HINSTANCE hinst;
extern Config cfg;
extern Terminal *term;
extern HPALETTE pal;
extern COLORREF colours[NALLCOLOURS];
extern void *ldisc;

const char* const WINTAB_PAGE_CLASS = "WintabPage";
int wintabpage_registed = 0;

static int send_raw_mouse = 0;
static int wheel_accumulator = 0;
static int dbltime, lasttime, lastact;
static Mouse_Button lastbtn;
static int extra_width, extra_height;
static int font_width, font_height, font_dualwidth, font_varpitch;
static int offset_width, offset_height;
static int caret_x = -1, caret_y = -1;


//-----------------------------------------------------------------------
// tabbar related
//-----------------------------------------------------------------------
/*
 * return 0 if succeed, -1 if failed.
 */
int wintab_init(wintab *wintab, HWND hwndParent)
{
    RECT rc; 

    /* create tabar */
    INITCOMMONCONTROLSEX icce;
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_TAB_CLASSES;
	InitCommonControlsEx(&icce);
    
    GetClientRect(hwndParent, &rc);    
    wintab->hwndTab = CreateWindow(WC_TABCONTROL, "", 
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE, 
        0, 0, rc.right, rc.bottom, 
        hwndParent, NULL, hinst, NULL); 
    if (wintab->hwndTab == NULL)
        return -1; 

    wintab->hwndParent = hwndParent;
 
    wintabitem_creat(wintab, &cfg);
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
    wintabpage_resize(&wintab->items[0].page, &rcPage);

    hdwp = BeginDeferWindowPos(1);  
    DeferWindowPos(hdwp, wintab->hwndTab, NULL, rc->left, rc->top, rc->right, 
        rc->bottom, SWP_NOZORDER);     
    EndDeferWindowPos(hdwp); 
    return 0;
}

void wintab_onsize(wintab *wintab, HWND hwndParent, LPARAM lParam)
{
    RECT rc; 

    SetRect(&rc, 0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); 
    wintab_resize(wintab, &rc);
    return;
}

//-----------------------------------------------------------------------

void wintab_check_closed_session(wintab *wintab)
{
    wintabitem_check_closed_session(&wintab->items[0]);
}

//-----------------------------------------------------------------------

void wintab_term_paste(wintab *wintab)
{
    //for
    term_paste(wintab->items[0].term);
}

//-----------------------------------------------------------------------

void wintab_term_set_focus(wintab *wintab, int has_focus)
{
    //get select ...
    term_set_focus(wintab->items[0].term, has_focus);
}

//-----------------------------------------------------------------------
//tabbar item related
//-----------------------------------------------------------------------

void wintabitem_creat(wintab *wintab, Config *cfg)
{
    wintab->items[0].send_raw_mouse = 0;
    wintab->items[0].wheel_accumulator = 0;
    wintab->items[0].busy_status = BUSY_NOT;
    wintab->items[0].compose_state = 0;
    wintab->items[0].wm_mousewheel = WM_MOUSEWHEEL;
    
    wintabpage_init(&wintab->items[0].page, cfg, wintab->hwndTab);
    
    adjust_host(cfg);
    wintab->items[0].cfg = *cfg;
    wintabitem_cfgtopalette(&wintab->items[0]);

    
    memset(&wintab->items[0].ucsdata, 0, sizeof(wintab->items[0].ucsdata));
    wintab->items[0].term = term_init(&wintab->items[0].cfg, &wintab->items[0].ucsdata, &wintab->items[0]);
    wintab->items[0].logctx = log_init(NULL, &wintab->items[0].cfg);
    term_provide_logctx(wintab->items[0].term, wintab->items[0].logctx);
    term_size(wintab->items[0].term, wintab->items[0].cfg.height, 
        wintab->items[0].cfg.width, wintab->items[0].cfg.savelines);   
    wintabitem_init_fonts(&wintab->items[0], 0, 0);

    wintabitem_CreateCaret(&wintab->items[0]);
    wintabpage_init_scrollbar(&wintab->items[0].page, wintab->items[0].term);
    wintabitem_init_mouse(&wintab->items[0]);
    if (wintabitem_start_backend(&wintab->items[0]) != 0){
        //todo
        return;
    }

    ShowWindow(wintab->items[0].page.hwndCtrl, SW_SHOW);
    SetForegroundWindow(wintab->items[0].page.hwndCtrl);

    wintab->items[0].pal = NULL;
    wintab->items[0].logpal = NULL;
    wintabitem_init_palette(&wintab->items[0]);
    term_set_focus(wintab->items[0].term, TRUE);
    UpdateWindow(wintab->items[0].page.hwndCtrl);
    
    TCITEM tie; 
    tie.mask = TCIF_TEXT | TCIF_IMAGE; 
    tie.iImage = -1; 
    tie.pszText = cfg->session_name; 
    if (TabCtrl_InsertItem(wintab->hwndTab, 0, &tie) == -1) { 
        //todo;
        return ; 
    } 
}

//-----------------------------------------------------------------------

void wintabitem_delete(wintab *wintab, Config *cfg)
{
// free term
// free log
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

void wintabitem_close_session(wintabitem *tabitem)
{
    //char morestuff[100];
    //int i;

    tabitem->session_closed = TRUE;
    //sprintf(morestuff, "%.70s (inactive)", appname);
    //set_icon(NULL, morestuff);
    //set_title(NULL, morestuff);

    if (tabitem->ldisc) {
    	ldisc_free(tabitem->ldisc);
    	tabitem->ldisc = NULL;
    }
    if (tabitem->back) {
    	tabitem->back->free(tabitem->backhandle);
    	tabitem->backhandle = NULL;
    	tabitem->back = NULL;
        term_provide_resize_fn(tabitem->term, NULL, NULL);
	//update_specials_menu(NULL);
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
}
//-----------------------------------------------------------------------
//page related
//-----------------------------------------------------------------------
int wintabpage_init(wintabpage *page, const Config *cfg, HWND hwndParent)
{
    wintabpage_register();

    int winmode = WS_CHILD | WS_VSCROLL | WS_VISIBLE;
	if (!cfg->scrollbar)
	    winmode &= ~(WS_VSCROLL);
	page->hwndCtrl = CreateWindowEx(
                    WS_EX_TOPMOST, 
                    WINTAB_PAGE_CLASS, 
                    WINTAB_PAGE_CLASS,
			        winmode, 
			        0, 0, 100, 100,
			        hwndParent, 
			        NULL,   /* hMenu */
			        hinst, 
			        NULL);  /* lpParam */

    if (page->hwndCtrl == NULL){
        MessageBox(NULL, "Can't create the page!", "Error",MB_OK|MB_ICONINFORMATION);
        ExitProcess(2); 
    }
    
    return 0;

}

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

int wintabpage_fini(wintabpage *page)
{
    return 0;
}

int wintabpage_resize(wintabpage *page, const RECT *rc)
{
    HDWP hdwp;
 
    MoveWindow(page->hwndCtrl, rc->left, rc->top, rc->right, rc->bottom, TRUE);
    
    hdwp = BeginDeferWindowPos(1); 
    DeferWindowPos(hdwp, page->hwndCtrl, HWND_TOPMOST, rc->left, rc->top,
        rc->right - rc->left, rc->bottom - rc->top, SWP_NOMOVE);   
    EndDeferWindowPos(hdwp); 
    return 0;
}

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

int wintabpage_unregister()
{
    return UnregisterClass(WINTAB_PAGE_CLASS, hinst);
}





static void show_mouseptr(int show)
{
    /* NB that the counter in ShowCursor() is also frobbed by
     * update_mouse_pointer() */
    static int cursor_visible = 1;
    if (!cfg.hide_mouseptr)	       /* override if this feature disabled */
	show = 1;
    if (cursor_visible && !show)
	ShowCursor(FALSE);
    else if (!cursor_visible && show)
	ShowCursor(TRUE);
    cursor_visible = show;
}
/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
static Mouse_Button translate_button(Mouse_Button button)
{
    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return cfg.mouse_is_xterm == 1 ? MBT_PASTE : MBT_EXTEND;
    if (button == MBT_RIGHT)
	return cfg.mouse_is_xterm == 1 ? MBT_EXTEND : MBT_PASTE;
    return 0;			       /* shouldn't happen */
}
static void click(Mouse_Button b, int x, int y, int shift, int ctrl, int alt)
{
    int thistime = GetMessageTime();

    if (send_raw_mouse && !(cfg.mouse_override && shift)) {
    	lastbtn = MBT_NOTHING;
    	term_mouse(term, b, translate_button(b), MA_CLICK,
    		   x, y, shift, ctrl, alt);
    	return;
    }

    if (lastbtn == b && thistime - lasttime < dbltime) {
	lastact = (lastact == MA_CLICK ? MA_2CLK :
		   lastact == MA_2CLK ? MA_3CLK :
		   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
	lastbtn = b;
	lastact = MA_CLICK;
    }
    if (lastact != MA_NOTHING)
	term_mouse(term, b, translate_button(b), lastact,
		   x, y, shift, ctrl, alt);
    lasttime = thistime;
}

static int is_alt_pressed(void)
{
    BYTE keystate[256];
    int r = GetKeyboardState(keystate);
    if (!r)
	return FALSE;
    if (keystate[VK_MENU] & 0x80)
	return TRUE;
    if (keystate[VK_RMENU] & 0x80)
	return TRUE;
    return FALSE;
}

static int resizing;




LRESULT CALLBACK WintabpageWndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
#if 0
    switch (message) {
        case WM_MOVE:
            MessageBox(NULL, "WM_MOVE", TEXT("Error"), MB_OK); 
    }
    HDC hdc;
    static int ignore_clip = FALSE;
    static int need_backend_resize = FALSE;
    static int fullscr_on_max = FALSE;
    static int processed_resize = FALSE;
    static UINT last_mousemove = 0;

    switch (message) {
 #if 0       
        case WM_TIMER:
        	if ((UINT_PTR)wParam == TIMING_TIMER_ID) {
        	    long next;

        	    KillTimer(hwnd, TIMING_TIMER_ID);
        	    if (run_timers(timing_next_time, &next)) {
            		//timer_change_notify(next);
        	    } else {
        	    }
        	}
        	return 0;
        case WM_CREATE:
            break;
        case WM_CLOSE:
        	return 0;
        case WM_DESTROY:
        	return 0;

        case WM_COMMAND:
        case WM_SYSCOMMAND:
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
	  case IDM_SHOWLOG:
	    showeventlog(hwnd);
	    break;
	  case IDM_NEWSESS:
	  case IDM_DUPSESS:
	  case IDM_SAVEDSESS:
	    {
		char b[2048];
		char c[30], *cl;
		int freecl = FALSE;
		BOOL inherit_handles;
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		HANDLE filemap = NULL;

		if (wParam == IDM_DUPSESS) {
		    /*
		     * Allocate a file-mapping memory chunk for the
		     * config structure.
		     */
		    SECURITY_ATTRIBUTES sa;
		    Config *p;

		    sa.nLength = sizeof(sa);
		    sa.lpSecurityDescriptor = NULL;
		    sa.bInheritHandle = TRUE;
		    filemap = CreateFileMapping(INVALID_HANDLE_VALUE,
						&sa,
						PAGE_READWRITE,
						0, sizeof(Config), NULL);
		    if (filemap && filemap != INVALID_HANDLE_VALUE) {
			p = (Config *) MapViewOfFile(filemap,
						     FILE_MAP_WRITE,
						     0, 0, sizeof(Config));
			if (p) {
			    *p = cfg;  /* structure copy */
			    UnmapViewOfFile(p);
			}
		    }
		    inherit_handles = TRUE;
		    sprintf(c, "putty &%p", filemap);
		    cl = c;
		} else if (wParam == IDM_SAVEDSESS) {
		    unsigned int sessno = ((lParam - IDM_SAVED_MIN)
					   / MENU_SAVED_STEP) + 1;
		    if (sessno < (unsigned)sesslist.nsessions) {
			char *session = sesslist.sessions[sessno];
			cl = dupprintf("putty @%s", session);
			inherit_handles = FALSE;
			freecl = TRUE;
		    } else
			break;
		} else /* IDM_NEWSESS */ {
		    cl = NULL;
		    inherit_handles = FALSE;
		}

		GetModuleFileName(NULL, b, sizeof(b) - 1);
		si.cb = sizeof(si);
		si.lpReserved = NULL;
		si.lpDesktop = NULL;
		si.lpTitle = NULL;
		si.dwFlags = 0;
		si.cbReserved2 = 0;
		si.lpReserved2 = NULL;
		CreateProcess(b, cl, NULL, NULL, inherit_handles,
			      NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);

		if (filemap)
		    CloseHandle(filemap);
		if (freecl)
		    sfree(cl);
	    }
	    break;
	  case IDM_RESTART:
	    if (!back) {
		logevent(NULL, "----- Session restarted -----");
		term_pwron(term, FALSE);
		start_backend();
	    }

	    break;
	  case IDM_RECONF:
	    {
		Config prev_cfg;
		int init_lvl = 1;
		int reconfig_result;

		if (reconfiguring)
		    break;
		else
		    reconfiguring = TRUE;

		GetWindowText(hwnd, cfg.wintitle, sizeof(cfg.wintitle));
		prev_cfg = cfg;

		reconfig_result =
		    do_reconfig(hwnd, back ? back->cfg_info(backhandle) : 0);
		reconfiguring = FALSE;
		if (!reconfig_result)
		    break;

		{
		    /* Disable full-screen if resizing forbidden */
		    int i;
		    for (i = 0; i < lenof(popup_menus); i++)
			EnableMenuItem(popup_menus[i].menu, IDM_FULLSCREEN,
				       MF_BYCOMMAND | 
				       (cfg.resize_action == RESIZE_DISABLED)
				       ? MF_GRAYED : MF_ENABLED);
		    /* Gracefully unzoom if necessary */
		    if (IsZoomed(hwnd) &&
			(cfg.resize_action == RESIZE_DISABLED)) {
			ShowWindow(hwnd, SW_RESTORE);
		    }
		}

		/* Pass new config data to the logging module */
		log_reconfig(logctx, &cfg);

		sfree(logpal);
		/*
		 * Flush the line discipline's edit buffer in the
		 * case where local editing has just been disabled.
		 */
		if (ldisc)
		    ldisc_send(ldisc, NULL, 0, 0);
		if (pal)
		    DeleteObject(pal);
		logpal = NULL;
		pal = NULL;
		cfgtopalette();
		init_palette();

		/* Pass new config data to the terminal */
		term_reconfig(term, &cfg);

		/* Pass new config data to the back end */
		if (back)
		    back->reconfig(backhandle, &cfg);

		/* Screen size changed ? */
		if (cfg.height != prev_cfg.height ||
		    cfg.width != prev_cfg.width ||
		    cfg.savelines != prev_cfg.savelines ||
		    cfg.resize_action == RESIZE_FONT ||
		    (cfg.resize_action == RESIZE_EITHER && IsZoomed(hwnd)) ||
		    cfg.resize_action == RESIZE_DISABLED)
		    term_size(term, cfg.height, cfg.width, cfg.savelines);

		/* Enable or disable the scroll bar, etc */
		{
		    LONG nflg, flag = GetWindowLongPtr(hwnd, GWL_STYLE);
		    LONG nexflag, exflag =
			GetWindowLongPtr(hwnd, GWL_EXSTYLE);

		    nexflag = exflag;
		    if (cfg.alwaysontop != prev_cfg.alwaysontop) {
			if (cfg.alwaysontop) {
			    nexflag |= WS_EX_TOPMOST;
			    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE);
			} else {
			    nexflag &= ~(WS_EX_TOPMOST);
			    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE);
			}
		    }
		    if (cfg.sunken_edge)
			nexflag |= WS_EX_CLIENTEDGE;
		    else
			nexflag &= ~(WS_EX_CLIENTEDGE);

		    nflg = flag;
		    if (is_full_screen() ?
			cfg.scrollbar_in_fullscreen : cfg.scrollbar)
			nflg |= WS_VSCROLL;
		    else
			nflg &= ~WS_VSCROLL;

		    if (cfg.resize_action == RESIZE_DISABLED ||
                        is_full_screen())
			nflg &= ~WS_THICKFRAME;
		    else
			nflg |= WS_THICKFRAME;

		    if (cfg.resize_action == RESIZE_DISABLED)
			nflg &= ~WS_MAXIMIZEBOX;
		    else
			nflg |= WS_MAXIMIZEBOX;

		    if (nflg != flag || nexflag != exflag) {
			if (nflg != flag)
			    SetWindowLongPtr(hwnd, GWL_STYLE, nflg);
			if (nexflag != exflag)
			    SetWindowLongPtr(hwnd, GWL_EXSTYLE, nexflag);

			SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
				     SWP_NOACTIVATE | SWP_NOCOPYBITS |
				     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
				     SWP_FRAMECHANGED);

			init_lvl = 2;
		    }
		}

		/* Oops */
		if (cfg.resize_action == RESIZE_DISABLED && IsZoomed(hwnd)) {
		    force_normal(hwnd);
		    init_lvl = 2;
		}

		set_title(NULL, cfg.wintitle);
		if (IsIconic(hwnd)) {
		    SetWindowText(hwnd,
				  cfg.win_name_always ? window_name :
				  icon_name);
		}

		if (strcmp(cfg.font.name, prev_cfg.font.name) != 0 ||
		    strcmp(cfg.line_codepage, prev_cfg.line_codepage) != 0 ||
		    cfg.font.isbold != prev_cfg.font.isbold ||
		    cfg.font.height != prev_cfg.font.height ||
		    cfg.font.charset != prev_cfg.font.charset ||
		    cfg.font_quality != prev_cfg.font_quality ||
		    cfg.vtmode != prev_cfg.vtmode ||
		    cfg.bold_colour != prev_cfg.bold_colour ||
		    cfg.resize_action == RESIZE_DISABLED ||
		    cfg.resize_action == RESIZE_EITHER ||
		    (cfg.resize_action != prev_cfg.resize_action))
		    init_lvl = 2;

		InvalidateRect(hwnd, NULL, TRUE);
		reset_window(init_lvl);
		net_pending_errors();
	    }
	    break;
	  case IDM_COPYALL:
	    term_copyall(term);
	    break;
	  case IDM_PASTE:
	    request_paste(NULL);
	    break;
	  case IDM_CLRSB:
	    term_clrsb(term);
	    break;
	  case IDM_RESET:
	    term_pwron(term, TRUE);
	    if (ldisc)
		ldisc_send(ldisc, NULL, 0, 0);
	    break;
	  case IDM_ABOUT:
	    showabout(hwnd);
	    break;
	  case IDM_HELP:
	    launch_help(hwnd, NULL);
	    break;
	  case SC_MOUSEMENU:
	    /*
	     * We get this if the System menu has been activated
	     * using the mouse.
	     */
	    show_mouseptr(1);
	    break;
          case SC_KEYMENU:
	    /*
	     * We get this if the System menu has been activated
	     * using the keyboard. This might happen from within
	     * TranslateKey, in which case it really wants to be
	     * followed by a `space' character to actually _bring
	     * the menu up_ rather than just sitting there in
	     * `ready to appear' state.
	     */
	    show_mouseptr(1);	       /* make sure pointer is visible */
	    if( lParam == 0 )
		PostMessage(hwnd, WM_CHAR, ' ', 0);
	    break;
	  case IDM_FULLSCREEN:
	    flip_full_screen();
	    break;
	  default:
	    if (wParam >= IDM_SAVED_MIN && wParam < IDM_SAVED_MAX) {
		SendMessage(hwnd, WM_SYSCOMMAND, IDM_SAVEDSESS, wParam);
	    }
	    if (wParam >= IDM_SPECIAL_MIN && wParam <= IDM_SPECIAL_MAX) {
		int i = (wParam - IDM_SPECIAL_MIN) / 0x10;
		/*
		 * Ensure we haven't been sent a bogus SYSCOMMAND
		 * which would cause us to reference invalid memory
		 * and crash. Perhaps I'm just too paranoid here.
		 */
		if (i >= n_specials)
		    break;
		if (back)
		    back->special(backhandle, specials[i].code);
		net_pending_errors();
	    }
	}
	break;
#endif
#if 0
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_MBUTTONUP:
      case WM_RBUTTONUP:

	if (message == WM_RBUTTONDOWN &&
	    ((wParam & MK_CONTROL) || (cfg.mouse_is_xterm == 2))) {
	    POINT cursorpos;

	    show_mouseptr(1);	       /* make sure pointer is visible */
	    GetCursorPos(&cursorpos);
	    TrackPopupMenu(popup_menus[CTXMENU].menu,
			   TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			   cursorpos.x, cursorpos.y,
			   0, hwnd, NULL);
	    break;
	}

	{
	    int button, press;

	    switch (message) {
	      case WM_LBUTTONDOWN:
		button = MBT_LEFT;
		wParam |= MK_LBUTTON;
		press = 1;
		break;
	      case WM_MBUTTONDOWN:
		button = MBT_MIDDLE;
		wParam |= MK_MBUTTON;
		press = 1;
		break;
	      case WM_RBUTTONDOWN:
		button = MBT_RIGHT;
		wParam |= MK_RBUTTON;
		press = 1;
		break;
	      case WM_LBUTTONUP:
		button = MBT_LEFT;
		wParam &= ~MK_LBUTTON;
		press = 0;
		break;
	      case WM_MBUTTONUP:
		button = MBT_MIDDLE;
		wParam &= ~MK_MBUTTON;
		press = 0;
		break;
	      case WM_RBUTTONUP:
		button = MBT_RIGHT;
		wParam &= ~MK_RBUTTON;
		press = 0;
		break;
	      default:
		button = press = 0;    /* shouldn't happen */
	    }
	    show_mouseptr(1);
	    /*
	     * Special case: in full-screen mode, if the left
	     * button is clicked in the very top left corner of the
	     * window, we put up the System menu instead of doing
	     * selection.
	     */
	    {
		char mouse_on_hotspot = 0;
		POINT pt;

		GetCursorPos(&pt);
#ifndef NO_MULTIMON
		{
		    HMONITOR mon;
		    MONITORINFO mi;

		    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);

		    if (mon != NULL) {
			mi.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(mon, &mi);

			if (mi.rcMonitor.left == pt.x &&
			    mi.rcMonitor.top == pt.y) {
			    mouse_on_hotspot = 1;
			}
		    }
		}
#else
		if (pt.x == 0 && pt.y == 0) {
		    mouse_on_hotspot = 1;
		}
#endif

#if 0
		if (is_full_screen() && press &&
		    button == MBT_LEFT && mouse_on_hotspot) {
		    SendMessage(hwnd, WM_SYSCOMMAND, SC_MOUSEMENU,
				MAKELPARAM(pt.x, pt.y));
		    return 0;
		}
#endif
	    }

	    if (press) {
		click(button,
		      TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)),
		      wParam & MK_SHIFT, wParam & MK_CONTROL,
		      is_alt_pressed());
		SetCapture(hwnd);
	    } else {
		term_mouse(term, button, translate_button(button), MA_RELEASE,
			   TO_CHR_X(X_POS(lParam)),
			   TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
			   wParam & MK_CONTROL, is_alt_pressed());
		if (!(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
		    ReleaseCapture();
	    }
	}
	return 0;
      case WM_MOUSEMOVE:
	{
	    /*
	     * Windows seems to like to occasionally send MOUSEMOVE
	     * events even if the mouse hasn't moved. Don't unhide
	     * the mouse pointer in this case.
	     */
	    static WPARAM wp = 0;
	    static LPARAM lp = 0;
	    if (wParam != wp || lParam != lp ||
		last_mousemove != WM_MOUSEMOVE) {
		show_mouseptr(1);
		wp = wParam; lp = lParam;
		last_mousemove = WM_MOUSEMOVE;
	    }
	}
	/*
	 * Add the mouse position and message time to the random
	 * number noise.
	 */
	noise_ultralight(lParam);

	if (wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON) &&
	    GetCapture() == hwnd) {
	    Mouse_Button b;
	    if (wParam & MK_LBUTTON)
		b = MBT_LEFT;
	    else if (wParam & MK_MBUTTON)
		b = MBT_MIDDLE;
	    else
		b = MBT_RIGHT;
	    term_mouse(term, b, translate_button(b), MA_DRAG,
		       TO_CHR_X(X_POS(lParam)),
		       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
		       wParam & MK_CONTROL, is_alt_pressed());
	}
	return 0;
      case WM_NCMOUSEMOVE:
	{
	    static WPARAM wp = 0;
	    static LPARAM lp = 0;
	    if (wParam != wp || lParam != lp ||
		last_mousemove != WM_NCMOUSEMOVE) {
		show_mouseptr(1);
		wp = wParam; lp = lParam;
		last_mousemove = WM_NCMOUSEMOVE;
	    }
	}
	noise_ultralight(lParam);
	break;
      case WM_IGNORE_CLIP:
	ignore_clip = wParam;	       /* don't panic on DESTROYCLIPBOARD */
	break;
      case WM_DESTROYCLIPBOARD:
	if (!ignore_clip)
	    term_deselect(term);
	ignore_clip = FALSE;
	return 0;
#endif 
        case WM_PAINT:
    	{
    	    PAINTSTRUCT p;

    	    HideCaret(hwnd);
    	    hdc = BeginPaint(hwnd, &p);
    	    if (pal) {
        		SelectPalette(hdc, pal, TRUE);
        		RealizePalette(hdc);
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
	    term_paint(term, hdc, 
		       (p.rcPaint.left-offset_width)/font_width,
		       (p.rcPaint.top-offset_height)/font_height,
		       (p.rcPaint.right-offset_width-1)/font_width,
		       (p.rcPaint.bottom-offset_height-1)/font_height,
		       !term->window_update_pending);

	    if (p.fErase ||
	        p.rcPaint.left  < offset_width  ||
		p.rcPaint.top   < offset_height ||
		p.rcPaint.right >= offset_width + font_width*term->cols ||
		p.rcPaint.bottom>= offset_height + font_height*term->rows)
	    {
		HBRUSH fillcolour, oldbrush;
		HPEN   edge, oldpen;
		fillcolour = CreateSolidBrush (
				    colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
		oldbrush = SelectObject(hdc, fillcolour);
		edge = CreatePen(PS_SOLID, 0, 
				    colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
		oldpen = SelectObject(hdc, edge);

		/*
		 * Jordan Russell reports that this apparently
		 * ineffectual IntersectClipRect() call masks a
		 * Windows NT/2K bug causing strange display
		 * problems when the PuTTY window is taller than
		 * the primary monitor. It seems harmless enough...
		 */
		IntersectClipRect(hdc,
			p.rcPaint.left, p.rcPaint.top,
			p.rcPaint.right, p.rcPaint.bottom);

		ExcludeClipRect(hdc, 
			offset_width, offset_height,
			offset_width+font_width*term->cols,
			offset_height+font_height*term->rows);

		Rectangle(hdc, p.rcPaint.left, p.rcPaint.top, 
			  p.rcPaint.right, p.rcPaint.bottom);

		/* SelectClipRgn(hdc, NULL); */

		SelectObject(hdc, oldbrush);
		DeleteObject(fillcolour);
		SelectObject(hdc, oldpen);
		DeleteObject(edge);
	    }
	    SelectObject(hdc, GetStockObject(SYSTEM_FONT));
	    SelectObject(hdc, GetStockObject(WHITE_PEN));
	    EndPaint(hwnd, &p);
	    ShowCaret(hwnd);
	}
	return 0;
#if 0
      case WM_NETEVENT:
	/* Notice we can get multiple netevents, FD_READ, FD_WRITE etc
	 * but the only one that's likely to try to overload us is FD_READ.
	 * This means buffering just one is fine.
	 */
	if (pending_netevent)
	    enact_pending_netevent();

	pending_netevent = TRUE;
	pend_netevent_wParam = wParam;
	pend_netevent_lParam = lParam;
	if (WSAGETSELECTEVENT(lParam) != FD_READ)
	    enact_pending_netevent();

	net_pending_errors();
	return 0;

      case WM_SETFOCUS:
	term_set_focus(term, TRUE);
	CreateCaret(hwnd, caretbm, font_width, font_height);
	ShowCaret(hwnd);
	flash_window(0);	       /* stop */
	compose_state = 0;
	term_update(term);
	break;
#endif    
        case WM_KILLFOCUS:
            show_mouseptr(1);
            term_set_focus(term, FALSE);
            DestroyCaret();
            caret_x = caret_y = -1;	       /* ensure caret is replaced next time */
            term_update(term);
            break;
        case WM_ENTERSIZEMOVE:
            EnableSizeTip(1);
            resizing = TRUE;
            need_backend_resize = FALSE;
            break;
        case WM_EXITSIZEMOVE:
            EnableSizeTip(0);
            resizing = FALSE;

            if (need_backend_resize) {
                term_size(term, cfg.height, cfg.width, cfg.savelines);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        case WM_SIZING:
        	/*
        	 * This does two jobs:
        	 * 1) Keep the sizetip uptodate
        	 * 2) Make sure the window size is _stepped_ in units of the font size.
        	 */
            if (cfg.resize_action == RESIZE_TERM ||
                    (cfg.resize_action == RESIZE_EITHER && !is_alt_pressed())) {
                int width, height, w, h, ew, eh;
                LPRECT r = (LPRECT) lParam;

                if ( !need_backend_resize && cfg.resize_action == RESIZE_EITHER &&
                (cfg.height != term->rows || cfg.width != term->cols )) {
                    /* 
                    * Great! It seems that both the terminal size and the
                    * font size have been changed and the user is now dragging.
                    * 
                    * It will now be difficult to get back to the configured
                    * font size!
                    *
                    * This would be easier but it seems to be too confusing.

                    term_size(term, cfg.height, cfg.width, cfg.savelines);
                    reset_window(2);
                    */
                    cfg.height=term->rows; cfg.width=term->cols;

                    InvalidateRect(hwnd, NULL, TRUE);
                    need_backend_resize = TRUE;
                }

                width = r->right - r->left - extra_width;
                height = r->bottom - r->top - extra_height;
                w = (width + font_width / 2) / font_width;
                if (w < 1)
                    w = 1;
                h = (height + font_height / 2) / font_height;
                if (h < 1)
                    h = 1;
                UpdateSizeTip(hwnd, w, h);
                ew = width - w * font_width;
                eh = height - h * font_height;
                if (ew != 0) {
                    if (wParam == WMSZ_LEFT ||
                        wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                        r->left += ew;
                    else
                        r->right -= ew;
                }
                if (eh != 0) {
                    if (wParam == WMSZ_TOP ||
                        wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
                        r->top += eh;
                    else
                        r->bottom -= eh;
                }
                if (ew || eh)
                    return 1;
                else
                    return 0;
            } else {
                int width, height, w, h, rv = 0;
                int ex_width = extra_width + (cfg.window_border - offset_width) * 2;
                int ex_height = extra_height + (cfg.window_border - offset_height) * 2;
                LPRECT r = (LPRECT) lParam;

                width = r->right - r->left - ex_width;
                height = r->bottom - r->top - ex_height;

                w = (width + term->cols/2)/term->cols;
                h = (height + term->rows/2)/term->rows;
                if ( r->right != r->left + w*term->cols + ex_width)
                    rv = 1;

                if (wParam == WMSZ_LEFT ||
                        wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                    r->left = r->right - w*term->cols - ex_width;
                else
                    r->right = r->left + w*term->cols + ex_width;

                if (r->bottom != r->top + h*term->rows + ex_height)
                rv = 1;

                if (wParam == WMSZ_TOP ||
                    wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
                    r->top = r->bottom - h*term->rows - ex_height;
                else
                    r->bottom = r->top + h*term->rows + ex_height;

                return rv;
            }
                /* break;  (never reached) */

        case WM_CHAR:
        case WM_SYSCHAR:
            /*
             * Nevertheless, we are prepared to deal with WM_CHAR
             * messages, should they crop up. So if someone wants to
             * post the things to us as part of a macro manoeuvre,
             * we're ready to cope.
             */
            {
                char c = (unsigned char)wParam;
                term_seen_key_event(term);
                if (ldisc)
            	lpage_send(ldisc, CP_ACP, &c, 1, 1);
            }
            return 0;
#if 0
        case WM_FULLSCR_ON_MAX:
            fullscr_on_max = TRUE;
            break;
        case WM_MOVE:
        	sys_cursor_update();
        	break;

        case WM_SIZE:
	if (wParam == SIZE_MINIMIZED)
	    SetWindowText(hwnd,
			  cfg.win_name_always ? window_name : icon_name);
	if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
	    SetWindowText(hwnd, window_name);
        if (wParam == SIZE_RESTORED) {
            processed_resize = FALSE;
            clear_full_screen();
            if (processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * clear_full_screen which contained the correct
                 * client area size.
                 */
                return 0;
            }
        }
        if (wParam == SIZE_MAXIMIZED && fullscr_on_max) {
            fullscr_on_max = FALSE;
            processed_resize = FALSE;
            make_full_screen();
            if (processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * make_full_screen which contained the correct client
                 * area size.
                 */
                return 0;
            }
        }

        processed_resize = TRUE;

	if (cfg.resize_action == RESIZE_DISABLED) {
	    /* A resize, well it better be a minimize. */
	    reset_window(-1);
	} else {

	    int width, height, w, h;

	    width = LOWORD(lParam);
	    height = HIWORD(lParam);

            if (wParam == SIZE_MAXIMIZED && !was_zoomed) {
                was_zoomed = 1;
                prev_rows = term->rows;
                prev_cols = term->cols;
                if (cfg.resize_action == RESIZE_TERM) {
                    w = width / font_width;
                    if (w < 1) w = 1;
                    h = height / font_height;
                    if (h < 1) h = 1;

                    term_size(term, h, w, cfg.savelines);
                }
                reset_window(0);
            } else if (wParam == SIZE_RESTORED && was_zoomed) {
                was_zoomed = 0;
                if (cfg.resize_action == RESIZE_TERM) {
                    w = (width-cfg.window_border*2) / font_width;
                    if (w < 1) w = 1;
                    h = (height-cfg.window_border*2) / font_height;
                    if (h < 1) h = 1;
                    term_size(term, h, w, cfg.savelines);
                    reset_window(2);
                } else if (cfg.resize_action != RESIZE_FONT)
                    reset_window(2);
                else
                    reset_window(0);
            } else if (wParam == SIZE_MINIMIZED) {
                /* do nothing */
	    } else if (cfg.resize_action == RESIZE_TERM ||
                       (cfg.resize_action == RESIZE_EITHER &&
                        !is_alt_pressed())) {
                w = (width-cfg.window_border*2) / font_width;
                if (w < 1) w = 1;
                h = (height-cfg.window_border*2) / font_height;
                if (h < 1) h = 1;

                if (resizing) {
                    /*
                     * Don't call back->size in mid-resize. (To
                     * prevent massive numbers of resize events
                     * getting sent down the connection during an NT
                     * opaque drag.)
                     */
		    need_backend_resize = TRUE;
		    cfg.height = h;
		    cfg.width = w;
                } else {
                    term_size(term, h, w, cfg.savelines);
                }
            } else {
                reset_window(0);
	    }
	}
	sys_cursor_update();
	return 0;
      case WM_VSCROLL:
	switch (LOWORD(wParam)) {
	  case SB_BOTTOM:
	    term_scroll(term, -1, 0);
	    break;
	  case SB_TOP:
	    term_scroll(term, +1, 0);
	    break;
	  case SB_LINEDOWN:
	    term_scroll(term, 0, +1);
	    break;
	  case SB_LINEUP:
	    term_scroll(term, 0, -1);
	    break;
	  case SB_PAGEDOWN:
	    term_scroll(term, 0, +term->rows / 2);
	    break;
	  case SB_PAGEUP:
	    term_scroll(term, 0, -term->rows / 2);
	    break;
	  case SB_THUMBPOSITION:
	  case SB_THUMBTRACK:
	    term_scroll(term, 1, HIWORD(wParam));
	    break;
	}
	break;
      case WM_PALETTECHANGED:
	if ((HWND) wParam != hwnd && pal != NULL) {
	    HDC hdc = get_ctx(NULL);
	    if (hdc) {
		if (RealizePalette(hdc) > 0)
		    UpdateColors(hdc);
		free_ctx(hdc);
	    }
	}
	break;
      case WM_QUERYNEWPALETTE:
	if (pal != NULL) {
	    HDC hdc = get_ctx(NULL);
	    if (hdc) {
		if (RealizePalette(hdc) > 0)
		    UpdateColors(hdc);
		free_ctx(hdc);
		return TRUE;
	    }
	}
	return FALSE;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP:
	/*
	 * Add the scan code and keypress timing to the random
	 * number noise.
	 */
	noise_ultralight(lParam);

	/*
	 * We don't do TranslateMessage since it disassociates the
	 * resulting CHAR message from the KEYDOWN that sparked it,
	 * which we occasionally don't want. Instead, we process
	 * KEYDOWN, and call the Win32 translator functions so that
	 * we get the translations under _our_ control.
	 */
	{
	    unsigned char buf[20];
	    int len;

	    if (wParam == VK_PROCESSKEY) { /* IME PROCESS key */
		if (message == WM_KEYDOWN) {
		    MSG m;
		    m.hwnd = hwnd;
		    m.message = WM_KEYDOWN;
		    m.wParam = wParam;
		    m.lParam = lParam & 0xdfff;
		    TranslateMessage(&m);
		} else break; /* pass to Windows for default processing */
	    } else {
		len = TranslateKey(message, wParam, lParam, buf);
		if (len == -1)
		    return DefWindowProc(hwnd, message, wParam, lParam);

		if (len != 0) {
		    /*
		     * Interrupt an ongoing paste. I'm not sure
		     * this is sensible, but for the moment it's
		     * preferable to having to faff about buffering
		     * things.
		     */
		    term_nopaste(term);

		    /*
		     * We need not bother about stdin backlogs
		     * here, because in GUI PuTTY we can't do
		     * anything about it anyway; there's no means
		     * of asking Windows to hold off on KEYDOWN
		     * messages. We _have_ to buffer everything
		     * we're sent.
		     */
		    term_seen_key_event(term);
		    if (ldisc)
			ldisc_send(ldisc, buf, len, 1);
		    show_mouseptr(0);
		}
	    }
	}
	net_pending_errors();
	return 0;
      case WM_INPUTLANGCHANGE:
	/* wParam == Font number */
	/* lParam == Locale */
	set_input_locale((HKL)lParam);
	sys_cursor_update();
	break;
      case WM_IME_STARTCOMPOSITION:
	{
	    HIMC hImc = ImmGetContext(hwnd);
	    ImmSetCompositionFont(hImc, &lfont);
	    ImmReleaseContext(hwnd, hImc);
	}
	break;
      case WM_IME_COMPOSITION:
	{
	    HIMC hIMC;
	    int n;
	    char *buff;

	    if(osVersion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS || 
	        osVersion.dwPlatformId == VER_PLATFORM_WIN32s) break; /* no Unicode */

	    if ((lParam & GCS_RESULTSTR) == 0) /* Composition unfinished. */
		break; /* fall back to DefWindowProc */

	    hIMC = ImmGetContext(hwnd);
	    n = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, NULL, 0);

	    if (n > 0) {
		int i;
		buff = snewn(n, char);
		ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, buff, n);
		/*
		 * Jaeyoun Chung reports that Korean character
		 * input doesn't work correctly if we do a single
		 * luni_send() covering the whole of buff. So
		 * instead we luni_send the characters one by one.
		 */
		term_seen_key_event(term);
		for (i = 0; i < n; i += 2) {
		    if (ldisc)
			luni_send(ldisc, (unsigned short *)(buff+i), 1, 1);
		}
		free(buff);
	    }
	    ImmReleaseContext(hwnd, hIMC);
	    return 1;
	}

      case WM_IME_CHAR:
	if (wParam & 0xFF00) {
	    unsigned char buf[2];

	    buf[1] = wParam;
	    buf[0] = wParam >> 8;
	    term_seen_key_event(term);
	    if (ldisc)
		lpage_send(ldisc, kbd_codepage, buf, 2, 1);
	} else {
	    char c = (unsigned char) wParam;
	    term_seen_key_event(term);
	    if (ldisc)
		lpage_send(ldisc, kbd_codepage, &c, 1, 1);
	}
	return (0);

      case WM_SYSCOLORCHANGE:
	if (cfg.system_colour) {
	    /* Refresh palette from system colours. */
	    /* XXX actually this zaps the entire palette. */
	    systopalette();
	    init_palette();
	    /* Force a repaint of the terminal window. */
	    term_invalidate(term);
	}
	break;
      case WM_AGENT_CALLBACK:
	{
	    struct agent_callback *c = (struct agent_callback *)lParam;
	    c->callback(c->callback_ctx, c->data, c->len);
	    sfree(c);
	}
	return 0;
      case WM_GOT_CLIPDATA:
	if (process_clipdata((HGLOBAL)lParam, wParam))
	    term_do_paste(term);
	return 0;
      default:
	if (message == wm_mousewheel || message == WM_MOUSEWHEEL) {
	    int shift_pressed=0, control_pressed=0;

	    if (message == WM_MOUSEWHEEL) {
		wheel_accumulator += (short)HIWORD(wParam);
		shift_pressed=LOWORD(wParam) & MK_SHIFT;
		control_pressed=LOWORD(wParam) & MK_CONTROL;
	    } else {
		BYTE keys[256];
		wheel_accumulator += (int)wParam;
		if (GetKeyboardState(keys)!=0) {
		    shift_pressed=keys[VK_SHIFT]&0x80;
		    control_pressed=keys[VK_CONTROL]&0x80;
		}
	    }

	    /* process events when the threshold is reached */
	    while (abs(wheel_accumulator) >= WHEEL_DELTA) {
		int b;

		/* reduce amount for next time */
		if (wheel_accumulator > 0) {
		    b = MBT_WHEEL_UP;
		    wheel_accumulator -= WHEEL_DELTA;
		} else if (wheel_accumulator < 0) {
		    b = MBT_WHEEL_DOWN;
		    wheel_accumulator += WHEEL_DELTA;
		} else
		    break;

		if (send_raw_mouse &&
		    !(cfg.mouse_override && shift_pressed)) {
		    /* Mouse wheel position is in screen coordinates for
		     * some reason */
		    POINT p;
		    p.x = X_POS(lParam); p.y = Y_POS(lParam);
		    if (ScreenToClient(hwnd, &p)) {
			/* send a mouse-down followed by a mouse up */
			term_mouse(term, b, translate_button(b),
				   MA_CLICK,
				   TO_CHR_X(p.x),
				   TO_CHR_Y(p.y), shift_pressed,
				   control_pressed, is_alt_pressed());
			term_mouse(term, b, translate_button(b),
				   MA_RELEASE, TO_CHR_X(p.x),
				   TO_CHR_Y(p.y), shift_pressed,
				   control_pressed, is_alt_pressed());
		    } /* else: not sure when this can fail */
		} else {
		    /* trigger a scroll */
		    int scrollLines = cfg.scrolllines == -1 ? term->rows/2
		            : cfg.scrolllines == -2          ? term->rows
		            : cfg.scrolllines < -2            ? 3
		            : cfg.scrolllines;
		    term_scroll(term, 0,
				b == MBT_WHEEL_UP ?
				-scrollLines : scrollLines);
		}
	    }
	    return 0;
	}
#endif
    }

    /*
     * Any messages we don't process completely above are passed through to
     * DefWindowProc() for default processing.
     */
#endif
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
void ErrorExit() 
{ 
    LPVOID lpMsgBuf;
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

    MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK); 

    LocalFree(lpMsgBuf);
    ExitProcess(dw); 
}

//-----------------------------------------------------------------------
//end
//-----------------------------------------------------------------------

