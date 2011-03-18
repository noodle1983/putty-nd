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


#define WM_IGNORE_CLIP (WM_APP + 2)
#define WM_FULLSCR_ON_MAX (WM_APP + 3)
#define WM_AGENT_CALLBACK (WM_APP + 4)
#define WM_GOT_CLIPDATA (WM_APP + 6)
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
    HWND hwndTab;
    int extra_page_width, extra_page_height; //gaps from term to page
    int extra_width, extra_height; //gaps from page to tab
}wintabpage;

typedef struct {
    void *parentTab;
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
    int extra_width, extra_height;
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

    const struct telnet_special *specials;
    HMENU specials_menu;
    int n_specials;

    int prev_rows, prev_cols;

    int ignore_clip;
  
    HRGN hRgn, hCloserRgn;
}wintabitem;

typedef struct {
    HWND hwndTab;
    HWND hwndParent;
    wintabitem* items[64];
    int end, cur;

    int extra_width, extra_height; //gaps from win to tab

    COLORREF bg_col, sel_col, nosel_col, on_col, hl_col, bd_col; 
    HRGN hSysRgn[3];
    HWND hMinBtn;
    HWND hMaxBtn;
    HWND hClsBtn;

    LRESULT CALLBACK (*defWndProc)(HWND,UINT,WPARAM,LPARAM);
}wintab;

//-----------------------------------------------------------------------
// common
//-----------------------------------------------------------------------
void win_bind_data(HWND hwnd, void *data);
void* win_get_data(HWND hwnd);
//-----------------------------------------------------------------------
// tabbar related
//-----------------------------------------------------------------------
int wintab_init(wintab *wintab, HWND hwndParent);
int wintab_fini(wintab *wintab);
int wintab_create_tab(wintab *wintab, Config *cfg);
int wintab_swith_tab(wintab *wintab);
int wintab_resize(wintab *wintab, const RECT *rc);
void wintab_onsize(wintab *wintab, HWND hwndParent, LPARAM lParam);
int  wintab_can_close(wintab *wintab);
void wintab_check_closed_session(wintab *wintab);
void wintab_term_paste(wintab *wintab);
void wintab_term_set_focus(wintab *wintab, int has_focus);
wintabitem* wintab_get_active_item(wintab *wintab);
void wintab_get_page_rect(wintab *wintab, RECT *rc);
void wintab_require_resize(wintab *wintab, int tab_width, int tab_height);
void wintab_get_extra_size(wintab *wintab, int *extra_width, int *extra_height);

