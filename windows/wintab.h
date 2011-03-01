/*
 * wintab.h define the tabbar and its page.
 * 
 */
#ifndef WINTAB_H
#define WINTAB_H


#define NCFGCOLOURS 22
#define NEXTCOLOURS 240
#define NALLCOLOURS (NCFGCOLOURS + NEXTCOLOURS)

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_UNDERLINE 2
#define FONT_BOLDUND 3
#define FONT_WIDE	0x04
#define FONT_HIGH	0x08
#define FONT_NARROW	0x10

#define FONT_OEM 	0x20
#define FONT_OEMBOLD 	0x21
#define FONT_OEMUND 	0x22
#define FONT_OEMBOLDUND 0x23

#define FONT_MAXNO 	0x2F
#define FONT_SHIFT	5
//-----------------------------------------------------------------------
// struct
//-----------------------------------------------------------------------
typedef enum {
    BOLD_COLOURS, BOLD_SHADOW, BOLD_FONT
} bold_mode_t;
typedef enum {
    UND_LINE, UND_FONT
} und_mode_t;


typedef struct {
    HWND hwndCtrl;
}wintabpage;

typedef struct {
    wintabpage page;

    HDC hdc;
    
    Config cfg;
    Terminal *term;
    void *logctx;
    RGBTRIPLE defpal[NALLCOLOURS];
    struct unicode_data ucsdata;

    HFONT fonts[FONT_MAXNO];
    LOGFONT lfont;
    int fontflag[FONT_MAXNO];
    bold_mode_t bold_mode;
    und_mode_t und_mode;
    int font_width, font_height, font_dualwidth, font_varpitch;
    int offset_width, offset_height;
    int caret_x, caret_y;
    int descent;

    HBITMAP caretbm;

    int dbltime, lasttime, lastact;
    Mouse_Button lastbtn;

    Backend *back;
    void *backhandle;
    void *ldisc;

    int must_close_session, session_closed;

    COLORREF colours[NALLCOLOURS];
    HPALETTE pal;
    LPLOGPALETTE logpal;

    int send_raw_mouse;
    int wheel_accumulator;
    int busy_status;
    int compose_state;
    UINT wm_mousewheel;

}wintabitem;

typedef struct {
    HWND hwndTab;
    HWND hwndParent;
    wintabitem items[64];
}wintab;

//-----------------------------------------------------------------------
// tabbar related
//-----------------------------------------------------------------------
int wintab_init(wintab *wintab, HWND hwndParent);
int wintab_fini(wintab *wintab);
int wintab_resize(wintab *wintab, const RECT *rc);
void wintab_onsize(wintab *wintab, HWND hwndParent, LPARAM lParam);
void wintab_check_closed_session(wintab *wintab);
void wintab_term_paste(wintab *wintab);
void wintab_term_set_focus(wintab *wintab, int has_focus);
wintabitem* wintab_get_active_item(wintab *wintab);

int wintab_on_paint(wintab *wintab, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
//-----------------------------------------------------------------------
// tabbar item related
//-----------------------------------------------------------------------
void wintabitem_creat(wintab *wintab, Config *cfg);
void wintabitem_cfgtopalette(wintabitem *tabitem);
void wintabitem_systopalette(wintabitem *tabitem);
void wintabitem_init_fonts(wintabitem *tabitem, const int pick_width, const int pick_height);
int wintabitem_get_font_width(wintabitem *tabitem, HDC hdc, const TEXTMETRIC *tm);
int wintabitem_CreateCaret(wintabitem *tabitem);
int wintabitem_init_mouse(wintabitem *tabitem);
int wintabitem_start_backend(wintabitem *tabitem);
void wintabitem_init_palette(wintabitem *tabitem);
void wintabitem_check_closed_session(wintabitem *tabitem);
void wintabitem_close_session(wintabitem *tabitem);
//-----------------------------------------------------------------------
//page related
//-----------------------------------------------------------------------
int wintabpage_init(wintabpage *page, const Config *cfg, HWND hwndParent);
void wintabpage_init_scrollbar(wintabpage *page, Terminal *term);
int wintabpage_fini(wintabpage *page);
int wintabpage_resize(wintabpage *page, const RECT *rc);

int wintabpage_register();
int wintabpage_unregister();
LRESULT CALLBACK WintabpageWndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
//-----------------------------------------------------------------------
//Config post handling
//-----------------------------------------------------------------------
void ltrim(char *str);
void takeout_username_from_host(Config *cfg);
void handle_host_colon(char *host);
void rm_whitespace(char *host);
void adjust_host(Config *cfg);

//-----------------------------------------------------------------------
//debug related
//-----------------------------------------------------------------------
void ErrorExit() ;

#endif
