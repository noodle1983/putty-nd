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

wintabitem* wintab_get_active_item(wintab *wintab)
{
    //get select ...
    return &wintab->items[0];
}

//-----------------------------------------------------------------------

int wintab_on_paint(wintab *wintab, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    wintabitem tabitem = wintab->items[0];
    PAINTSTRUCT p;

    HideCaret(tabitem.page.hwndCtrl);
    tabitem.hdc = BeginPaint(tabitem.page.hwndCtrl, &p);
    if (tabitem.pal) {
    	SelectPalette(tabitem.hdc, tabitem.pal, TRUE);
    	RealizePalette(tabitem.hdc);
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
    term_paint(tabitem.term, (Context)&tabitem, 
	       (p.rcPaint.left-tabitem.offset_width)/tabitem.font_width,
	       (p.rcPaint.top-tabitem.offset_height)/tabitem.font_height,
	       (p.rcPaint.right-tabitem.offset_width-1)/tabitem.font_width,
	       (p.rcPaint.bottom-tabitem.offset_height-1)/tabitem.font_height,
	       !tabitem.term->window_update_pending);

    if (p.fErase ||
        p.rcPaint.left  < tabitem.offset_width  ||
	p.rcPaint.top   < tabitem.offset_height ||
	p.rcPaint.right >= tabitem.offset_width + tabitem.font_width*tabitem.term->cols ||
	p.rcPaint.bottom>= tabitem.offset_height + tabitem.font_height*tabitem.term->rows)
    {
    	HBRUSH fillcolour, oldbrush;
    	HPEN   edge, oldpen;
    	fillcolour = CreateSolidBrush (
    			    tabitem.colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
    	oldbrush = SelectObject(tabitem.hdc, fillcolour);
    	edge = CreatePen(PS_SOLID, 0, 
    			    tabitem.colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
    	oldpen = SelectObject(tabitem.hdc, edge);

    	/*
    	 * Jordan Russell reports that this apparently
    	 * ineffectual IntersectClipRect() call masks a
    	 * Windows NT/2K bug causing strange display
    	 * problems when the PuTTY window is taller than
    	 * the primary monitor. It seems harmless enough...
    	 */
    	IntersectClipRect(tabitem.hdc,
    		p.rcPaint.left, p.rcPaint.top,
    		p.rcPaint.right, p.rcPaint.bottom);

    	ExcludeClipRect(tabitem.hdc, 
    		tabitem.offset_width, tabitem.offset_height,
    		tabitem.offset_width+tabitem.font_width*tabitem.term->cols,
    		tabitem.offset_height+tabitem.font_height*tabitem.term->rows);

    	Rectangle(tabitem.hdc, p.rcPaint.left, p.rcPaint.top, 
    		  p.rcPaint.right, p.rcPaint.bottom);

    	/* SelectClipRgn(hdc, NULL); */

    	SelectObject(tabitem.hdc, oldbrush);
    	DeleteObject(fillcolour);
    	SelectObject(tabitem.hdc, oldpen);
    	DeleteObject(edge);
    }
    SelectObject(tabitem.hdc, GetStockObject(SYSTEM_FONT));
    SelectObject(tabitem.hdc, GetStockObject(WHITE_PEN));
    EndPaint(tabitem.page.hwndCtrl, &p);
    ShowCaret(tabitem.page.hwndCtrl);

    return 0;
}
//-----------------------------------------------------------------------
//tabbar item related
//-----------------------------------------------------------------------

void wintabitem_creat(wintab *wintab, Config *cfg)
{
    wintab->items[0].hdc = NULL;
    wintab->items[0].send_raw_mouse = 0;
    wintab->items[0].wheel_accumulator = 0;
    wintab->items[0].busy_status = BUSY_NOT;
    wintab->items[0].compose_state = 0;
    wintab->items[0].wm_mousewheel = WM_MOUSEWHEEL;
    wintab->items[0].offset_width = wintab->items[0].offset_height = wintab->items[0].cfg.window_border;
    wintab->items[0].caret_x = -1; 
    wintab->items[0].caret_y = -1;
    wintab->items[0].n_specials = 0;
    wintab->items[0].specials = NULL;
    wintab->items[0].specials_menu = NULL;
    
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
//    SetForegroundWindow(wintab->items[0].page.hwndCtrl);

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
    return 0;
}

//-----------------------------------------------------------------------

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


LRESULT CALLBACK WintabpageWndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
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