int wintab_drawitems(wintab *wintab);
int wintab_del_rgn(wintab *wintab);
int wintab_drawitem(wintab *wintab, HDC hdc, const int index);
int wintab_on_paint(wintab* wintab, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
int wintab_hit_tab(wintab *wintab, const int x, const int y);
int wintab_on_lclick(wintab* wintab, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WintabWndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);

//-----------------------------------------------------------------------
// tabbar item related
//-----------------------------------------------------------------------
int wintabitem_creat(wintab *wintab, Config *cfg);
int wintabitem_init(wintab *wintab, wintabitem *tabitem, Config *cfg);
void wintabitem_fini(wintabitem *tabitem);
void wintabitem_cfgtopalette(wintabitem *tabitem);
void wintabitem_systopalette(wintabitem *tabitem);
void wintabitem_init_fonts(wintabitem *tabitem, const int pick_width, const int pick_height);
void wintabitem_deinit_fonts(wintabitem *tabitem);
int wintabitem_get_font_width(wintabitem *tabitem, HDC hdc, const TEXTMETRIC *tm);
int wintabitem_CreateCaret(wintabitem *tabitem);
int wintabitem_init_mouse(wintabitem *tabitem);
int wintabitem_start_backend(wintabitem *tabitem);
void wintabitem_init_palette(wintabitem *tabitem);
void wintabitem_check_closed_session(wintabitem *tabitem);
void wintabitem_close_session(wintabitem *tabitem);
int wintabitem_can_close(wintabitem *tabitem);
void wintabitem_require_resize(wintabitem *tabitem, int page_width, int page_height);
void wintabitem_get_extra_size(wintabitem *tabitem, int *extra_width, int *extra_height);
void wintabitem_set_rgn(wintabitem *tabitem, HRGN hRgn);
void wintabitem_set_closer_rgn(wintabitem *tabitem, HRGN hRgn);

int wintabitem_on_scroll(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
int wintabitem_on_paint(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam);
//-----------------------------------------------------------------------
//page related
//-----------------------------------------------------------------------
int wintabpage_init(wintabpage *page, const Config *cfg, HWND hwndParent);
void wintabpage_init_scrollbar(wintabpage *page, Terminal *term);
int wintabpage_fini(wintabpage *page);
int wintabpage_resize(wintabpage *page, const RECT *rc, const int cfg_winborder);
wintabitem * wintabpage_get_item(HWND hwndPage);
void wintabpage_get_term_size(wintabpage *page, int *term_width, int *term_height);

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
//A big thank you to Mr. Clinton Williams 
// who made an incredibly generous donation to Planet Source Code's T1 fund. 
// On behalf of myself and all the users of Planet Source Code, 
// thank you Mr. Williams for helping us keep the site a free resource for all!
typedef struct {
 const UINT Code;
 const char Message[100];
}WMArray;
const static WMArray waWMArray[] = {
{0x0000, "WM_NULL"},{0x0001, "WM_CREATE"},{0x0002, "WM_DESTROY"},{0x0003, "WM_MOVE"},
{0x0005, "WM_SIZE"},{0x0006, "WM_ACTIVATE"},{0x0007, "WM_SETFOCUS"},{0x0008, "WM_KILLFOCUS"},
{0x000A, "WM_ENABLE"},{0x000B, "WM_SETREDRAW"},{0x000C, "WM_SETTEXT"},{0x000D, "WM_GETTEXT"},
{0x000E, "WM_GETTEXTLENGTH"},{0x000F, "WM_PAINT"},{0x0010, "WM_CLOSE"},{0x0011, "WM_QUERYENDSESSION"},
{0x0012, "WM_QUIT"},{0x0013, "WM_QUERYOPEN"},{0x0014, "WM_ERASEBKGND"},{0x0015, "WM_SYSCOLORCHANGE"},
{0x0016, "WM_ENDSESSION"},{0x0018, "WM_SHOWWINDOW"},{0x001A, "WM_WININICHANGE"},{WM_WININICHANGE, "WM_SETTINGCHANGE"},
{0x001B, "WM_DEVMODECHANGE"},{0x001C, "WM_ACTIVATEAPP"},{0x001D, "WM_FONTCHANGE"},{0x001E, "WM_TIMECHANGE"},
{0x001F, "WM_CANCELMODE"},{0x0020, "WM_SETCURSOR"},{0x0021, "WM_MOUSEACTIVATE"},{0x0022, "WM_CHILDACTIVATE"},
{0x0023, "WM_QUEUESYNC"},{0x0024, "WM_GETMINMAXINFO"},{0x0026, "WM_PAINTICON"},{0x0027, "WM_ICONERASEBKGND"},
{0x0028, "WM_NEXTDLGCTL"},{0x002A, "WM_SPOOLERSTATUS"},{0x002B, "WM_DRAWITEM"},{0x002C, "WM_MEASUREITEM"},
{0x002D, "WM_DELETEITEM"},{0x002E, "WM_VKEYTOITEM"},{0x002F, "WM_CHARTOITEM"},{0x0030, "WM_SETFONT"},
{0x0031, "WM_GETFONT"},{0x0032, "WM_SETHOTKEY"},{0x0033, "WM_GETHOTKEY"},{0x0037, "WM_QUERYDRAGICON"},
{0x0039, "WM_COMPAREITEM"},{0x003D, "WM_GETOBJECT"},{0x0041, "WM_COMPACTING"},{0x0044, "WM_COMMNOTIFY"},
{0x0046, "WM_WINDOWPOSCHANGING"},{0x0047, "WM_WINDOWPOSCHANGED"},{0x0048, "WM_POWER"},{0x004A, "WM_COPYDATA"},
{0x004B, "WM_CANCELJOURNAL"},{0x004E, "WM_NOTIFY"},{0x0050, "WM_INPUTLANGCHANGEREQUEST"},{0x0051, "WM_INPUTLANGCHANGE"},
{0x0052, "WM_TCARD"},{0x0053, "WM_HELP"},{0x0054, "WM_USERCHANGED"},{0x0055, "WM_NOTIFYFORMAT"},
{0x007B, "WM_CONTEXTMENU"},{0x007C, "WM_STYLECHANGING"},{0x007D, "WM_STYLECHANGED"},{0x007E, "WM_DISPLAYCHANGE"},
{0x007F, "WM_GETICON"},{0x0080, "WM_SETICON"},{0x0081, "WM_NCCREATE"},{0x0082, "WM_NCDESTROY"},
{0x0083, "WM_NCCALCSIZE"},{0x0084, "WM_NCHITTEST"},{0x0085, "WM_NCPAINT"},{0x0086, "WM_NCACTIVATE"},
{0x0087, "WM_GETDLGCODE"},{0x0088, "WM_SYNCPAINT"},{0x00A0, "WM_NCMOUSEMOVE"},{0x00A1, "WM_NCLBUTTONDOWN"},
{0x00A2, "WM_NCLBUTTONUP"},{0x00A3, "WM_NCLBUTTONDBLCLK"},{0x00A4, "WM_NCRBUTTONDOWN"},{0x00A5, "WM_NCRBUTTONUP"},
{0x00A6, "WM_NCRBUTTONDBLCLK"},{0x00A7, "WM_NCMBUTTONDOWN"},{0x00A8, "WM_NCMBUTTONUP"},{0x00A9, "WM_NCMBUTTONDBLCLK"},
{0x0100, "WM_KEYFIRST"},{0x0100, "WM_KEYDOWN"},{0x0101, "WM_KEYUP"},{0x0102, "WM_CHAR"},
{0x0103, "WM_DEADCHAR"},{0x0104, "WM_SYSKEYDOWN"},{0x0105, "WM_SYSKEYUP"},{0x0106, "WM_SYSCHAR"},
{0x0107, "WM_SYSDEADCHAR"},{0x0108, "WM_KEYLAST"},{0x010D, "WM_IME_STARTCOMPOSITION"},{0x010E, "WM_IME_ENDCOMPOSITION"},
{0x010F, "WM_IME_COMPOSITION"},{0x010F, "WM_IME_KEYLAST"},{0x0110, "WM_INITDIALOG"},{0x0111, "WM_COMMAND"},
{0x0112, "WM_SYSCOMMAND"},{0x0113, "WM_TIMER"},{0x0114, "WM_HSCROLL"},{0x0115, "WM_VSCROLL"},
{0x0116, "WM_INITMENU"},{0x0117, "WM_INITMENUPOPUP"},{0x011F, "WM_MENUSELECT"},{0x0120, "WM_MENUCHAR"},
{0x0121, "WM_ENTERIDLE"},{0x0122, "WM_MENURBUTTONUP"},{0x0123, "WM_MENUDRAG"},{0x0124, "WM_MENUGETOBJECT"},
{0x0125, "WM_UNINITMENUPOPUP"},{0x0126, "WM_MENUCOMMAND"},{0x0132, "WM_CTLCOLORMSGBOX"},{0x0133, "WM_CTLCOLOREDIT"},
{0x0134, "WM_CTLCOLORLISTBOX"},{0x0135, "WM_CTLCOLORBTN"},{0x0136, "WM_CTLCOLORDLG"},{0x0137, "WM_CTLCOLORSCROLLBAR"},
{0x0138, "WM_CTLCOLORSTATIC"},{0x0200, "WM_MOUSEFIRST"},{0x0200, "WM_MOUSEMOVE"},{0x0201, "WM_LBUTTONDOWN"},
{0x0202, "WM_LBUTTONUP"},{0x0203, "WM_LBUTTONDBLCLK"},{0x0204, "WM_RBUTTONDOWN"},{0x0205, "WM_RBUTTONUP"},
{0x0206, "WM_RBUTTONDBLCLK"},{0x0207, "WM_MBUTTONDOWN"},{0x0208, "WM_MBUTTONUP"},{0x0209, "WM_MBUTTONDBLCLK"},
{0x020A, "WM_MOUSEWHEEL"},{0x020A, "WM_MOUSELAST"},{0x0209, "WM_MOUSELAST"},{0x0210, "WM_PARENTNOTIFY"},
{0x0211, "WM_ENTERMENULOOP"},{0x0212, "WM_EXITMENULOOP"},{0x0213, "WM_NEXTMENU"},{0x0214, "WM_SIZING"},
{0x0215, "WM_CAPTURECHANGED"},{0x0216, "WM_MOVING"},{0x0218, "WM_POWERBROADCAST"},{0x0219, "WM_DEVICECHANGE"},
{0x0220, "WM_MDICREATE"},{0x0221, "WM_MDIDESTROY"},{0x0222, "WM_MDIACTIVATE"},{0x0223, "WM_MDIRESTORE"},
{0x0224, "WM_MDINEXT"},{0x0225, "WM_MDIMAXIMIZE"},{0x0226, "WM_MDITILE"},{0x0227, "WM_MDICASCADE"},
{0x0228, "WM_MDIICONARRANGE"},{0x0229, "WM_MDIGETACTIVE"},{0x0230, "WM_MDISETMENU"},{0x0231, "WM_ENTERSIZEMOVE"},
{0x0232, "WM_EXITSIZEMOVE"},{0x0233, "WM_DROPFILES"},{0x0234, "WM_MDIREFRESHMENU"},{0x0281, "WM_IME_SETCONTEXT"},
{0x0282, "WM_IME_NOTIFY"},{0x0283, "WM_IME_CONTROL"},{0x0284, "WM_IME_COMPOSITIONFULL"},{0x0285, "WM_IME_SELECT"},
{0x0286, "WM_IME_CHAR"},{0x0288, "WM_IME_REQUEST"},{0x0290, "WM_IME_KEYDOWN"},{0x0291, "WM_IME_KEYUP"},
{0x02A1, "WM_MOUSEHOVER"},{0x02A3, "WM_MOUSELEAVE"},{0x0300, "WM_CUT"},{0x0301, "WM_COPY"},
{0x0302, "WM_PASTE"},{0x0303, "WM_CLEAR"},{0x0304, "WM_UNDO"},{0x0305, "WM_RENDERFORMAT"},
{0x0306, "WM_RENDERALLFORMATS"},{0x0307, "WM_DESTROYCLIPBOARD"},{0x0308, "WM_DRAWCLIPBOARD"},{0x0309, "WM_PAINTCLIPBOARD"},
{0x030A, "WM_VSCROLLCLIPBOARD"},{0x030B, "WM_SIZECLIPBOARD"},{0x030C, "WM_ASKCBFORMATNAME"},{0x030D, "WM_CHANGECBCHAIN"},
{0x030E, "WM_HSCROLLCLIPBOARD"},{0x030F, "WM_QUERYNEWPALETTE"},{0x0310, "WM_PALETTEISCHANGING"},{0x0311, "WM_PALETTECHANGED"},
{0x0312, "WM_HOTKEY"},{0x0317, "WM_PRINT"},{0x0318, "WM_PRINTCLIENT"},{0x0358, "WM_HANDHELDFIRST"},
{0x035F, "WM_HANDHELDLAST"},{0x0360, "WM_AFXFIRST"},{0x037F, "WM_AFXLAST"},{0x0380, "WM_PENWINFIRST"},
{0x038F, "WM_PENWINLAST"},{0x8000, "WM_APP"},{0x0400, "WM_USER"},
{WM_IGNORE_CLIP, "WM_IGNORE_CLIP"},
{WM_FULLSCR_ON_MAX, "WM_FULLSCR_ON_MAX"},
{WM_AGENT_CALLBACK, "WM_AGENT_CALLBACK"},
{WM_NETEVENT, "WM_NETEVENT"},
{WM_GOT_CLIPDATA, "WM_GOT_CLIPDATA"}
};
// 203 number of elemts...
static const char* TranslateWMessage(UINT uMsg)
{
    int i;
    static char buf[16];
    for(i=0; i < sizeof(waWMArray)/ sizeof( WMArray ); i++){
        if(waWMArray[i].Code == uMsg){
         return waWMArray[i].Message;
        }
    }
    sprintf(buf, "%d", uMsg);
    return buf;
}
void ErrorExit(char * str) ;

#endif
