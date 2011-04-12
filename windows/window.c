/*
 * window.c - the PuTTY(tel) main program, which runs a PuTTY terminal
 * emulator and backend in a window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#ifndef NO_MULTIMON
#define COMPILE_MULTIMON_STUBS
#endif

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"
#include "terminal.h"
#include "storage.h"
#include "win_res.h"
#include "wintab.h"

#ifndef NO_MULTIMON
#include <multimon.h>
#endif

#include <imm.h>
#include <commctrl.h>
#include <richedit.h>
#include <mmsystem.h>




/* Needed for Chinese support and apparently not always defined. */
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

/* Mouse wheel support. */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A	       /* not defined in earlier SDKs */
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

static Mouse_Button translate_button(wintabitem* tabitem, Mouse_Button button);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(wintabitem* tabitem, UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output);
static void another_font(Context,int);
static void set_input_locale(HKL);
static void update_savedsess_menu(void);
static void init_flashwindow(void);

static int is_full_screen(void);
static void make_full_screen(wintabitem* tabitem);
static void clear_full_screen(wintabitem* tabitem);
static void flip_full_screen(void);
int process_clipdata(HGLOBAL clipdata, int unicode);

static int was_zoomed = 0;
  
static int pending_netevent = 0;
static WPARAM pend_netevent_wParam = 0;
static LPARAM pend_netevent_lParam = 0;
static void enact_pending_netevent(void);
static void flash_window(int mode);
static void sys_cursor_update(wintabitem *tabitem);
static int get_fullscreen_rect(RECT * ss);

static int kbd_codepage;
static int reconfiguring = FALSE;
//static HMENU specials_menu = NULL;

static wchar_t *clipboard_contents;
static size_t clipboard_length;

static UINT last_mousemove = 0;

#define TIMING_TIMER_ID 1234
static long timing_next_time;

static int need_backend_resize = FALSE;
int ignore_clip = FALSE;
static int fullscr_on_max = FALSE;
static int processed_resize = FALSE;

static struct {
    HMENU menu;
} popup_menus[2];
enum { SYSMENU, CTXMENU };
static HMENU savedsess_menu;

Config cfg;			       /* exported to windlg.c */
wintab tab;
static int initialized = 0;

static struct sesslist sesslist;       /* for saved-session menu */

struct agent_callback {
    void (*callback)(void *, void *, int);
    void *callback_ctx;
    void *data;
    int len;
};

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

#define NCFGCOLOURS 22
#define NEXTCOLOURS 240
#define NALLCOLOURS (NCFGCOLOURS + NEXTCOLOURS)

static int compose_state = 0;

static UINT wm_mousewheel = WM_MOUSEWHEEL;

int win_fullscr_on_max()
{
    return fullscr_on_max;
}
/* Dummy routine, only required in plink. */
void ldisc_update(void *frontend, int echo, int edit)
{
}

char *get_ttymode(void *frontend, const char *mode)
{
    assert(frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    return term_get_ttymode(tabitem->term, mode);
}

/*
 * Initialize COM.
 */
int init_com()
{
    HRESULT hr;
    hr = CoInitialize(NULL);
    if (hr != S_OK && hr != S_FALSE) {
            char *str = dupprintf("%s Fatal Error", appname);
    	MessageBox(NULL, "Failed to initialize COM subsystem",
    		   str, MB_OK | MB_ICONEXCLAMATION);
    	sfree(str);
    	return 1;
    }
    return 0;
}

/*
 * Process the command line.
 */
void process_cmdline(LPSTR cmdline)
{
	char *p;
	int got_host = 0;
	/* By default, we bring up the config dialog, rather than launching
	 * a session. This gets set to TRUE if something happens to change
	 * that (e.g., a hostname is specified on the command-line). */
	int allow_launch = FALSE;

	default_protocol = be_default_protocol;
	/* Find the appropriate default port. */
	{
	    Backend *b = backend_from_proto(default_protocol);
	    default_port = 0; /* illegal */
	    if (b)
		default_port = b->default_port;
	}
	cfg.logtype = LGTYP_NONE;

	do_defaults(NULL, &cfg);

	p = cmdline;

	/*
	 * Process a couple of command-line options which are more
	 * easily dealt with before the line is broken up into words.
	 * These are the old-fashioned but convenient @sessionname and
	 * the internal-use-only &sharedmemoryhandle, neither of which
	 * are combined with anything else.
	 */
	while (*p && isspace(*p))
	    p++;
	if (*p == '@') {
            /*
             * An initial @ means that the whole of the rest of the
             * command line should be treated as the name of a saved
             * session, with _no quoting or escaping_. This makes it a
             * very convenient means of automated saved-session
             * launching, via IDM_SAVEDSESS or Windows 7 jump lists.
             */
	    int i = strlen(p);
	    while (i > 1 && isspace(p[i - 1]))
		i--;
	    p[i] = '\0';
	    do_defaults(p + 1, &cfg);
	    if (!cfg_launchable(&cfg) && !do_config()) {
		cleanup_exit(0);
	    }
	    allow_launch = TRUE;    /* allow it to be launched directly */
	} else if (*p == '&') {
	    /*
	     * An initial & means we've been given a command line
	     * containing the hex value of a HANDLE for a file
	     * mapping object, which we must then extract as a
	     * config.
	     */
	    HANDLE filemap;
	    Config *cp;
	    if (sscanf(p + 1, "%p", &filemap) == 1 &&
		(cp = MapViewOfFile(filemap, FILE_MAP_READ,
				    0, 0, sizeof(Config))) != NULL) {
		cfg = *cp;
		UnmapViewOfFile(cp);
		CloseHandle(filemap);
	    } else if (!do_config()) {
		cleanup_exit(0);
	    }
	    allow_launch = TRUE;
	} else {
	    /*
	     * Otherwise, break up the command line and deal with
	     * it sensibly.
	     */
	    int argc, i;
	    char **argv;
	    
	    split_into_argv(cmdline, &argc, &argv, NULL);

	    for (i = 0; i < argc; i++) {
		char *p = argv[i];
		int ret;

		ret = cmdline_process_param(p, i+1<argc?argv[i+1]:NULL,
					    1, &cfg);
		if (ret == -2) {
		    cmdline_error("option \"%s\" requires an argument", p);
		} else if (ret == 2) {
		    i++;	       /* skip next argument */
		} else if (ret == 1) {
		    continue;	       /* nothing further needs doing */
		} else if (!strcmp(p, "-cleanup") ||
			   !strcmp(p, "-cleanup-during-uninstall")) {
		    /*
		     * `putty -cleanup'. Remove all registry
		     * entries associated with PuTTY, and also find
		     * and delete the random seed file.
		     */
		    char *s1, *s2;
		    /* Are we being invoked from an uninstaller? */
		    if (!strcmp(p, "-cleanup-during-uninstall")) {
			s1 = dupprintf("Remove saved sessions and random seed file?\n"
				       "\n"
				       "If you hit Yes, ALL Registry entries associated\n"
				       "with %s will be removed, as well as the\n"
				       "random seed file. THIS PROCESS WILL\n"
				       "DESTROY YOUR SAVED SESSIONS.\n"
				       "(This only affects the currently logged-in user.)\n"
				       "\n"
				       "If you hit No, uninstallation will proceed, but\n"
				       "saved sessions etc will be left on the machine.",
				       appname);
			s2 = dupprintf("%s Uninstallation", appname);
		    } else {
			s1 = dupprintf("This procedure will remove ALL Registry entries\n"
				       "associated with %s, and will also remove\n"
				       "the random seed file. (This only affects the\n"
				       "currently logged-in user.)\n"
				       "\n"
				       "THIS PROCESS WILL DESTROY YOUR SAVED SESSIONS.\n"
				       "Are you really sure you want to continue?",
				       appname);
			s2 = dupprintf("%s Warning", appname);
		    }
		    if (message_box(s1, s2,
				    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
				    HELPCTXID(option_cleanup)) == IDYES) {
			cleanup_all();
		    }
		    sfree(s1);
		    sfree(s2);
		    exit(0);
		} else if (!strcmp(p, "-pgpfp")) {
		    pgp_fingerprints();
		    exit(1);
		} else if (*p != '-') {
		    char *q = p;
		    if (got_host) {
			/*
			 * If we already have a host name, treat
			 * this argument as a port number. NB we
			 * have to treat this as a saved -P
			 * argument, so that it will be deferred
			 * until it's a good moment to run it.
			 */
			int ret = cmdline_process_param("-P", p, 1, &cfg);
			assert(ret == 2);
		    } else if (!strncmp(q, "telnet:", 7)) {
			/*
			 * If the hostname starts with "telnet:",
			 * set the protocol to Telnet and process
			 * the string as a Telnet URL.
			 */
			char c;

			q += 7;
			if (q[0] == '/' && q[1] == '/')
			    q += 2;
			cfg.protocol = PROT_TELNET;
			p = q;
			while (*p && *p != ':' && *p != '/')
			    p++;
			c = *p;
			if (*p)
			    *p++ = '\0';
			if (c == ':')
			    cfg.port = atoi(p);
			else
			    cfg.port = -1;
			strncpy(cfg.host, q, sizeof(cfg.host) - 1);
			cfg.host[sizeof(cfg.host) - 1] = '\0';
			got_host = 1;
		    } else {
			/*
			 * Otherwise, treat this argument as a host
			 * name.
			 */
			while (*p && !isspace(*p))
			    p++;
			if (*p)
			    *p++ = '\0';
			strncpy(cfg.host, q, sizeof(cfg.host) - 1);
			cfg.host[sizeof(cfg.host) - 1] = '\0';
			got_host = 1;
		    }
		} else {
		    cmdline_error("unknown option \"%s\"", p);
		}
	    }
	}

	cmdline_run_saved(&cfg);

	if (loaded_session || got_host)
	    allow_launch = TRUE;

	if ((!allow_launch || !cfg_launchable(&cfg)) && !do_config()) {
	    cleanup_exit(0);
	}

	adjust_host(&cfg);
}


int registerClass(HINSTANCE inst)
{
    WNDCLASS wndclass;
	wndclass.style = 0;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = inst;
	wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = appname;

	return RegisterClass(&wndclass);
}

void guessSize(int *width, int *height)
{
    int guess_width, guess_height;
    int extra_width, extra_height;
    int font_width, font_height;
    /*
     * Guess some defaults for the window size. This all gets
     * updated later, so we don't really care too much. However, we
     * do want the font width/height guesses to correspond to a
     * large font rather than a small one...
     */

    font_width = 10;
    font_height = 20;
    extra_width = 25;
    extra_height = 28;
    guess_width = extra_width + font_width * cfg.width;
    guess_height = extra_height + font_height * cfg.height;
	RECT r;
	get_fullscreen_rect(&r);
	if (guess_width > r.right - r.left)
	    guess_width = r.right - r.left;
	if (guess_height > r.bottom - r.top)
	    guess_height = r.bottom - r.top;
    *width = guess_width;
    *height = guess_height;
}

void creatMainWindow(HINSTANCE inst, int width, int height)
{ 
	int winmode = WS_OVERLAPPEDWINDOW;
	int exwinmode = 0;
	if (cfg.resize_action == RESIZE_DISABLED)
	    winmode &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
	if (cfg.alwaysontop)
	    exwinmode |= WS_EX_TOPMOST;
	if (cfg.sunken_edge)
	    exwinmode |= WS_EX_CLIENTEDGE;
	hwnd = CreateWindowEx(exwinmode, appname, appname,
			      winmode, CW_USEDEFAULT, CW_USEDEFAULT,
			      width, height,
			      NULL, NULL, inst, NULL);
}

void resizeWindows()
{
    int guess_width, guess_height;
    int extra_width, extra_height;
    int font_width, font_height;
    int offset_width, offset_height;
    Terminal* term = wintab_get_active_item(&tab)->term;
     /*
     * Correct the guesses for extra_{width,height}.
     */
	RECT cr, wr;
	GetWindowRect(hwnd, &wr);
	GetClientRect(hwnd, &cr);
	offset_width = offset_height = cfg.window_border;
	extra_width = wr.right - wr.left - cr.right + cr.left + offset_width*2;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top +offset_height*2;

    /*
     * Resize the window, now we know what size we _really_ want it
     * to be.
     */
    font_width = 10;
    font_height = 20;
    guess_width = extra_width + font_width * term->cols;
    guess_height = extra_height + font_height * term->rows;
    SetWindowPos(hwnd, NULL, 0, 0, guess_width, guess_height,
		 SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;   
    int guess_width, guess_height;
    hinst = inst;
    hwnd = NULL;
    flags = FLAG_VERBOSE | FLAG_INTERACTIVE;
    initialized = 0;

    sk_init();

    LoadLibraryA("backtrace.dll");

    InitCommonControls();

    /* Ensure a Maximize setting in Explorer doesn't maximise the
     * config box. */
    defuse_showwindow();

    if (!init_winver())
    {
	char *str = dupprintf("%s Fatal Error", appname);
	MessageBox(NULL, "Windows refuses to report a version",
		   str, MB_OK | MB_ICONEXCLAMATION);
	sfree(str);
	return 1;
    }

    /*
     * If we're running a version of Windows that doesn't support
     * WM_MOUSEWHEEL, find out what message number we should be
     * using instead.
     */
    if (osVersion.dwMajorVersion < 4 ||
	(osVersion.dwMajorVersion == 4 && 
	 osVersion.dwPlatformId != VER_PLATFORM_WIN32_NT))
	wm_mousewheel = RegisterWindowMessage("MSWHEEL_ROLLMSG");

    init_help();

    init_flashwindow();

    if (init_com() != 0)
        return 1;

    process_cmdline(cmdline);
    
    if (!prev) {
        registerClass(inst);
    }

    guessSize(&guess_width, &guess_height);
    creatMainWindow(inst, guess_width, guess_height);

    wintab_init(&tab, hwnd);

    //resizeWindows();

    {
	HMENU m;
	int j;
	char *str;

	popup_menus[SYSMENU].menu = GetSystemMenu(hwnd, FALSE);
	popup_menus[CTXMENU].menu = CreatePopupMenu();
	AppendMenu(popup_menus[CTXMENU].menu, MF_ENABLED, IDM_PASTE, "&Paste");

	savedsess_menu = CreateMenu();
	get_sesslist(&sesslist, TRUE);
	update_savedsess_menu();

	for (j = 0; j < lenof(popup_menus); j++) {
	    m = popup_menus[j].menu;

	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, MF_ENABLED, IDM_SHOWLOG, "&Event Log");
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, MF_ENABLED, IDM_NEWSESS, "Ne&w Session...");
	    AppendMenu(m, MF_ENABLED, IDM_DUPSESS, "&Duplicate Session");
	    //AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT) savedsess_menu,
		//       "Sa&ved Sessions");
	    AppendMenu(m, MF_ENABLED, IDM_RECONF, "Chan&ge Settings...");
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, MF_ENABLED, IDM_COPYALL, "C&opy All to Clipboard");
	    AppendMenu(m, MF_ENABLED, IDM_CLRSB, "C&lear Scrollback");
	    AppendMenu(m, MF_ENABLED, IDM_RESET, "Rese&t Terminal");
	    //AppendMenu(m, MF_SEPARATOR, 0, 0);
	    //AppendMenu(m, (cfg.resize_action == RESIZE_DISABLED) ?
		//       MF_GRAYED : MF_ENABLED, IDM_FULLSCREEN, "&Full Screen");
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    if (has_help())
		AppendMenu(m, MF_ENABLED, IDM_HELP, "&Help");
	    str = dupprintf("&About %s", appname);
	    AppendMenu(m, MF_ENABLED, IDM_ABOUT, str);
	    sfree(str);
	}
    }

    //    start_backend();

    /*
     * Set up the initial input locale.
     */
    set_input_locale(GetKeyboardLayout(0));

    /*
     * Finally show the window!
     */
    ShowWindow(hwnd, show);
    SetForegroundWindow(hwnd);

            /*
             * Set the palette up.
             */

            //init_palette();

            //term_set_focus(term, GetForegroundWindow() == hwnd);
    UpdateWindow(hwnd);
    initialized = 1;

    while (1) {
    	HANDLE *handles;
    	int nhandles, n;

    	handles = handle_get_events(&nhandles);

    	n = MsgWaitForMultipleObjects(nhandles, handles, FALSE, INFINITE,
    				      QS_ALLINPUT);

    	if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)nhandles) {
    	    handle_got_event(handles[n - WAIT_OBJECT_0]);
    	    sfree(handles);
    	    wintab_check_closed_session(&tab);
    	} else
    	    sfree(handles);

    	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    	    if (msg.message == WM_QUIT)
    		goto finished;	       /* two-level break */

    	    if (!(wintab_is_logboxmsg(&tab, &msg)) ){ 
                if ((msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN)
                    ||(!IsDialogMessage(hwnd, &msg)))
                    DispatchMessage(&msg);
    	    }
    	    /* Send the paste buffer if there's anything to send */
    	    wintab_term_paste(&tab);
    	    /* If there's nothing new in the queue then we can do everything
    	     * we've delayed, reading the socket, writing, and repainting
    	     * the window.
    	     */
    	    wintab_check_closed_session(&tab);
    	}

    	/* The messages seem unreliable; especially if we're being tricky */
    	wintab_term_set_focus(&tab, GetForegroundWindow() == hwnd);

    	if (pending_netevent)
    	    enact_pending_netevent();

    	net_pending_errors();
    }

    finished:
    cleanup_exit(msg.wParam);	       /* this doesn't return... */
    return msg.wParam;		       /* ... but optimiser doesn't know */
}

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    wintab_fini(&tab);
    sk_cleanup();

    shutdown_help();

    /* Clean up COM. */
    CoUninitialize();

    exit(code);
}

/*
 * Set up, or shut down, an AsyncSelect. Called from winnet.c.
 */
char *do_select(SOCKET skt, int startup)
{
    int msg, events;
    if (startup) {
	msg = WM_NETEVENT;
	events = (FD_CONNECT | FD_READ | FD_WRITE |
		  FD_OOB | FD_CLOSE | FD_ACCEPT);
    } else {
	msg = events = 0;
    }
    if (!hwnd)
	return "do_select(): internal error (hwnd==NULL)";
    if (p_WSAAsyncSelect(skt, hwnd, msg, events) == SOCKET_ERROR) {
	switch (p_WSAGetLastError()) {
	  case WSAENETDOWN:
	    return "Network is down";
	  default:
	    return "WSAAsyncSelect(): unknown error";
	}
    }
    return NULL;
}

/*
 * Refresh the saved-session submenu from `sesslist'.
 */
static void update_savedsess_menu(void)
{
    int i;
    while (DeleteMenu(savedsess_menu, 0, MF_BYPOSITION)) ;
    /* skip sesslist.sessions[0] == Default Settings */
    for (i = 1;
	 i < ((sesslist.nsessions <= MENU_SAVED_MAX+1) ? sesslist.nsessions
						       : MENU_SAVED_MAX+1);
	 i++)
	AppendMenu(savedsess_menu, MF_ENABLED,
		   IDM_SAVED_MIN + (i-1)*MENU_SAVED_STEP,
		   sesslist.sessions[i]);
    if (sesslist.nsessions <= 1)
	AppendMenu(savedsess_menu, MF_GRAYED, IDM_SAVED_MIN, "(No sessions)");
}

/*
 * Update the Special Commands submenu.
 */
void update_specials_menu(void *frontend)
{
    assert (frontend != NULL);

    wintabitem *tabitem = (wintabitem*) frontend;
    HMENU new_menu;
    int i;//, j;

    if (tabitem->back)
	tabitem->specials = tabitem->back->get_specials(tabitem->backhandle);
    else
	tabitem->specials = NULL;

    if (tabitem->specials) {
	/* We can't use Windows to provide a stack for submenus, so
	 * here's a lame "stack" that will do for now. */
	HMENU saved_menu = NULL;
	int nesting = 1;
	new_menu = CreatePopupMenu();
	for (i = 0; nesting > 0; i++) {
	    assert(IDM_SPECIAL_MIN + 0x10 * i < IDM_SPECIAL_MAX);
	    switch (tabitem->specials[i].code) {
	      case TS_SEP:
		AppendMenu(new_menu, MF_SEPARATOR, 0, 0);
		break;
	      case TS_SUBMENU:
		assert(nesting < 2);
		nesting++;
		saved_menu = new_menu; /* XXX lame stacking */
		new_menu = CreatePopupMenu();
		AppendMenu(saved_menu, MF_POPUP | MF_ENABLED,
			   (UINT) new_menu, tabitem->specials[i].name);
		break;
	      case TS_EXITMENU:
		nesting--;
		if (nesting) {
		    new_menu = saved_menu; /* XXX lame stacking */
		    saved_menu = NULL;
		}
		break;
	      default:
		AppendMenu(new_menu, MF_ENABLED, IDM_SPECIAL_MIN + 0x10 * i,
			   tabitem->specials[i].name);
		break;
	    }
	}
	/* Squirrel the highest special. */
	tabitem->n_specials = i - 1;
    } else {
	new_menu = NULL;
	tabitem->n_specials = 0;
    }
    /* not show the special menu 
    for (j = 0; j < lenof(popup_menus); j++) {
	if (tabitem->specials_menu) {
	    // XXX does this free up all submenus? 
	    DeleteMenu(popup_menus[j].menu, (UINT)specials_menu, MF_BYCOMMAND);
	    DeleteMenu(popup_menus[j].menu, IDM_SPECIALSEP, MF_BYCOMMAND);
	}
	if (new_menu) {
	    InsertMenu(popup_menus[j].menu, IDM_SHOWLOG,
		       MF_BYCOMMAND | MF_POPUP | MF_ENABLED,
		       (UINT) new_menu, "S&pecial Command");
	    InsertMenu(popup_menus[j].menu, IDM_SHOWLOG,
		       MF_BYCOMMAND | MF_SEPARATOR, IDM_SPECIALSEP, 0);
	}
    }*/
    tabitem->specials_menu = new_menu;
}

static void update_mouse_pointer(void *frontend)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    
    LPTSTR curstype;
    int force_visible = FALSE;
    static int forced_visible = FALSE;
    switch (tabitem->busy_status) {
      case BUSY_NOT:
	if (tabitem->send_raw_mouse)
	    curstype = IDC_ARROW;
	else
	    curstype = IDC_IBEAM;
	break;
      case BUSY_WAITING:
	curstype = IDC_APPSTARTING; /* this may be an abuse */
	force_visible = TRUE;
	break;
      case BUSY_CPU:
	curstype = IDC_WAIT;
	force_visible = TRUE;
	break;
      default:
	assert(0);
    }
    {
	HCURSOR cursor = LoadCursor(NULL, curstype);
	SetClassLongPtr(tabitem->page.hwndCtrl, GCLP_HCURSOR, (LONG_PTR)cursor);
	SetCursor(cursor); /* force redraw of cursor at current posn */
    }
    if (force_visible != forced_visible) {
	/* We want some cursor shapes to be visible always.
	 * Along with show_mouseptr(), this manages the ShowCursor()
	 * counter such that if we switch back to a non-force_visible
	 * cursor, the previous visibility state is restored. */
	ShowCursor(force_visible);
	forced_visible = force_visible;
    }
}

void set_busy_status(void *frontend, int status)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    tabitem->busy_status = status;
    update_mouse_pointer(frontend);
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(void *frontend, int activate)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    activate = activate && !tabitem->cfg.no_mouse_rep;
    tabitem->send_raw_mouse = activate;
    update_mouse_pointer(frontend);
}

/*
 * Print a message box and close the connection.
 */
void connection_fatal(void *frontend, char *fmt, ...)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    
    va_list ap;
    char *stuff, morestuff[100];

    va_start(ap, fmt);
    stuff = dupvprintf(fmt, ap);
    va_end(ap);
    sprintf(morestuff, "%.70s Fatal Error", appname);
    MessageBox(tabitem->page.hwndCtrl, stuff, morestuff, MB_ICONERROR | MB_OK);
    sfree(stuff);

    //if (tabitem->cfg.close_on_exit == FORCE_ON)
	//    PostQuitMessage(1);
    //else {
    tabitem->must_close_session = TRUE;
    //}
}

/*
 * Report an error at the command-line parsing stage.
 */
void cmdline_error(char *fmt, ...)
{
    va_list ap;
    char *stuff, morestuff[100];

    va_start(ap, fmt);
    stuff = dupvprintf(fmt, ap);
    va_end(ap);
    sprintf(morestuff, "%.70s Command Line Error", appname);
    MessageBox(hwnd, stuff, morestuff, MB_ICONERROR | MB_OK);
    sfree(stuff);
    exit(1);
}

/*
 * Actually do the job requested by a WM_NETEVENT
 */
static void enact_pending_netevent(void)
{
    static int reentering = 0;
    extern int select_result(WPARAM, LPARAM);

    if (reentering)
	return;			       /* don't unpend the pending */

    pending_netevent = FALSE;

    reentering = 1;
    select_result(pend_netevent_wParam, pend_netevent_lParam);
    reentering = 0;
}
#if 0
/*
 * Copy the colour palette from the configuration data into defpal.
 * This is non-trivial because the colour indices are different.
 */

static void cfgtopalette(void)
{
    int i;
    static const int ww[] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };

    for (i = 0; i < 22; i++) {
	int w = ww[i];
	defpal[w].rgbtRed = cfg.colours[i][0];
	defpal[w].rgbtGreen = cfg.colours[i][1];
	defpal[w].rgbtBlue = cfg.colours[i][2];
    }
    for (i = 0; i < NEXTCOLOURS; i++) {
	if (i < 216) {
	    int r = i / 36, g = (i / 6) % 6, b = i % 6;
	    defpal[i+16].rgbtRed = r ? r * 40 + 55 : 0;
	    defpal[i+16].rgbtGreen = g ? g * 40 + 55 : 0;
	    defpal[i+16].rgbtBlue = b ? b * 40 + 55 : 0;
	} else {
	    int shade = i - 216;
	    shade = shade * 10 + 8;
	    defpal[i+16].rgbtRed = defpal[i+16].rgbtGreen =
		defpal[i+16].rgbtBlue = shade;
	}
    }

    /* Override with system colours if appropriate */
    if (cfg.system_colour)
        systopalette();
}

/*
 * Override bit of defpal with colours from the system.
 * (NB that this takes a copy the system colours at the time this is called,
 * so subsequent colour scheme changes don't take effect. To fix that we'd
 * probably want to be using GetSysColorBrush() and the like.)
 */
static void systopalette(void)
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
	defpal[or[i].norm].rgbtRed =
	   defpal[or[i].bold].rgbtRed = GetRValue(colour);
	defpal[or[i].norm].rgbtGreen =
	   defpal[or[i].bold].rgbtGreen = GetGValue(colour);
	defpal[or[i].norm].rgbtBlue =
	   defpal[or[i].bold].rgbtBlue = GetBValue(colour);
    }
}

/*
 * Set up the colour palette.
 */
static void init_palette(void)
{
    int i;
    HDC hdc = GetDC(HWNDDC);
    if (hdc) {
	if (cfg.try_palette && GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
	    /*
	     * This is a genuine case where we must use smalloc
	     * because the snew macros can't cope.
	     */
	    logpal = smalloc(sizeof(*logpal)
			     - sizeof(logpal->palPalEntry)
			     + NALLCOLOURS * sizeof(PALETTEENTRY));
	    logpal->palVersion = 0x300;
	    logpal->palNumEntries = NALLCOLOURS;
	    for (i = 0; i < NALLCOLOURS; i++) {
		logpal->palPalEntry[i].peRed = defpal[i].rgbtRed;
		logpal->palPalEntry[i].peGreen = defpal[i].rgbtGreen;
		logpal->palPalEntry[i].peBlue = defpal[i].rgbtBlue;
		logpal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
	    }
	    pal = CreatePalette(logpal);
	    if (pal) {
		SelectPalette(hdc, pal, FALSE);
		RealizePalette(hdc);
		SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
	    }
	}
	ReleaseDC(HWNDDC, hdc);
    }
    if (pal)
	for (i = 0; i < NALLCOLOURS; i++)
	    colours[i] = PALETTERGB(defpal[i].rgbtRed,
				    defpal[i].rgbtGreen,
				    defpal[i].rgbtBlue);
    else
	for (i = 0; i < NALLCOLOURS; i++)
	    colours[i] = RGB(defpal[i].rgbtRed,
			     defpal[i].rgbtGreen, defpal[i].rgbtBlue);
}
#endif
/*
 * This is a wrapper to ExtTextOut() to force Windows to display
 * the precise glyphs we give it. Otherwise it would do its own
 * bidi and Arabic shaping, and we would end up uncertain which
 * characters it had put where.
 */
static void exact_textout(HDC hdc, int x, int y, CONST RECT *lprc,
			  unsigned short *lpString, UINT cbCount,
			  CONST INT *lpDx, int opaque)
{
#ifdef __LCC__
    /*
     * The LCC include files apparently don't supply the
     * GCP_RESULTSW type, but we can make do with GCP_RESULTS
     * proper: the differences aren't important to us (the only
     * variable-width string parameter is one we don't use anyway).
     */
    GCP_RESULTS gcpr;
#else
    GCP_RESULTSW gcpr;
#endif
    char *buffer = snewn(cbCount*2+2, char);
    char *classbuffer = snewn(cbCount, char);
    memset(&gcpr, 0, sizeof(gcpr));
    memset(buffer, 0, cbCount*2+2);
    memset(classbuffer, GCPCLASS_NEUTRAL, cbCount);

    gcpr.lStructSize = sizeof(gcpr);
    gcpr.lpGlyphs = (void *)buffer;
    gcpr.lpClass = (void *)classbuffer;
    gcpr.nGlyphs = cbCount;
    GetCharacterPlacementW(hdc, lpString, cbCount, 0, &gcpr,
			   FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);

    ExtTextOut(hdc, x, y,
	       ETO_GLYPH_INDEX | ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
	       lprc, buffer, cbCount, lpDx);
}

/*
 * The exact_textout() wrapper, unfortunately, destroys the useful
 * Windows `font linking' behaviour: automatic handling of Unicode
 * code points not supported in this font by falling back to a font
 * which does contain them. Therefore, we adopt a multi-layered
 * approach: for any potentially-bidi text, we use exact_textout(),
 * and for everything else we use a simple ExtTextOut as we did
 * before exact_textout() was introduced.
 */
static void general_textout(Context ctx, int x, int y, CONST RECT *lprc,
			    unsigned short *lpString, UINT cbCount,
			    CONST INT *lpDx, int opaque)
{
    assert(ctx != NULL);
    wintabitem *tabitem = (wintabitem *)ctx;
    HDC hdc = tabitem->hdc;
    assert(hdc != NULL);
    
    int i, j, xp, xn;
    int bkmode = 0, got_bkmode = FALSE;

    xp = xn = x;

    for (i = 0; i < (int)cbCount ;) {
	int rtl = is_rtl(lpString[i]);

	xn += lpDx[i];

	for (j = i+1; j < (int)cbCount; j++) {
	    if (rtl != is_rtl(lpString[j]))
		break;
	    xn += lpDx[j];
	}

	/*
	 * Now [i,j) indicates a maximal substring of lpString
	 * which should be displayed using the same textout
	 * function.
	 */
	if (rtl) {
	    exact_textout(hdc, xp, y, lprc, lpString+i, j-i,
                          tabitem->font_varpitch ? NULL : lpDx+i, opaque);
	} else {
	    ExtTextOutW(hdc, xp, y, ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
			lprc, lpString+i, j-i,
                        tabitem->font_varpitch ? NULL : lpDx+i);
	}

	i = j;
	xp = xn;

        bkmode = GetBkMode(hdc);
        got_bkmode = TRUE;
        SetBkMode(hdc, TRANSPARENT);
        opaque = FALSE;
    }

    if (got_bkmode)
        SetBkMode(hdc, bkmode);
}
#if 0
static int get_font_width(HDC hdc, const TEXTMETRIC *tm)
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

        font_varpitch = TRUE;
        font_dualwidth = TRUE;
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
/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one.  The other (potentially) twenty-one fonts are done
 * if/when they are needed.
 *
 * We also:
 *
 * - check the font width and height, correcting our guesses if
 *   necessary.
 *
 * - verify that the bold font is the same width as the ordinary
 *   one, and engage shadow bolding if not.
 * 
 * - verify that the underlined font is the same width as the
 *   ordinary one (manual underlining by means of line drawing can
 *   be done in a pinch).
 */
static void init_fonts(int pick_width, int pick_height)
{
    TEXTMETRIC tm;
    CPINFO cpinfo;
    int fontsize[3];
    int i;
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i = 0; i < FONT_MAXNO; i++)
	fonts[i] = NULL;

    bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
    und_mode = UND_FONT;

    if (cfg.font.isbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_HEAVY;
    } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

    hdc = GetDC(HWNDDC);

    if (pick_height)
	font_height = pick_height;
    else {
	font_height = cfg.font.height;
	if (font_height > 0) {
	    font_height =
		-MulDiv(font_height, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	}
    }
    font_width = pick_width;

#define f(i,c,w,u) \
    fonts[i] = CreateFont (font_height, font_width, 0, 0, w, FALSE, u, FALSE, \
			   c, OUT_DEFAULT_PRECIS, \
		           CLIP_DEFAULT_PRECIS, FONT_QUALITY(cfg.font_quality), \
			   FIXED_PITCH | FF_DONTCARE, cfg.font.name)

    f(FONT_NORMAL, cfg.font.charset, fw_dontcare, FALSE);

    SelectObject(hdc, fonts[FONT_NORMAL]);
    GetTextMetrics(hdc, &tm);

    GetObject(fonts[FONT_NORMAL], sizeof(LOGFONT), &lfont);

    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm.tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        font_varpitch = FALSE;
        font_dualwidth = (tm.tmAveCharWidth != tm.tmMaxCharWidth);
    } else {
        font_varpitch = TRUE;
        font_dualwidth = TRUE;
    }
    if (pick_width == 0 || pick_height == 0) {
	font_height = tm.tmHeight;
        font_width = get_font_width(hdc, &tm);
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
	    ucsdata.font_codepage = GetOEMCP();
	else
	    if (TranslateCharsetInfo ((DWORD *) cset, &info, TCI_SRCCHARSET))
		ucsdata.font_codepage = info.ciACP;
	else
	    ucsdata.font_codepage = -1;

	GetCPInfo(ucsdata.font_codepage, &cpinfo);
	ucsdata.dbcs_screenfont = (cpinfo.MaxCharSize > 1);
    }

    f(FONT_UNDERLINE, cfg.font.charset, fw_dontcare, TRUE);

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
	und_bm = CreateCompatibleBitmap(hdc, font_width, font_height);
	und_oldbm = SelectObject(und_dc, und_bm);
	SelectObject(und_dc, fonts[FONT_UNDERLINE]);
	SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
	SetTextColor(und_dc, RGB(255, 255, 255));
	SetBkColor(und_dc, RGB(0, 0, 0));
	SetBkMode(und_dc, OPAQUE);
	ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
	gotit = FALSE;
	for (i = 0; i < font_height; i++) {
	    c = GetPixel(und_dc, font_width / 2, i);
	    if (c != RGB(0, 0, 0))
		gotit = TRUE;
	}
	SelectObject(und_dc, und_oldbm);
	DeleteObject(und_bm);
	DeleteDC(und_dc);
	if (!gotit) {
	    und_mode = UND_LINE;
	    DeleteObject(fonts[FONT_UNDERLINE]);
	    fonts[FONT_UNDERLINE] = 0;
	}
    }

    if (bold_mode == BOLD_FONT) {
	f(FONT_BOLD, cfg.font.charset, fw_bold, FALSE);
    }
#undef f

    descent = tm.tmAscent + 1;
    if (descent >= font_height)
	descent = font_height - 1;

    for (i = 0; i < 3; i++) {
	if (fonts[i]) {
	    if (SelectObject(hdc, fonts[i]) && GetTextMetrics(hdc, &tm))
		fontsize[i] = get_font_width(hdc, &tm) + 256 * tm.tmHeight;
	    else
		fontsize[i] = -i;
	} else
	    fontsize[i] = -i;
    }

    ReleaseDC(HWNDDC, hdc);

    if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
	und_mode = UND_LINE;
	DeleteObject(fonts[FONT_UNDERLINE]);
	fonts[FONT_UNDERLINE] = 0;
    }

    if (bold_mode == BOLD_FONT &&
	fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
	bold_mode = BOLD_SHADOW;
	DeleteObject(fonts[FONT_BOLD]);
	fonts[FONT_BOLD] = 0;
    }
    fontflag[0] = fontflag[1] = fontflag[2] = 1;

    init_ucs(&cfg, &ucsdata);
}
#endif
static void another_font(Context ctx, int fontno)
{
    assert(ctx != NULL);
    wintabitem *tabitem = (wintabitem *)ctx;
    int basefont;
    int fw_dontcare, fw_bold;
    int c, u, w, x;
    char *s;

    if (fontno < 0 || fontno >= FONT_MAXNO || tabitem->fontflag[fontno])
	return;

    basefont = (fontno & ~(FONT_BOLDUND));
    if (basefont != fontno && !tabitem->fontflag[basefont])
	another_font(ctx, basefont);

    if (tabitem->cfg.font.isbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_HEAVY;
    } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

    c = tabitem->cfg.font.charset;
    w = fw_dontcare;
    u = FALSE;
    s = tabitem->cfg.font.name;
    x = tabitem->font_width;

    if (fontno & FONT_WIDE)
	x *= 2;
    if (fontno & FONT_NARROW)
	x = (x+1)/2;
    if (fontno & FONT_OEM)
	c = OEM_CHARSET;
    if (fontno & FONT_BOLD)
	w = fw_bold;
    if (fontno & FONT_UNDERLINE)
	u = TRUE;

    tabitem->fonts[fontno] =
	CreateFont(tabitem->font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w,
		   FALSE, u, FALSE, c, OUT_DEFAULT_PRECIS,
		   CLIP_DEFAULT_PRECIS, FONT_QUALITY(tabitem->cfg.font_quality),
		   DEFAULT_PITCH | FF_DONTCARE, s);

    tabitem->fontflag[fontno] = 1;
}
#if 0
static void deinit_fonts(void)
{
    int i;
    for (i = 0; i < FONT_MAXNO; i++) {
	if (fonts[i])
	    DeleteObject(fonts[i]);
	fonts[i] = 0;
	fontflag[i] = 0;
    }
}
#endif
//------------------------------------------------------------------------------
void request_resize(void *frontend, int w, int h)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    
    int width, height;
    int extra_width, extra_height;
    wintabitem_get_extra_size(tabitem, &extra_width, &extra_height);

    /* If the window is maximized supress resizing attempts */
    if (IsZoomed(hwnd)) {
    	if (tabitem->cfg.resize_action == RESIZE_TERM)
    	    return;
    }

    if (tabitem->cfg.resize_action == RESIZE_DISABLED) return;
    if (h == tabitem->term->rows && w == tabitem->term->cols) return;

    /* Sanity checks ... */
    {
    	static int first_time = 1;
    	static RECT ss;

    	switch (first_time) {
    	  case 1:
    	    /* Get the size of the screen */
    	    if (get_fullscreen_rect(&ss))
    		/* first_time = 0 */ ;
    	    else {
    		first_time = 2;
    		break;
    	    }
    	  case 0:
    	    /* Make sure the values are sane */
    	    width = (ss.right - ss.left - extra_width) / 4;
    	    height = (ss.bottom - ss.top - extra_height) / 6;

    	    if (w > width || h > height)
    		return;
    	    if (w < 15)
    		w = 15;
    	    if (h < 1)
    		h = 1;
    	}
    }

    term_size(tabitem->term, h, w, tabitem->cfg.savelines);

    if (tabitem->cfg.resize_action != RESIZE_FONT && !IsZoomed(hwnd)) {
	width = tabitem->font_width * w;
	height = tabitem->font_height * h;

	wintabitem_require_resize(tabitem, width, height);
    } else
	reset_window(tabitem, 0);

    InvalidateRect(hwnd, NULL, TRUE);
}

void reset_window(wintabitem* tabitem, int reinit) {
    /*
     * This function decides how to resize or redraw when the 
     * user changes something. 
     *
     * This function doesn't like to change the terminal size but if the
     * font size is locked that may be it's only soluion.
     */
    int win_width, win_height;
    RECT cr, wr;
    assert (tabitem != NULL);

#ifdef RDB_DEBUG_PATCH
    debug((27, "reset_window()"));
#endif

    /* Current window sizes ... */
    GetWindowRect(tabitem->page.hwndCtrl, &wr);
    GetClientRect(tabitem->page.hwndCtrl, &cr);

    win_width  = cr.right - cr.left;
    win_height = cr.bottom - cr.top;

    if (tabitem->cfg.resize_action == RESIZE_DISABLED) reinit = 2;

    /* Are we being forced to reload the fonts ? */
    if (reinit>1) {
    	wintabitem_deinit_fonts(tabitem);
    	wintabitem_init_fonts(tabitem, 0, 0);
    }

    /* Oh, looks like we're minimised */
    if (win_width == 0 || win_height == 0)
    	return;

    /* Is the window out of position ? */
    if ( !reinit && 
	    (tabitem->offset_width != (win_width-tabitem->font_width*tabitem->term->cols)/2 ||
	     tabitem->offset_height != (win_height-tabitem->font_height*tabitem->term->rows)/2) ){
	     
        tabitem->offset_width = (win_width-tabitem->font_width*tabitem->term->cols)/2;
        tabitem->offset_height = (win_height-tabitem->font_height*tabitem->term->rows)/2;
        InvalidateRect(hwnd, NULL, TRUE);
    }

    if (IsZoomed(hwnd)) {
    	/* We're fullscreen, this means we must not change the size of
    	 * the window so it's the font size or the terminal itself.
    	 */

    	if (tabitem->cfg.resize_action != RESIZE_TERM) {
    	    if (  tabitem->font_width != win_width/tabitem->term->cols || 
    		  tabitem->font_height != win_height/tabitem->term->rows) {
    		  
                wintabitem_deinit_fonts(tabitem);
                wintabitem_init_fonts(tabitem, win_width/tabitem->term->cols, win_height/tabitem->term->rows);
                tabitem->offset_width = (win_width-tabitem->font_width*tabitem->term->cols)/2;
                tabitem->offset_height = (win_height-tabitem->font_height*tabitem->term->rows)/2;
                InvalidateRect(hwnd, NULL, TRUE);
    	    }
    	} else {
    	    if (  tabitem->font_width * tabitem->term->cols != win_width || 
    		  tabitem->font_height * tabitem->term->rows != win_height) {
    		  
                /* Our only choice at this point is to change the 
                 * size of the terminal; Oh well.
                 */
                term_size(tabitem->term, win_height/tabitem->font_height, win_width/tabitem->font_width,
                	  tabitem->cfg.savelines);
                tabitem->offset_width = (win_width-tabitem->font_width*tabitem->term->cols)/2;
                tabitem->offset_height = (win_height-tabitem->font_height*tabitem->term->rows)/2;
                InvalidateRect(hwnd, NULL, TRUE);
#ifdef RDB_DEBUG_PATCH
    		debug((27, "reset_window() -> Zoomed term_size"));
#endif
    	    }
    	}
    	return;
    }

    /* Hmm, a force re-init means we should ignore the current window
     * so we resize to the default font size.
     */
    if (reinit>0) {
#ifdef RDB_DEBUG_PATCH
    	debug((27, "reset_window() -> Forced re-init"));
#endif

    	tabitem->offset_width = tabitem->offset_height = tabitem->cfg.window_border;

    	if (win_width != tabitem->font_width*tabitem->term->cols + tabitem->offset_width*2 ||
    	    win_height != tabitem->font_height*tabitem->term->rows + tabitem->offset_height*2) {

    	    /* If this is too large windows will resize it to the maximum
    	     * allowed window size, we will then be back in here and resize
    	     * the font or terminal to fit.
    	     */
    	    wintabitem_require_resize(tabitem,
    	         tabitem->font_width*tabitem->term->cols , 
    			 tabitem->font_height*tabitem->term->rows);
    	}

    	InvalidateRect(hwnd, NULL, TRUE);
    	return;
    }

    /* Okay the user doesn't want us to change the font so we try the 
     * window. But that may be too big for the screen which forces us
     * to change the terminal.
     */
    if ((tabitem->cfg.resize_action == RESIZE_TERM && reinit<=0) ||
        (tabitem->cfg.resize_action == RESIZE_EITHER && reinit<0) ||
            	reinit>0) {
        tabitem->offset_width = tabitem->offset_height = tabitem->cfg.window_border;

    	if (win_width != tabitem->font_width*tabitem->term->cols + tabitem->offset_width*2 ||
    	    win_height != tabitem->font_height*tabitem->term->rows + tabitem->offset_height*2) {

    	    static RECT ss;
    	    int width, height;
            int extra_width, extra_height;
            wintabitem_get_extra_size(tabitem, &extra_width, &extra_height);
    		
    		get_fullscreen_rect(&ss);

    	    width = (ss.right - ss.left - extra_width) / tabitem->font_width;
    	    height = (ss.bottom - ss.top - extra_height) / tabitem->font_height;

    	    /* Grrr too big */
    	    if ( tabitem->term->rows > height || tabitem->term->cols > width ) {
    		if (tabitem->cfg.resize_action == RESIZE_EITHER) {
    		    /* Make the font the biggest we can */
    		    if (tabitem->term->cols > width)
    			tabitem->font_width = (ss.right - ss.left - extra_width)
    			    / tabitem->term->cols;
    		    if (tabitem->term->rows > height)
    			tabitem->font_height = (ss.bottom - ss.top - extra_height)
    			    / tabitem->term->rows;

    		    wintabitem_deinit_fonts(tabitem);
    		    wintabitem_init_fonts(tabitem, tabitem->font_width, tabitem->font_height);

    		    width = (ss.right - ss.left - extra_width) / tabitem->font_width;
    		    height = (ss.bottom - ss.top - extra_height) / tabitem->font_height;
    		} else {
    		    if ( height > tabitem->term->rows ) height = tabitem->term->rows;
    		    if ( width > tabitem->term->cols )  width = tabitem->term->cols;
    		    term_size(tabitem->term, height, width, tabitem->cfg.savelines);
#ifdef RDB_DEBUG_PATCH
    		    debug((27, "reset_window() -> term resize to (%d,%d)",
    			       height, width));
#endif
    		}
    	    }
    	    
    	    wintabitem_require_resize(tabitem,
    	         tabitem->font_width*tabitem->term->cols, 
    			 tabitem->font_height*tabitem->term->rows);

    	    InvalidateRect(hwnd, NULL, TRUE);
#ifdef RDB_DEBUG_PATCH
    	    debug((27, "reset_window() -> window resize to (%d,%d)",
    			tabitem->font_width*tabitem->term->cols + extra_width,
    			tabitem->font_height*tabitem->term->rows + extra_height));
#endif
    	}
    	return;
    }

    /* We're allowed to or must change the font but do we want to ?  */

    if (tabitem->font_width != (win_width-tabitem->cfg.window_border*2)/tabitem->term->cols || 
	tabitem->font_height != (win_height-tabitem->cfg.window_border*2)/tabitem->term->rows) {

	wintabitem_deinit_fonts(tabitem);
	wintabitem_init_fonts(tabitem, (win_width-tabitem->cfg.window_border*2)/tabitem->term->cols, 
		   (win_height-tabitem->cfg.window_border*2)/tabitem->term->rows);
	tabitem->offset_width = (win_width-tabitem->font_width*tabitem->term->cols)/2;
	tabitem->offset_height = (win_height-tabitem->font_height*tabitem->term->rows)/2;

	InvalidateRect(hwnd, NULL, TRUE);
#ifdef RDB_DEBUG_PATCH
	debug((25, "reset_window() -> font resize to (%d,%d)", 
		   tabitem->font_width, tabitem->font_height));
#endif
    }
}

static void set_input_locale(HKL kl)
{
    char lbuf[20];

    GetLocaleInfo(LOWORD(kl), LOCALE_IDEFAULTANSICODEPAGE,
		  lbuf, sizeof(lbuf));

    kbd_codepage = atoi(lbuf);
}

static void click(wintabitem* tabitem, Mouse_Button b, int x, int y, int shift, int ctrl, int alt)
{
    int thistime = GetMessageTime();

    if (tabitem->send_raw_mouse && !(tabitem->cfg.mouse_override && shift)) {
	tabitem->lastbtn = MBT_NOTHING;
	term_mouse(tabitem->term, b, translate_button(tabitem, b), MA_CLICK,
		   x, y, shift, ctrl, alt);
	return;
    }

    if (tabitem->lastbtn == b && thistime - tabitem->lasttime < tabitem->dbltime) {
	tabitem->lastact = (tabitem->lastact == MA_CLICK ? MA_2CLK :
		   tabitem->lastact == MA_2CLK ? MA_3CLK :
		   tabitem->lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
	tabitem->lastbtn = b;
	tabitem->lastact = MA_CLICK;
    }
    if (tabitem->lastact != MA_NOTHING)
	term_mouse(tabitem->term, b, translate_button(tabitem, b), tabitem->lastact,
		   x, y, shift, ctrl, alt);
    tabitem->lasttime = thistime;
}

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
static Mouse_Button translate_button(wintabitem* tabitem, Mouse_Button button)
{
    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return tabitem->cfg.mouse_is_xterm == 1 ? MBT_PASTE : MBT_EXTEND;
    if (button == MBT_RIGHT)
	return tabitem->cfg.mouse_is_xterm == 1 ? MBT_EXTEND : MBT_PASTE;
    return 0;			       /* shouldn't happen */
}

void show_mouseptr(wintabitem *tabitem, int show)
{
    /* NB that the counter in ShowCursor() is also frobbed by
     * update_mouse_pointer() */
    static int cursor_visible = 1;
    if (!tabitem->cfg.hide_mouseptr)	       /* override if this feature disabled */
    	show = 1;
    if (cursor_visible && !show)
    	ShowCursor(FALSE);
    else if (!cursor_visible && show)
    	ShowCursor(TRUE);
    cursor_visible = show;
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

void notify_remote_exit(void *frontend)
{
    int exitcode;
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;

    if (!tabitem->session_closed &&
        (exitcode = tabitem->back->exitcode(tabitem->backhandle)) >= 0) {
	/* Abnormal exits will already have set session_closed and taken
	 * appropriate action. */
	if (tabitem->cfg.close_on_exit == FORCE_ON ||
	    (tabitem->cfg.close_on_exit == AUTO && exitcode != INT_MAX)) {
	    wintabitem_close_session(tabitem);
	    //PostQuitMessage(0);
	} else {
	    tabitem->must_close_session = TRUE;
	    tabitem->session_closed = TRUE;
	    /* exitcode == INT_MAX indicates that the connection was closed
	     * by a fatal error, so an error box will be coming our way and
	     * we should not generate this informational one. */
	    if (exitcode != INT_MAX){
		MessageBox(hwnd, "Connection closed by remote host",
			   appname, MB_OK | MB_ICONINFORMATION);
	    }
	}
    }
}

void timer_change_notify(long next)
{
    long ticks = next - GETTICKCOUNT();
    if (ticks <= 0) ticks = 1;	       /* just in case */
    KillTimer(hwnd, TIMING_TIMER_ID);
    SetTimer(hwnd, TIMING_TIMER_ID, ticks, NULL);
    timing_next_time = next;
}

int on_timer(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if ((UINT_PTR)wParam == TIMING_TIMER_ID) {
	    long next;

	    KillTimer(hwnd, TIMING_TIMER_ID);
	    if (run_timers(timing_next_time, &next)) {
    		timer_change_notify(next);
	    } else {
	    }
	}
    return 0;
}

int on_close(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    //todo
    char *str;
    show_mouseptr(wintab_get_active_item(&tab), 1);
    str = dupprintf("%s Exit Confirmation", appname);
    if ( wintab_can_close(&tab)||
            MessageBox(hwnd,
            	   "Are you sure you want to close all the sessions?",
            	   str, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1)
            == IDOK){
        DestroyWindow(hwnd);
    }
    sfree(str);
    return 0;
}

int on_init_menu_popup(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if ((HMENU)wParam == savedsess_menu) {
	    /* About to pop up Saved Sessions sub-menu.
	     * Refresh the session list. */
	    get_sesslist(&sesslist, FALSE); /* free */
	    get_sesslist(&sesslist, TRUE);
	    update_savedsess_menu();
	    return 0;
	}
    return 0;
}

int on_session_menu(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if (wParam == IDM_DUPSESS) {
        wintabitem* tabitem = wintab_get_active_item(&tab);
        assert (tabitem != NULL);
        cfg = tabitem->cfg;
        wintab_create_tab(&tab, &cfg);
        return 0;
    } else if (wParam == IDM_SAVEDSESS) {
        //noodle: please try reconfigure
    	return 0;
    } else /* IDM_NEWSESS */ {
        if (!do_config())
            return -1;
        wintab_create_tab(&tab, &cfg);
        return 0;
    }
    return 0;
}

int on_reconfig(wintabitem* tabitem, UINT message,
				WPARAM wParam, LPARAM lParam)
{
	Config prev_cfg;
	int init_lvl = 1;
	int reconfig_result;

	if (reconfiguring)
	    return 0;
	else
	    reconfiguring = TRUE;

	//GetWindowText(hwnd, cfg.wintitle, sizeof(cfg.wintitle));
	prev_cfg = tabitem->cfg;
    cfg = tabitem->cfg;

	reconfig_result =
	    do_reconfig(hwnd, tabitem->back ? tabitem->back->cfg_info(tabitem->backhandle) : 0);
	reconfiguring = FALSE;
	if (!reconfig_result)
	    return 0;
    
    tabitem->cfg = cfg;
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
	log_reconfig(tabitem->logctx, &cfg);

	sfree(tabitem->logpal);
	/*
	 * Flush the line discipline's edit buffer in the
	 * case where local editing has just been disabled.
	 */
	if (tabitem->ldisc)
	    ldisc_send(tabitem->ldisc, NULL, 0, 0);
	if (tabitem->pal)
	    DeleteObject(tabitem->pal);
	tabitem->logpal = NULL;
	tabitem->pal = NULL;
	wintabitem_cfgtopalette(tabitem);
	wintabitem_init_palette(tabitem);

	/* Pass new config data to the terminal */
	term_reconfig(tabitem->term, &tabitem->cfg);

	/* Pass new config data to the back end */
	if (tabitem->back)
	    tabitem->back->reconfig(tabitem->backhandle, &tabitem->cfg);

	/* Screen size changed ? */
	if (tabitem->cfg.height != prev_cfg.height ||
	    tabitem->cfg.width != prev_cfg.width ||
	    tabitem->cfg.savelines != prev_cfg.savelines ||
	    tabitem->cfg.resize_action == RESIZE_FONT ||
	    (tabitem->cfg.resize_action == RESIZE_EITHER && IsZoomed(hwnd)) ||
	    tabitem->cfg.resize_action == RESIZE_DISABLED)
	    term_size(tabitem->term, tabitem->cfg.height, tabitem->cfg.width, tabitem->cfg.savelines);

	/* Enable or disable the scroll bar, etc */
	{
	    LONG nflg, flag = GetWindowLongPtr(hwnd, GWL_STYLE);
	    LONG nexflag, exflag =
		GetWindowLongPtr(hwnd, GWL_EXSTYLE);

	    nexflag = exflag;
        nflg = flag;
	    if (tabitem->cfg.alwaysontop != prev_cfg.alwaysontop) {
    		if (tabitem->cfg.alwaysontop) {
    		    nexflag |= WS_EX_TOPMOST;
    		    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
    				 SWP_NOMOVE | SWP_NOSIZE);
    		} else {
    		    nexflag &= ~(WS_EX_TOPMOST);
    		    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
    				 SWP_NOMOVE | SWP_NOSIZE);
    		}
	    }

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
        LONG npflg, pflag = GetWindowLongPtr(tabitem->page.hwndCtrl, GWL_STYLE);
        npflg = pflag;
	    if (is_full_screen() ?
    		tabitem->cfg.scrollbar_in_fullscreen : tabitem->cfg.scrollbar)
		npflg |= WS_VSCROLL;
	    else
		npflg &= ~WS_VSCROLL;
        if (npflg != pflag)
		    SetWindowLongPtr(tabitem->page.hwndCtrl, GWL_STYLE, nflg);
	}

	/* Oops */
	if (tabitem->cfg.resize_action == RESIZE_DISABLED && IsZoomed(hwnd)) {
	    force_normal(hwnd);
	    init_lvl = 2;
	}

	//set_title(tabitem, tabitem->cfg.wintitle);
	if (IsIconic(hwnd)) {
	    SetWindowText(hwnd,
			  tabitem->cfg.win_name_always ? tabitem->window_name :
			  tabitem->icon_name);
	}

	if (strcmp(tabitem->cfg.font.name, prev_cfg.font.name) != 0 ||
	    strcmp(tabitem->cfg.line_codepage, prev_cfg.line_codepage) != 0 ||
	    tabitem->cfg.font.isbold != prev_cfg.font.isbold ||
	    tabitem->cfg.font.height != prev_cfg.font.height ||
	    tabitem->cfg.font.charset != prev_cfg.font.charset ||
	    tabitem->cfg.font_quality != prev_cfg.font_quality ||
	    tabitem->cfg.vtmode != prev_cfg.vtmode ||
	    tabitem->cfg.bold_colour != prev_cfg.bold_colour ||
	    tabitem->cfg.resize_action == RESIZE_DISABLED ||
	    tabitem->cfg.resize_action == RESIZE_EITHER ||
	    (tabitem->cfg.resize_action != prev_cfg.resize_action))
	    init_lvl = 2;

	InvalidateRect(hwnd, NULL, TRUE);
	reset_window(tabitem, init_lvl);
	net_pending_errors();
	    

    return 0;
}


int on_menu(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
        case IDM_SHOWLOG:
            showeventlog(tabitem, hwnd);
            break;
        case IDM_NEWSESS:
        case IDM_DUPSESS:
        case IDM_SAVEDSESS:
            on_session_menu(hwnd, message, wParam, lParam);
            break;
        case IDM_RESTART:{
            char *str;
            show_mouseptr(tabitem, 1);
            str = dupprintf("%s Exit Confirmation", tabitem->cfg.host);
            if (!( wintabitem_can_close(tabitem)||
                    MessageBox(hwnd,
                    	   "Are you sure you want to close this session?",
                    	   str, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1)
                    == IDOK)){
                break;
            }
            wintabitem_close_session(tabitem);
            if (!tabitem->back) {
            logevent(tabitem, "----- Session restarted -----");
            term_pwron(tabitem->term, FALSE);
            wintabitem_start_backend(tabitem);
            }
            break;
        }
        case IDM_RECONF:
            on_reconfig(tabitem, message, wParam, lParam);
            break;
        case IDM_COPYALL:
            term_copyall(tabitem->term);
            break;
        case IDM_PASTE:
            request_paste(tabitem);
            break;
        case IDM_CLRSB:
            term_clrsb(tabitem->term);
            break;
        case IDM_RESET:
            term_pwron(tabitem->term, TRUE);
            if (tabitem->ldisc)
            ldisc_send(tabitem->ldisc, NULL, 0, 0);
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
            show_mouseptr(wintab_get_active_item(&tab), 1);
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
            show_mouseptr(wintab_get_active_item(&tab), 1);	       /* make sure pointer is visible */
            if( lParam == 0 )
                PostMessage(tabitem->page.hwndCtrl, WM_CHAR, ' ', 0);
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
                if (i >= tabitem->n_specials)
                    break;
                if (tabitem->back)
                    tabitem->back->special(tabitem->backhandle, tabitem->specials[i].code);
                net_pending_errors();
            }
	}
    return 0;
}

#define X_POS(l) ((int)(short)LOWORD(l))
#define Y_POS(l) ((int)(short)HIWORD(l))

#define TO_CHR_X(x) ((((x)<0 ? (x)-tabitem->font_width+1 : (x))-tabitem->offset_width) / tabitem->font_width)
#define TO_CHR_Y(y) ((((y)<0 ? (y)-tabitem->font_height+1: (y))-tabitem->offset_height) / tabitem->font_height)
 
int on_button(wintabitem* tabitem, HWND hWnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    SetFocus(hwnd);
    if (message == WM_RBUTTONDOWN &&
    	    ((wParam & MK_CONTROL) || (tabitem->cfg.mouse_is_xterm == 2))) {
	    POINT cursorpos;

	    show_mouseptr(tabitem, 1);	       /* make sure pointer is visible */
	    GetCursorPos(&cursorpos);
	    TrackPopupMenu(popup_menus[CTXMENU].menu,
			   TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			   cursorpos.x, cursorpos.y,
			   0, hWnd, NULL);
	    return 0;
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
	    show_mouseptr(tabitem, 1);
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
		if (is_full_screen() && press &&
		    button == MBT_LEFT && mouse_on_hotspot) {
		    SendMessage(hWnd, WM_SYSCOMMAND, SC_MOUSEMENU,
				MAKELPARAM(pt.x, pt.y));
		    return 0;
		}
	    }

	    if (press) {
		click(tabitem, button,
		      TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)),
		      wParam & MK_SHIFT, wParam & MK_CONTROL,
		      is_alt_pressed());
		SetCapture(tabitem->page.hwndCtrl);
	    } else {
		term_mouse(tabitem->term, button, translate_button(tabitem, button), MA_RELEASE,
			   TO_CHR_X(X_POS(lParam)),
			   TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
			   wParam & MK_CONTROL, is_alt_pressed());
		if (!(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
		    ReleaseCapture();
	    }
	}
    return 0;

}

int on_mouse_move(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
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
		show_mouseptr(tabitem, 1);
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
	    GetCapture() == tabitem->page.hwndCtrl) {
	    Mouse_Button b;
	    if (wParam & MK_LBUTTON)
		b = MBT_LEFT;
	    else if (wParam & MK_MBUTTON)
		b = MBT_MIDDLE;
	    else
		b = MBT_RIGHT;
	    term_mouse(tabitem->term, b, translate_button(tabitem, b), MA_DRAG,
		       TO_CHR_X(X_POS(lParam)),
		       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
		       wParam & MK_CONTROL, is_alt_pressed());
	}
    return 0;
}

int on_nc_mouse_move(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    {
	    static WPARAM wp = 0;
	    static LPARAM lp = 0;
	    if (wParam != wp || lParam != lp ||
		last_mousemove != WM_NCMOUSEMOVE) {
		show_mouseptr(tabitem, 1);
		wp = wParam; lp = lParam;
		last_mousemove = WM_NCMOUSEMOVE;
	    }
	}
	noise_ultralight(lParam);
    return 0;
}

int on_net_event(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
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
}
int on_set_focus(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    term_set_focus(tabitem->term, TRUE);
	CreateCaret(tabitem->page.hwndCtrl, tabitem->caretbm, tabitem->font_width, tabitem->font_height);
	ShowCaret(tabitem->page.hwndCtrl);
	flash_window(0);	       /* stop */
	tabitem->compose_state = 0;
	term_update(tabitem->term);
	return 0;
}

int on_kill_focus(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
	show_mouseptr(tabitem, 1);
	term_set_focus(tabitem->term, FALSE);
	DestroyCaret();
	tabitem->caret_x = tabitem->caret_y = -1;	       /* ensure caret is replaced next time */
	term_update(tabitem->term);
    return 0;
}

int on_sizing(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    int ex_width, ex_height;
    wintabitem_get_extra_size(tabitem, &ex_width, &ex_height);
    /*
	 * This does two jobs:
	 * 1) Keep the sizetip uptodate
	 * 2) Make sure the window size is _stepped_ in units of the font size.
	 */
    if (tabitem->cfg.resize_action == RESIZE_TERM ||
        (tabitem->cfg.resize_action == RESIZE_EITHER && !is_alt_pressed())) {
	    int width, height, w, h, ew, eh;
	    LPRECT r = (LPRECT) lParam;
        RECT rc;
        GetClientRect(hwnd, &rc); 
        wintab_resize(tabitem->parentTab, &rc);

	    if ( !need_backend_resize && tabitem->cfg.resize_action == RESIZE_EITHER &&
		    (tabitem->cfg.height != tabitem->term->rows || tabitem->cfg.width != tabitem->term->cols )) {
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
	        tabitem->cfg.height=tabitem->term->rows; tabitem->cfg.width=tabitem->term->cols;

    		InvalidateRect(hwnd, NULL, TRUE);
    		need_backend_resize = TRUE;
	    }

        //calc term size
	    width = r->right - r->left - ex_width;
	    height = r->bottom - r->top - ex_height;
	    w = (width + tabitem->font_width / 2) / tabitem->font_width;
	    if (w < 1)
    		w = 1;
	    h = (height + tabitem->font_height / 2) / tabitem->font_height;
	    if (h < 1)
    		h = 1;
	    UpdateSizeTip(hwnd, w, h);
        
        //calc the grap if any
	    ew = width - w * tabitem->font_width;
	    eh = height - h * tabitem->font_height;
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
	    LPRECT r = (LPRECT) lParam;

	    width = r->right - r->left - ex_width;
	    height = r->bottom - r->top - ex_height;

	    w = (width + tabitem->term->cols/2)/tabitem->term->cols;
	    h = (height + tabitem->term->rows/2)/tabitem->term->rows;
	    if ( r->right != r->left + w*tabitem->term->cols + ex_width)
		rv = 1;

	    if (wParam == WMSZ_LEFT ||
		wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
		r->left = r->right - w*tabitem->term->cols - ex_width;
	    else
		r->right = r->left + w*tabitem->term->cols + ex_width;

	    if (r->bottom != r->top + h*tabitem->term->rows + ex_height)
		rv = 1;

	    if (wParam == WMSZ_TOP ||
		wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
		r->top = r->bottom - h*tabitem->term->rows - ex_height;
	    else
		r->bottom = r->top + h*tabitem->term->rows + ex_height;

	    return rv;
	}
    return 0;
}

int on_size(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    wintab_onsize(&tab, hwnd, lParam);
	if (wParam == SIZE_MINIMIZED)
	    SetWindowText(hwnd,
			  tabitem->cfg.win_name_always ? tabitem->window_name : tabitem->icon_name);
	if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
	    SetWindowText(hwnd, tabitem->window_name);
        if (wParam == SIZE_RESTORED) {
            processed_resize = FALSE;
            clear_full_screen(tabitem);
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
            make_full_screen(tabitem);
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

	if (tabitem->cfg.resize_action == RESIZE_DISABLED) {
	    /* A resize, well it better be a minimize. */
	    reset_window(tabitem, -1);
	} else {

	    int width, height, w, h;

	    wintabpage_get_term_size(&tabitem->page, &width, &height);

        if (wParam == SIZE_MAXIMIZED && !was_zoomed) {
                was_zoomed = 1;
                tabitem->prev_rows = tabitem->term->rows;
                tabitem->prev_cols = tabitem->term->cols;
                if (tabitem->cfg.resize_action == RESIZE_TERM) {
                    w = width / tabitem->font_width;
                    if (w < 1) w = 1;
                    h = height / tabitem->font_height;
                    if (h < 1) h = 1;

                    term_size(tabitem->term, h, w, tabitem->cfg.savelines);
                }
                reset_window(tabitem, 0);
        } else if (wParam == SIZE_RESTORED && was_zoomed) {
                was_zoomed = 0;
                if (tabitem->cfg.resize_action == RESIZE_TERM) {
                    w = width/ tabitem->font_width;
                    if (w < 1) w = 1;
                    h = height / tabitem->font_height;
                    if (h < 1) h = 1;
                    term_size(tabitem->term, h, w, tabitem->cfg.savelines);
                    reset_window(tabitem, 2);
                } else if (tabitem->cfg.resize_action != RESIZE_FONT)
                    reset_window(tabitem, 2);
                else
                    reset_window(tabitem, 0);
        } else if (wParam == SIZE_MINIMIZED) {
                /* do nothing */
	    } else if (tabitem->cfg.resize_action == RESIZE_TERM ||
                       (tabitem->cfg.resize_action == RESIZE_EITHER &&
                        !is_alt_pressed())) {
                w = width / tabitem->font_width;
                if (w < 1) w = 1;
                h = height / tabitem->font_height;
                if (h < 1) h = 1;

                if (resizing) {
                    /*
                     * Don't call back->size in mid-resize. (To
                     * prevent massive numbers of resize events
                     * getting sent down the connection during an NT
                     * opaque drag.)
                     */
		    need_backend_resize = TRUE;
		    tabitem->cfg.height = h;
		    tabitem->cfg.width = w;
                } else {
                    term_size(tabitem->term, h, w, tabitem->cfg.savelines);
                }
            } else {
                reset_window(tabitem, 0);
	    }
	}
	sys_cursor_update(tabitem);
    return 0;

}

int on_palette_changed(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if ((HWND) wParam != hwnd && tabitem->pal != NULL) {
        wintabitem *item = get_ctx(tabitem);
	    if (item && item->hdc) {
    		if (RealizePalette(item->hdc) > 0)
    		    UpdateColors(item->hdc);
    		free_ctx(item, item);
	    }
	}
    return 0;
}

int on_query_new_palette(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if (tabitem->pal != NULL) {
        wintabitem *item = get_ctx(tabitem);
	    if (item && item->hdc) {
		if (RealizePalette(item->hdc) > 0)
		    UpdateColors(item->hdc);
		free_ctx(item, item);
		return TRUE;
	    }
	}
	return FALSE;
}


int on_key(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
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
		} else return 1; /* pass to Windows for default processing */
	    } else {
		len = TranslateKey(tabitem, message, wParam, lParam, buf);
		if (len == -1)
		    return DefWindowProc(hwnd, message, wParam, lParam);

		if (len != 0) {
		    /*
		     * Interrupt an ongoing paste. I'm not sure
		     * this is sensible, but for the moment it's
		     * preferable to having to faff about buffering
		     * things.
		     */
		    term_nopaste(tabitem->term);

		    /*
		     * We need not bother about stdin backlogs
		     * here, because in GUI PuTTY we can't do
		     * anything about it anyway; there's no means
		     * of asking Windows to hold off on KEYDOWN
		     * messages. We _have_ to buffer everything
		     * we're sent.
		     */
		    term_seen_key_event(tabitem->term);
		    if (tabitem->ldisc)
			ldisc_send(tabitem->ldisc, buf, len, 1);
		    show_mouseptr(tabitem, 0);
		}
	    }
	}
	net_pending_errors();
    return 0;
}

int on_ime_composition(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    HIMC hIMC;
    int n;
    char *buff;

    if(osVersion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS || 
        osVersion.dwPlatformId == VER_PLATFORM_WIN32s) return 1; /* no Unicode */

    if ((lParam & GCS_RESULTSTR) == 0) /* Composition unfinished. */
    	return 1; /* fall back to DefWindowProc */

    hIMC = ImmGetContext(tabitem->page.hwndCtrl);
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
	term_seen_key_event(tabitem->term);
	for (i = 0; i < n; i += 2) {
	    if (tabitem->ldisc)
		luni_send(tabitem->ldisc, (unsigned short *)(buff+i), 1, 1);
	}
	free(buff);
    }
    ImmReleaseContext(tabitem->page.hwndCtrl, hIMC);
    return 0;
}

int on_ime_char(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if (wParam & 0xFF00) {
	    unsigned char buf[2];

	    buf[1] = wParam;
	    buf[0] = wParam >> 8;
	    term_seen_key_event(tabitem->term);
	    if (tabitem->ldisc)
		lpage_send(tabitem->ldisc, kbd_codepage, buf, 2, 1);
	} else {
	    char c = (unsigned char) wParam;
	    term_seen_key_event(tabitem->term);
	    if (tabitem->ldisc)
		lpage_send(tabitem->ldisc, kbd_codepage, &c, 1, 1);
	}
    return 0;
}

int on_char(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    /*
	 * Nevertheless, we are prepared to deal with WM_CHAR
	 * messages, should they crop up. So if someone wants to
	 * post the things to us as part of a macro manoeuvre,
	 * we're ready to cope.
	 */
	{
	    char c = (unsigned char)wParam;
	    term_seen_key_event(tabitem->term);
	    if (tabitem->ldisc)
		lpage_send(tabitem->ldisc, CP_ACP, &c, 1, 1);
	}
    return 0;
}

int on_sys_color_change(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if (tabitem->cfg.system_colour) {
	    /* Refresh palette from system colours. */
	    /* XXX actually this zaps the entire palette. */
	    wintabitem_systopalette(tabitem);
	    wintabitem_init_palette(tabitem);
	    /* Force a repaint of the terminal window. */
	    term_invalidate(tabitem->term);
	}
    return 0;
}

int on_default(wintabitem* tabitem, HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    if (!initialized) return 0;
    if (message == tabitem->wm_mousewheel || message == WM_MOUSEWHEEL) {
	    int shift_pressed=0, control_pressed=0;

	    if (message == WM_MOUSEWHEEL) {
		tabitem->wheel_accumulator += (short)HIWORD(wParam);
		shift_pressed=LOWORD(wParam) & MK_SHIFT;
		control_pressed=LOWORD(wParam) & MK_CONTROL;
	    } else {
		BYTE keys[256];
		tabitem->wheel_accumulator += (int)wParam;
		if (GetKeyboardState(keys)!=0) {
		    shift_pressed=keys[VK_SHIFT]&0x80;
		    control_pressed=keys[VK_CONTROL]&0x80;
		}
	    }

	    /* process events when the threshold is reached */
	    while (abs(tabitem->wheel_accumulator) >= WHEEL_DELTA) {
		int b;

		/* reduce amount for next time */
		if (tabitem->wheel_accumulator > 0) {
		    b = MBT_WHEEL_UP;
		    tabitem->wheel_accumulator -= WHEEL_DELTA;
		} else if (tabitem->wheel_accumulator < 0) {
		    b = MBT_WHEEL_DOWN;
		    tabitem->wheel_accumulator += WHEEL_DELTA;
		} else
		    break;

		if (tabitem->send_raw_mouse &&
		    !(tabitem->cfg.mouse_override && shift_pressed)) {
		    /* Mouse wheel position is in screen coordinates for
		     * some reason */
		    POINT p;
		    p.x = X_POS(lParam); p.y = Y_POS(lParam);
		    if (ScreenToClient(hwnd, &p)) {
			/* send a mouse-down followed by a mouse up */
			term_mouse(tabitem->term, b, translate_button(tabitem, b),
				   MA_CLICK,
				   TO_CHR_X(p.x),
				   TO_CHR_Y(p.y), shift_pressed,
				   control_pressed, is_alt_pressed());
			term_mouse(tabitem->term, b, translate_button(tabitem, b),
				   MA_RELEASE, TO_CHR_X(p.x),
				   TO_CHR_Y(p.y), shift_pressed,
				   control_pressed, is_alt_pressed());
		    } /* else: not sure when this can fail */
		} else {
		    /* trigger a scroll */
		    int scrollLines = tabitem->cfg.scrolllines == -1 ? tabitem->term->rows/2
		            : tabitem->cfg.scrolllines == -2          ? tabitem->term->rows
		            : tabitem->cfg.scrolllines < -2            ? 3
		            : tabitem->cfg.scrolllines;
		    term_scroll(tabitem->term, 0,
				b == MBT_WHEEL_UP ?
				-scrollLines : scrollLines);
		}
	    }
	    return 0;
	}

    return 0;
}
static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    debug(("[WndProc]%s:%s\n", hwnd == hwnd ? "DialogMsg"
                            :hwnd == tab.hwndTab ? "TabBarMsg"
                            :hwnd == tab.items[0]->page.hwndCtrl ? "PageMsg"
                            : "UnknowMsg", TranslateWMessage(message)));

    wintabitem *tabitem = wintab_get_active_item(&tab);

  
    switch (message) {
        case WM_GETMINMAXINFO:  
        {   
            RECT WorkArea;
            HMONITOR mon;
        	MONITORINFO mi;
        	mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        	mi.cbSize = sizeof(mi);
        	GetMonitorInfo(mon, &mi);

        	/* structure copy */
        	WorkArea = fullscr_on_max ? mi.rcMonitor: mi.rcWork;
            
            /*
            RECT cli_rc, pri_rc;
            SystemParametersInfo( SPI_GETWORKAREA, 0, &pri_rc, 0 ); 
            GetWindowRect(GetDesktopWindow(), &cli_rc);
            POINT pt = {(pri_rc.left + pri_rc.right)/2, (pri_rc.top + pri_rc.bottom)/2};
            WorkArea = (PtInRect(&cli_rc, pt)) ? pri_rc : cli_rc;  
            */
            ( ( MINMAXINFO * )lParam )->ptMaxSize.x = ( WorkArea.right - WorkArea.left );  
            ( ( MINMAXINFO * )lParam )->ptMaxSize.y = ( WorkArea.bottom - WorkArea.top );  
            ( ( MINMAXINFO * )lParam )->ptMaxPosition.x = 0;//WorkArea.left;  
            ( ( MINMAXINFO * )lParam )->ptMaxPosition.y = 0;//WorkArea.top;  
            return 0;  
        }
        case WM_TIMER:
            on_timer(hwnd, message, wParam, lParam);
	        return 0;
        case WM_CREATE:
        	break;
        case WM_CLOSE:
            on_close(hwnd, message, wParam, lParam);
            return 0;
        case WM_DESTROY:
            show_mouseptr(tabitem, 1);
            PostQuitMessage(0);
            return 0;
        case WM_INITMENUPOPUP:
            on_init_menu_popup(hwnd, message, wParam, lParam);
            break;
        case WM_COMMAND:
        case WM_SYSCOMMAND:
            on_menu(tabitem, hwnd, message, wParam, lParam);
            break;

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
            PostMessage(tab.hwndTab, message, wParam, lParam);
            return 0;
	        //on_button(tabitem, hwnd, message, wParam, lParam);
    		//return 0;
        case WM_MOUSEMOVE:
            //on_mouse_move(tabitem, hwnd, message, wParam, lParam);
        	//return 0;
        case WM_NCMOUSEMOVE:
        	//on_nc_mouse_move(tabitem, hwnd, message, wParam, lParam);
        	break;
        case WM_IGNORE_CLIP:
        	//ignore_clip = wParam;	       /* don't panic on DESTROYCLIPBOARD */
        	break;
        case WM_DESTROYCLIPBOARD:
        	//if (!ignore_clip)
        	//    term_deselect(tabitem->term);
        	//ignore_clip = FALSE;
        	return 0;
        case WM_PAINT:
            //wintabitem_on_paint(tabitem, hwnd, message,wParam, lParam);
    		break;
        case WM_NETEVENT:
            on_net_event(tabitem, hwnd, message,wParam, lParam);
        	return 0;
        case WM_SETFOCUS:
        	on_set_focus(tabitem, hwnd, message,wParam, lParam);
        	break;
        case WM_KILLFOCUS:
        	on_kill_focus(tabitem, hwnd, message,wParam, lParam);
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
        	    term_size(tabitem->term, tabitem->cfg.height, tabitem->cfg.width, tabitem->cfg.savelines);
        	    InvalidateRect(hwnd, NULL, TRUE);
        	}
        	break;
        case WM_SIZING:
            return on_sizing(tabitem, hwnd, message,wParam, lParam);
        	/* break;  (never reached) */
        case WM_FULLSCR_ON_MAX:
        	fullscr_on_max = TRUE;
        	break;
        case WM_MOVE:
        	sys_cursor_update(tabitem);
        	break;
        case WM_SIZE:
            on_size(tabitem, hwnd, message,wParam, lParam);
        	return 0;
        case WM_PALETTECHANGED:
	        on_palette_changed(tabitem, hwnd, message,wParam, lParam);
        	break;
        case WM_QUERYNEWPALETTE:
	        return on_query_new_palette(tabitem, hwnd, message,wParam, lParam);
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
	        if (on_key(tabitem, hwnd, message,wParam, lParam))
                break;
        	return 0;
        case WM_INPUTLANGCHANGE:
        	/* wParam == Font number */
        	/* lParam == Locale */
        	set_input_locale((HKL)lParam);
        	sys_cursor_update(tabitem);
        	break;
        case WM_IME_STARTCOMPOSITION:
        	{
        	    HIMC hImc = ImmGetContext(hwnd);
        	    ImmSetCompositionFont(hImc, &tabitem->lfont);
        	    ImmReleaseContext(hwnd, hImc);
        	}
        	break;
        case WM_IME_COMPOSITION:
            if (on_ime_composition(tabitem, hwnd, message,wParam, lParam))
                break;
            return 1;

        case WM_IME_CHAR:
            on_ime_char(tabitem, hwnd, message,wParam, lParam);
            return (0);
        case WM_CHAR:
        case WM_SYSCHAR:
	        on_char(tabitem, hwnd, message,wParam, lParam);
        	return 0;
        case WM_SYSCOLORCHANGE:
	        on_sys_color_change(tabitem, hwnd, message,wParam, lParam);
        	break;
        case WM_AGENT_CALLBACK:
        	{
        	    struct agent_callback *c = (struct agent_callback *)lParam;
        	    c->callback(c->callback_ctx, c->data, c->len);
        	    sfree(c);
        	}
        	return 0;
        case WM_GOT_CLIPDATA:
        	//if (process_clipdata((HGLOBAL)lParam, wParam))
    	    //term_do_paste(tabitem->term);
        	return 0;
        case WM_NOTIFY:
            if (((LPNMHDR) lParam)->hwndFrom != tab.hwndTab)
                break;
            switch(((LPNMHDR) lParam)->code){
        		case TCN_SELCHANGE:
                	wintab_swith_tab(&tab);
        			break;
            }
            break; 
        default:
        	on_default(tabitem, hwnd, message,wParam, lParam);
            break;
    }

    /*
     * Any messages we don't process completely above are passed through to
     * DefWindowProc() for default processing.
     */
    return DefWindowProc(hwnd, message, wParam, lParam);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
void sys_cursor(void *frontend, int x, int y)
{
    assert(frontend != NULL);
    int cx, cy;

    wintabitem *tabitem = (wintabitem*) frontend;

    if (!tabitem->term->has_focus) return;

    /*
     * Avoid gratuitously re-updating the cursor position and IMM
     * window if there's no actual change required.
     */
    cx = x * tabitem->font_width + tabitem->offset_width;
    cy = y * tabitem->font_height + tabitem->offset_height;
    if (cx == tabitem->caret_x && cy == tabitem->caret_y)
	return;
    tabitem->caret_x = cx;
    tabitem->caret_y = cy;

    sys_cursor_update(tabitem);
}

static void sys_cursor_update(wintabitem *tabitem)
{
    COMPOSITIONFORM cf;
    HIMC hIMC;

    if (!tabitem->term->has_focus) return;

    if (tabitem->caret_x < 0 || tabitem->caret_y < 0)
	return;

    SetCaretPos(tabitem->caret_x, tabitem->caret_y);

    /* IMM calls on Win98 and beyond only */
    if(osVersion.dwPlatformId == VER_PLATFORM_WIN32s) return; /* 3.11 */
    
    if(osVersion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
	    osVersion.dwMinorVersion == 0) return; /* 95 */

    /* we should have the IMM functions */
    hIMC = ImmGetContext(tabitem->page.hwndCtrl);
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = tabitem->caret_x;
    cf.ptCurrentPos.y = tabitem->caret_y;
    ImmSetCompositionWindow(hIMC, &cf);

    ImmReleaseContext(tabitem->page.hwndCtrl, hIMC);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text_internal(Context ctx, int x, int y, wchar_t *text, int len,
		      unsigned long attr, int lattr)
{
    assert(ctx != NULL);
    wintabitem *tabitem = (wintabitem *)ctx;
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    HDC hdc = tabitem->hdc;
    RECT line_box;
    int force_manual_underline = 0;
    int fnt_width, char_width;
    int text_adjust = 0;
    int xoffset = 0;
    int maxlen, remaining, opaque;
    static int *lpDx = NULL;
    static int lpDx_len = 0;
    int *lpDx_maybe;

    assert (hdc != NULL);

    lattr &= LATTR_MODE;

    char_width = fnt_width = tabitem->font_width * (1 + (lattr != LATTR_NORM));

    if (attr & ATTR_WIDE)
	char_width *= 2;

    /* Only want the left half of double width lines */
    if (lattr != LATTR_NORM && x*2 >= tabitem->term->cols)
	return;

    x *= fnt_width;
    y *= tabitem->font_height;
    x += tabitem->offset_width;
    y += tabitem->offset_height;

    if ((attr & TATTR_ACTCURS) && (tabitem->cfg.cursor_type == 0 || tabitem->term->big_cursor)) {
	attr &= ~(ATTR_REVERSE|ATTR_BLINK|ATTR_COLOURS);
	if (tabitem->bold_mode == BOLD_COLOURS)
	    attr &= ~ATTR_BOLD;

	/* cursor fg and bg */
	attr |= (260 << ATTR_FGSHIFT) | (261 << ATTR_BGSHIFT);
    }

    nfont = 0;
    if (tabitem->cfg.vtmode == VT_POORMAN && lattr != LATTR_NORM) {
	/* Assume a poorman font is borken in other ways too. */
	lattr = LATTR_WIDE;
    } else
	switch (lattr) {
	  case LATTR_NORM:
	    break;
	  case LATTR_WIDE:
	    nfont |= FONT_WIDE;
	    break;
	  default:
	    nfont |= FONT_WIDE + FONT_HIGH;
	    break;
	}
    if (attr & ATTR_NARROW)
	nfont |= FONT_NARROW;

    /* Special hack for the VT100 linedraw glyphs. */
    if (text[0] >= 0x23BA && text[0] <= 0x23BD) {
	switch ((unsigned char) (text[0])) {
	  case 0xBA:
	    text_adjust = -2 * tabitem->font_height / 5;
	    break;
	  case 0xBB:
	    text_adjust = -1 * tabitem->font_height / 5;
	    break;
	  case 0xBC:
	    text_adjust = tabitem->font_height / 5;
	    break;
	  case 0xBD:
	    text_adjust = 2 * tabitem->font_height / 5;
	    break;
	}
	if (lattr == LATTR_TOP || lattr == LATTR_BOT)
	    text_adjust *= 2;
	text[0] = tabitem->ucsdata.unitab_xterm['q'];
	if (attr & ATTR_UNDER) {
	    attr &= ~ATTR_UNDER;
	    force_manual_underline = 1;
	}
    }

    /* Anything left as an original character set is unprintable. */
    if (DIRECT_CHAR(text[0])) {
	int i;
	for (i = 0; i < len; i++)
	    text[i] = 0xFFFD;
    }

    /* OEM CP */
    if ((text[0] & CSET_MASK) == CSET_OEMCP)
	nfont |= FONT_OEM;

    nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (tabitem->bold_mode == BOLD_FONT && (attr & ATTR_BOLD))
	nfont |= FONT_BOLD;
    if (tabitem->und_mode == UND_FONT && (attr & ATTR_UNDER))
	nfont |= FONT_UNDERLINE;
    another_font(ctx, nfont);
    if (!tabitem->fonts[nfont]) {
	if (nfont & FONT_UNDERLINE)
	    force_manual_underline = 1;
	/* Don't do the same for manual bold, it could be bad news. */

	nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
    }
    another_font(ctx, nfont);
    if (!tabitem->fonts[nfont])
	nfont = FONT_NORMAL;
    if (attr & ATTR_REVERSE) {
	t = nfg;
	nfg = nbg;
	nbg = t;
    }
    if (tabitem->bold_mode == BOLD_COLOURS && (attr & ATTR_BOLD)) {
	if (nfg < 16) nfg |= 8;
	else if (nfg >= 256) nfg |= 1;
    }
    if (tabitem->bold_mode == BOLD_COLOURS && (attr & ATTR_BLINK)) {
	if (nbg < 16) nbg |= 8;
	else if (nbg >= 256) nbg |= 1;
    }
    fg = tabitem->colours[nfg];
    bg = tabitem->colours[nbg];
    SelectObject(hdc, tabitem->fonts[nfont]);
    SetTextColor(hdc, fg);
    SetBkColor(hdc, bg);
    if (attr & TATTR_COMBINING)
	SetBkMode(hdc, TRANSPARENT);
    else
	SetBkMode(hdc, OPAQUE);
    line_box.left = x;
    line_box.top = y;
    line_box.right = x + char_width * len;
    line_box.bottom = y + tabitem->font_height;

    /* Only want the left half of double width lines */
    if (line_box.right > tabitem->font_width*tabitem->term->cols+tabitem->offset_width)
	line_box.right = tabitem->font_width*tabitem->term->cols+tabitem->offset_width;

    if (tabitem->font_varpitch) {
        /*
         * If we're using a variable-pitch font, we unconditionally
         * draw the glyphs one at a time and centre them in their
         * character cells (which means in particular that we must
         * disable the lpDx mechanism). This gives slightly odd but
         * generally reasonable results.
         */
        xoffset = char_width / 2;
        SetTextAlign(hdc, TA_TOP | TA_CENTER | TA_NOUPDATECP);
        lpDx_maybe = NULL;
        maxlen = 1;
    } else {
        /*
         * In a fixed-pitch font, we draw the whole string in one go
         * in the normal way.
         */
        xoffset = 0;
        SetTextAlign(hdc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        lpDx_maybe = lpDx;
        maxlen = len;
    }

    opaque = TRUE;                     /* start by erasing the rectangle */
    for (remaining = len; remaining > 0;
         text += len, remaining -= len, x += char_width * len) {
        len = (maxlen < remaining ? maxlen : remaining);

        if (len > lpDx_len) {
            if (len > lpDx_len) {
                lpDx_len = len * 9 / 8 + 16;
                lpDx = sresize(lpDx, lpDx_len, int);
            }
        }
        {
            int i;
            for (i = 0; i < len; i++)
                lpDx[i] = char_width;
        }

        /* We're using a private area for direct to font. (512 chars.) */
        if (tabitem->ucsdata.dbcs_screenfont && (text[0] & CSET_MASK) == CSET_ACP) {
            /* Ho Hum, dbcs fonts are a PITA! */
            /* To display on W9x I have to convert to UCS */
            static wchar_t *uni_buf = 0;
            static int uni_len = 0;
            int nlen, mptr;
            if (len > uni_len) {
                sfree(uni_buf);
                uni_len = len;
                uni_buf = snewn(uni_len, wchar_t);
            }

            for(nlen = mptr = 0; mptr<len; mptr++) {
                uni_buf[nlen] = 0xFFFD;
                if (IsDBCSLeadByteEx(tabitem->ucsdata.font_codepage,
                                     (BYTE) text[mptr])) {
                    char dbcstext[2];
                    dbcstext[0] = text[mptr] & 0xFF;
                    dbcstext[1] = text[mptr+1] & 0xFF;
                    lpDx[nlen] += char_width;
                    MultiByteToWideChar(tabitem->ucsdata.font_codepage, MB_USEGLYPHCHARS,
                                        dbcstext, 2, uni_buf+nlen, 1);
                    mptr++;
                }
                else
                {
                    char dbcstext[1];
                    dbcstext[0] = text[mptr] & 0xFF;
                    MultiByteToWideChar(tabitem->ucsdata.font_codepage, MB_USEGLYPHCHARS,
                                        dbcstext, 1, uni_buf+nlen, 1);
                }
                nlen++;
            }
            if (nlen <= 0)
                return;		       /* Eeek! */

            ExtTextOutW(hdc, x + xoffset,
                        y - tabitem->font_height * (lattr == LATTR_BOT) + text_adjust,
                        ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                        &line_box, uni_buf, nlen,
                        lpDx_maybe);
            if (tabitem->bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(hdc, TRANSPARENT);
                ExtTextOutW(hdc, x + xoffset - 1,
                            y - tabitem->font_height * (lattr ==
                                               LATTR_BOT) + text_adjust,
                            ETO_CLIPPED, &line_box, uni_buf, nlen, lpDx_maybe);
            }

            lpDx[0] = -1;
        } else if (DIRECT_FONT(text[0])) {
            static char *directbuf = NULL;
            static int directlen = 0;
            int i;
            if (len > directlen) {
                directlen = len;
                directbuf = sresize(directbuf, directlen, char);
            }

            for (i = 0; i < len; i++)
                directbuf[i] = text[i] & 0xFF;

            ExtTextOut(hdc, x + xoffset,
                       y - tabitem->font_height * (lattr == LATTR_BOT) + text_adjust,
                       ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                       &line_box, directbuf, len, lpDx_maybe);
            if (tabitem->bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(hdc, TRANSPARENT);

                /* GRR: This draws the character outside its box and
                 * can leave 'droppings' even with the clip box! I
                 * suppose I could loop it one character at a time ...
                 * yuk.
                 * 
                 * Or ... I could do a test print with "W", and use +1
                 * or -1 for this shift depending on if the leftmost
                 * column is blank...
                 */
                ExtTextOut(hdc, x + xoffset - 1,
                           y - tabitem->font_height * (lattr ==
                                              LATTR_BOT) + text_adjust,
                           ETO_CLIPPED, &line_box, directbuf, len, lpDx_maybe);
            }
        } else {
            /* And 'normal' unicode characters */
            static WCHAR *wbuf = NULL;
            static int wlen = 0;
            int i;

            if (wlen < len) {
                sfree(wbuf);
                wlen = len;
                wbuf = snewn(wlen, WCHAR);
            }

            for (i = 0; i < len; i++)
                wbuf[i] = text[i];

            /* print Glyphs as they are, without Windows' Shaping*/
            general_textout(ctx, x + xoffset,
                            y - tabitem->font_height * (lattr==LATTR_BOT) + text_adjust,
                            &line_box, wbuf, len, lpDx,
                            opaque && !(attr & TATTR_COMBINING));

            /* And the shadow bold hack. */
            if (tabitem->bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(hdc, TRANSPARENT);
                ExtTextOutW(hdc, x + xoffset - 1,
                            y - tabitem->font_height * (lattr ==
                                               LATTR_BOT) + text_adjust,
                            ETO_CLIPPED, &line_box, wbuf, len, lpDx_maybe);
            }
        }

        /*
         * If we're looping round again, stop erasing the background
         * rectangle.
         */
        SetBkMode(hdc, TRANSPARENT);
        opaque = FALSE;
    }
    if (lattr != LATTR_TOP && (force_manual_underline ||
			       (tabitem->und_mode == UND_LINE
				&& (attr & ATTR_UNDER)))) {
	HPEN oldpen;
	int dec = tabitem->descent;
	if (lattr == LATTR_BOT)
	    dec = dec * 2 - tabitem->font_height;

	oldpen = SelectObject(hdc, CreatePen(PS_SOLID, 0, fg));
	MoveToEx(hdc, x, y + dec, NULL);
	LineTo(hdc, x + len * char_width, y + dec);
	oldpen = SelectObject(hdc, oldpen);
	DeleteObject(oldpen);
    }
}

/*
 * Wrapper that handles combining characters.
 */
void do_text(Context ctx, int x, int y, wchar_t *text, int len,
	     unsigned long attr, int lattr)
{
    assert(ctx != NULL);
    if (attr & TATTR_COMBINING) {
	unsigned long a = 0;
	attr &= ~TATTR_COMBINING;
	while (len--) {
	    do_text_internal(ctx, x, y, text, 1, attr | a, lattr);
	    text++;
	    a = TATTR_COMBINING;
	}
    } else
	do_text_internal(ctx, x, y, text, len, attr, lattr);
}

void do_cursor(Context ctx, int x, int y, wchar_t *text, int len,
	       unsigned long attr, int lattr)
{
    assert(ctx != NULL);
    wintabitem *tabitem = (wintabitem *)ctx;
    int fnt_width;
    int char_width;
    HDC hdc = tabitem->hdc;
    int ctype = cfg.cursor_type;

    assert (hdc != NULL);

    lattr &= LATTR_MODE;

    if ((attr & TATTR_ACTCURS) && (ctype == 0 || tabitem->term->big_cursor)) {
	if (*text != UCSWIDE) {
	    do_text(ctx, x, y, text, len, attr, lattr);
	    return;
	}
	ctype = 2;
	attr |= TATTR_RIGHTCURS;
    }

    fnt_width = char_width = tabitem->font_width * (1 + (lattr != LATTR_NORM));
    if (attr & ATTR_WIDE)
	char_width *= 2;
    x *= fnt_width;
    y *= tabitem->font_height;
    x += tabitem->offset_width;
    y += tabitem->offset_height;

    if ((attr & TATTR_PASCURS) && (ctype == 0 || tabitem->term->big_cursor)) {
	POINT pts[5];
	HPEN oldpen;
	pts[0].x = pts[1].x = pts[4].x = x;
	pts[2].x = pts[3].x = x + char_width - 1;
	pts[0].y = pts[3].y = pts[4].y = y;
	pts[1].y = pts[2].y = y + tabitem->font_height - 1;
	oldpen = SelectObject(hdc, CreatePen(PS_SOLID, 0, tabitem->colours[261]));
	Polyline(hdc, pts, 5);
	oldpen = SelectObject(hdc, oldpen);
	DeleteObject(oldpen);
    } else if ((attr & (TATTR_ACTCURS | TATTR_PASCURS)) && ctype != 0) {
	int startx, starty, dx, dy, length, i;
	if (ctype == 1) {
	    startx = x;
	    starty = y + tabitem->descent;
	    dx = 1;
	    dy = 0;
	    length = char_width;
	} else {
	    int xadjust = 0;
	    if (attr & TATTR_RIGHTCURS)
		xadjust = char_width - 1;
	    startx = x + xadjust;
	    starty = y;
	    dx = 0;
	    dy = 1;
	    length = tabitem->font_height;
	}
	if (attr & TATTR_ACTCURS) {
	    HPEN oldpen;
	    oldpen =
		SelectObject(hdc, CreatePen(PS_SOLID, 0, tabitem->colours[261]));
	    MoveToEx(hdc, startx, starty, NULL);
	    LineTo(hdc, startx + dx * length, starty + dy * length);
	    oldpen = SelectObject(hdc, oldpen);
	    DeleteObject(oldpen);
	} else {
	    for (i = 0; i < length; i++) {
		if (i % 2 == 0) {
		    SetPixel(hdc, startx, starty, tabitem->colours[261]);
		}
		startx += dx;
		starty += dy;
	    }
	}
    }
}

/* This function gets the actual width of a character in the normal font.
 */
int char_width(Context ctx, int uc) {
    assert(ctx != NULL);
    wintabitem *tabitem = (wintabitem *)ctx;
    HDC hdc = tabitem->hdc;
    int ibuf = 0;

    assert (hdc != NULL);

    /* If the font max is the same as the font ave width then this
     * function is a no-op.
     */
    if (!tabitem->font_dualwidth) return 1;

    switch (uc & CSET_MASK) {
      case CSET_ASCII:
	uc = tabitem->ucsdata.unitab_line[uc & 0xFF];
	break;
      case CSET_LINEDRW:
	uc = tabitem->ucsdata.unitab_xterm[uc & 0xFF];
	break;
      case CSET_SCOACS:
	uc = tabitem->ucsdata.unitab_scoacs[uc & 0xFF];
	break;
    }
    if (DIRECT_FONT(uc)) {
	if (tabitem->ucsdata.dbcs_screenfont) return 1;

	/* Speedup, I know of no font where ascii is the wrong width */
	if ((uc&~CSET_MASK) >= ' ' && (uc&~CSET_MASK)<= '~')
	    return 1;

	if ( (uc & CSET_MASK) == CSET_ACP ) {
	    SelectObject(hdc, tabitem->fonts[FONT_NORMAL]);
	} else if ( (uc & CSET_MASK) == CSET_OEMCP ) {
	    another_font(ctx, FONT_OEM);
	    if (!tabitem->fonts[FONT_OEM]) return 0;

	    SelectObject(hdc, tabitem->fonts[FONT_OEM]);
	} else
	    return 0;

	if ( GetCharWidth32(hdc, uc&~CSET_MASK, uc&~CSET_MASK, &ibuf) != 1 &&
	     GetCharWidth(hdc, uc&~CSET_MASK, uc&~CSET_MASK, &ibuf) != 1)
	    return 0;
    } else {
	/* Speedup, I know of no font where ascii is the wrong width */
	if (uc >= ' ' && uc <= '~') return 1;

	SelectObject(hdc, tabitem->fonts[FONT_NORMAL]);
	if ( GetCharWidth32W(hdc, uc, uc, &ibuf) == 1 )
	    /* Okay that one worked */ ;
	else if ( GetCharWidthW(hdc, uc, uc, &ibuf) == 1 )
	    /* This should work on 9x too, but it's "less accurate" */ ;
	else
	    return 0;
    }

    ibuf += tabitem->font_width / 2 -1;
    ibuf /= tabitem->font_width;

    return ibuf;
}

/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used, zero to drop the message,
 * -1 to forward the message to Windows, or another negative number
 * to indicate a NUL-terminated "special" string.
 */
static int TranslateKey(wintabitem* tabitem, UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output)
{
    BYTE keystate[256];
    int scan, left_alt = 0, key_down, shift_state;
    int r, i, code;
    unsigned char *p = output;
    static int alt_sum = 0;

    HKL kbd_layout = GetKeyboardLayout(0);

    /* keys is for ToAsciiEx. There's some ick here, see below. */
    static WORD keys[3];
    static int compose_char = 0;
    static WPARAM compose_key = 0;

    r = GetKeyboardState(keystate);
    if (!r)
	memset(keystate, 0, sizeof(keystate));
    else {
#if 0
#define SHOW_TOASCII_RESULT
	{			       /* Tell us all about key events */
	    static BYTE oldstate[256];
	    static int first = 1;
	    static int scan;
	    int ch;
	    if (first)
		memcpy(oldstate, keystate, sizeof(oldstate));
	    first = 0;

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT) {
		debug(("+"));
	    } else if ((HIWORD(lParam) & KF_UP)
		       && scan == (HIWORD(lParam) & 0xFF)) {
		debug((". U"));
	    } else {
		debug((".\n"));
		if (wParam >= VK_F1 && wParam <= VK_F20)
		    debug(("K_F%d", wParam + 1 - VK_F1));
		else
		    switch (wParam) {
		      case VK_SHIFT:
			debug(("SHIFT"));
			break;
		      case VK_CONTROL:
			debug(("CTRL"));
			break;
		      case VK_MENU:
			debug(("ALT"));
			break;
		      default:
			debug(("VK_%02x", wParam));
		    }
		if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		    debug(("*"));
		debug((", S%02x", scan = (HIWORD(lParam) & 0xFF)));

		ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
		if (ch >= ' ' && ch <= '~')
		    debug((", '%c'", ch));
		else if (ch)
		    debug((", $%02x", ch));

		if (keys[0])
		    debug((", KB0=%02x", keys[0]));
		if (keys[1])
		    debug((", KB1=%02x", keys[1]));
		if (keys[2])
		    debug((", KB2=%02x", keys[2]));

		if ((keystate[VK_SHIFT] & 0x80) != 0)
		    debug((", S"));
		if ((keystate[VK_CONTROL] & 0x80) != 0)
		    debug((", C"));
		if ((HIWORD(lParam) & KF_EXTENDED))
		    debug((", E"));
		if ((HIWORD(lParam) & KF_UP))
		    debug((", U"));
	    }

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT);
	    else if ((HIWORD(lParam) & KF_UP))
		oldstate[wParam & 0xFF] ^= 0x80;
	    else
		oldstate[wParam & 0xFF] ^= 0x81;

	    for (ch = 0; ch < 256; ch++)
		if (oldstate[ch] != keystate[ch])
		    debug((", M%02x=%02x", ch, keystate[ch]));

	    memcpy(oldstate, keystate, sizeof(oldstate));
	}
#endif

	if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED)) {
	    keystate[VK_RMENU] = keystate[VK_MENU];
	}


	/* Nastyness with NUMLock - Shift-NUMLock is left alone though */
	if ((tabitem->cfg.funky_type == FUNKY_VT400 ||
	     (tabitem->cfg.funky_type <= FUNKY_LINUX && tabitem->term->app_keypad_keys &&
	      !tabitem->cfg.no_applic_k))
	    && wParam == VK_NUMLOCK && !(keystate[VK_SHIFT] & 0x80)) {

	    wParam = VK_EXECUTE;

	    /* UnToggle NUMLock */
	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
		keystate[VK_NUMLOCK] ^= 1;
	}

	/* And write back the 'adjusted' state */
	SetKeyboardState(keystate);
    }

    /* Disable Auto repeat if required */
    if (tabitem->term->repeat_off &&
	(HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT)
	return 0;

    if ((HIWORD(lParam) & KF_ALTDOWN) && (keystate[VK_RMENU] & 0x80) == 0)
	left_alt = 1;

    key_down = ((HIWORD(lParam) & KF_UP) == 0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL] & 0x80)) {
	if (tabitem->cfg.ctrlaltkeys)
	    keystate[VK_MENU] = 0;
	else {
	    keystate[VK_RMENU] = 0x80;
	    left_alt = 0;
	}
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT] & 0x80) != 0)
	+ ((keystate[VK_CONTROL] & 0x80) != 0) * 2;

    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!tabitem->compose_state) {
	compose_key = 0x100;
	if (tabitem->cfg.compose_key) {
	    if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED))
		compose_key = wParam;
	}
	if (wParam == VK_APPS)
	    compose_key = wParam;
    }

    if (wParam == compose_key) {
	if (tabitem->compose_state == 0
	    && (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0) tabitem->compose_state =
		1;
	else if (tabitem->compose_state == 1 && (HIWORD(lParam) & KF_UP))
	    tabitem->compose_state = 2;
	else
	    tabitem->compose_state = 0;
    } else if (tabitem->compose_state == 1 && wParam != VK_CONTROL)
	tabitem->compose_state = 0;

    if (tabitem->compose_state > 1 && left_alt)
	tabitem->compose_state = 0;

    /* Sanitize the number pad if not using a PC NumPad */
    if (left_alt || (tabitem->term->app_keypad_keys && !tabitem->cfg.no_applic_k
		     && tabitem->cfg.funky_type != FUNKY_XTERM)
	|| tabitem->cfg.funky_type == FUNKY_VT400 || tabitem->cfg.nethack_keypad || tabitem->compose_state) {
	if ((HIWORD(lParam) & KF_EXTENDED) == 0) {
	    int nParam = 0;
	    switch (wParam) {
	      case VK_INSERT:
		nParam = VK_NUMPAD0;
		break;
	      case VK_END:
		nParam = VK_NUMPAD1;
		break;
	      case VK_DOWN:
		nParam = VK_NUMPAD2;
		break;
	      case VK_NEXT:
		nParam = VK_NUMPAD3;
		break;
	      case VK_LEFT:
		nParam = VK_NUMPAD4;
		break;
	      case VK_CLEAR:
		nParam = VK_NUMPAD5;
		break;
	      case VK_RIGHT:
		nParam = VK_NUMPAD6;
		break;
	      case VK_HOME:
		nParam = VK_NUMPAD7;
		break;
	      case VK_UP:
		nParam = VK_NUMPAD8;
		break;
	      case VK_PRIOR:
		nParam = VK_NUMPAD9;
		break;
	      case VK_DELETE:
		nParam = VK_DECIMAL;
		break;
	    }
	    if (nParam) {
		if (keystate[VK_NUMLOCK] & 1)
		    shift_state |= 1;
		wParam = nParam;
	    }
	}
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU] & 0x80) == 0 && !compose_state) {
	/* Okay, prepare for most alts then ... */
	if (left_alt)
	    *p++ = '\033';

	/* Lets see if it's a pattern we know all about ... */
	if (wParam == VK_PRIOR && shift_state == 1) {
	    SendMessage(tabitem->page.hwndCtrl, WM_VSCROLL, SB_PAGEUP, 0);
	    return 0;
	}
	if (wParam == VK_PRIOR && shift_state == 2) {
	    SendMessage(tabitem->page.hwndCtrl, WM_VSCROLL, SB_LINEUP, 0);
	    return 0;
	}
	if (wParam == VK_NEXT && shift_state == 1) {
	    SendMessage(tabitem->page.hwndCtrl, WM_VSCROLL, SB_PAGEDOWN, 0);
	    return 0;
	}
	if (wParam == VK_NEXT && shift_state == 2) {
	    SendMessage(tabitem->page.hwndCtrl, WM_VSCROLL, SB_LINEDOWN, 0);
	    return 0;
	}
	if ((wParam == VK_PRIOR || wParam == VK_NEXT) && shift_state == 3) {
	    term_scroll_to_selection(tabitem->term, (wParam == VK_PRIOR ? 0 : 1));
	    return 0;
	}
	if (wParam == VK_INSERT && shift_state == 1) {
	    request_paste(tabitem);
	    return 0;
	}
	if (left_alt && wParam == VK_F4 && tabitem->cfg.alt_f4) {
	    return -1;
	}
	if (left_alt && wParam == VK_SPACE && tabitem->cfg.alt_space) {
	    SendMessage(tabitem->page.hwndCtrl, WM_SYSCOMMAND, SC_KEYMENU, 0);
	    return -1;
	}
	if (left_alt && wParam == VK_RETURN && tabitem->cfg.fullscreenonaltenter &&
	    (tabitem->cfg.resize_action != RESIZE_DISABLED)) {
 	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) != KF_REPEAT)
 		flip_full_screen();
	    return -1;
	}
	/* Control-Numlock for app-keypad mode switch */
	if (wParam == VK_PAUSE && shift_state == 2) {
	    tabitem->term->app_keypad_keys ^= 1;
	    return 0;
	}

	/* Nethack keypad */
	if (tabitem->cfg.nethack_keypad && !left_alt) {
	    switch (wParam) {
	      case VK_NUMPAD1:
		*p++ = "bB\002\002"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD2:
		*p++ = "jJ\012\012"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD3:
		*p++ = "nN\016\016"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD4:
		*p++ = "hH\010\010"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD5:
		*p++ = shift_state ? '.' : '.';
		return p - output;
	      case VK_NUMPAD6:
		*p++ = "lL\014\014"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD7:
		*p++ = "yY\031\031"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD8:
		*p++ = "kK\013\013"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD9:
		*p++ = "uU\025\025"[shift_state & 3];
		return p - output;
	    }
	}

	/* Application Keypad */
	if (!left_alt) {
	    int xkey = 0;

	    if (tabitem->cfg.funky_type == FUNKY_VT400 ||
		(tabitem->cfg.funky_type <= FUNKY_LINUX &&
		 tabitem->term->app_keypad_keys && !tabitem->cfg.no_applic_k)) switch (wParam) {
		  case VK_EXECUTE:
		    xkey = 'P';
		    break;
		  case VK_DIVIDE:
		    xkey = 'Q';
		    break;
		  case VK_MULTIPLY:
		    xkey = 'R';
		    break;
		  case VK_SUBTRACT:
		    xkey = 'S';
		    break;
		}
	    if (tabitem->term->app_keypad_keys && !tabitem->cfg.no_applic_k)
		switch (wParam) {
		  case VK_NUMPAD0:
		    xkey = 'p';
		    break;
		  case VK_NUMPAD1:
		    xkey = 'q';
		    break;
		  case VK_NUMPAD2:
		    xkey = 'r';
		    break;
		  case VK_NUMPAD3:
		    xkey = 's';
		    break;
		  case VK_NUMPAD4:
		    xkey = 't';
		    break;
		  case VK_NUMPAD5:
		    xkey = 'u';
		    break;
		  case VK_NUMPAD6:
		    xkey = 'v';
		    break;
		  case VK_NUMPAD7:
		    xkey = 'w';
		    break;
		  case VK_NUMPAD8:
		    xkey = 'x';
		    break;
		  case VK_NUMPAD9:
		    xkey = 'y';
		    break;

		  case VK_DECIMAL:
		    xkey = 'n';
		    break;
		  case VK_ADD:
		    if (tabitem->cfg.funky_type == FUNKY_XTERM) {
			if (shift_state)
			    xkey = 'l';
			else
			    xkey = 'k';
		    } else if (shift_state)
			xkey = 'm';
		    else
			xkey = 'l';
		    break;

		  case VK_DIVIDE:
		    if (tabitem->cfg.funky_type == FUNKY_XTERM)
			xkey = 'o';
		    break;
		  case VK_MULTIPLY:
		    if (tabitem->cfg.funky_type == FUNKY_XTERM)
			xkey = 'j';
		    break;
		  case VK_SUBTRACT:
		    if (tabitem->cfg.funky_type == FUNKY_XTERM)
			xkey = 'm';
		    break;

		  case VK_RETURN:
		    if (HIWORD(lParam) & KF_EXTENDED)
			xkey = 'M';
		    break;
		}
	    if (xkey) {
		if (tabitem->term->vt52_mode) {
		    if (xkey >= 'P' && xkey <= 'S')
			p += sprintf((char *) p, "\x1B%c", xkey);
		    else
			p += sprintf((char *) p, "\x1B?%c", xkey);
		} else
		    p += sprintf((char *) p, "\x1BO%c", xkey);
		return p - output;
	    }
	}

	if (wParam == VK_BACK && shift_state == 0) {	/* Backspace */
	    *p++ = (tabitem->cfg.bksp_is_delete ? 0x7F : 0x08);
	    *p++ = 0;
	    return -2;
	}
	if (wParam == VK_BACK && shift_state == 1) {	/* Shift Backspace */
	    /* We do the opposite of what is configured */
	    *p++ = (tabitem->cfg.bksp_is_delete ? 0x08 : 0x7F);
	    *p++ = 0;
	    return -2;
	}
	if (wParam == VK_TAB && shift_state == 1) {	/* Shift tab */
	    *p++ = 0x1B;
	    *p++ = '[';
	    *p++ = 'Z';
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 2) {	/* Ctrl-Space */
	    *p++ = 0;
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 3) {	/* Ctrl-Shift-Space */
	    *p++ = 160;
	    return p - output;
	}
	if (wParam == VK_CANCEL && shift_state == 2) {	/* Ctrl-Break */
	    if (tabitem->back)
		tabitem->back->special(tabitem->backhandle, TS_BRK);
	    return 0;
	}
	if (wParam == VK_PAUSE) {      /* Break/Pause */
	    *p++ = 26;
	    *p++ = 0;
	    return -2;
	}
	/* Control-2 to Control-8 are special */
	if (shift_state == 2 && wParam >= '2' && wParam <= '8') {
	    *p++ = "\000\033\034\035\036\037\177"[wParam - '2'];
	    return p - output;
	}
	if (shift_state == 2 && (wParam == 0xBD || wParam == 0xBF)) {
	    *p++ = 0x1F;
	    return p - output;
	}
	if (shift_state == 2 && (wParam == 0xDF || wParam == 0xDC)) {
	    *p++ = 0x1C;
	    return p - output;
	}
	if (shift_state == 3 && wParam == 0xDE) {
	    *p++ = 0x1E;	       /* Ctrl-~ == Ctrl-^ in xterm at least */
	    return p - output;
	}
	if (shift_state == 0 && wParam == VK_RETURN && tabitem->term->cr_lf_return) {
	    *p++ = '\r';
	    *p++ = '\n';
	    return p - output;
	}

	/*
	 * Next, all the keys that do tilde codes. (ESC '[' nn '~',
	 * for integer decimal nn.)
	 *
	 * We also deal with the weird ones here. Linux VCs replace F1
	 * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
	 * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
	 * respectively.
	 */
	code = 0;
	switch (wParam) {
	  case VK_F1:
	    code = (keystate[VK_SHIFT] & 0x80 ? 23 : 11);
	    break;
	  case VK_F2:
	    code = (keystate[VK_SHIFT] & 0x80 ? 24 : 12);
	    break;
	  case VK_F3:
	    code = (keystate[VK_SHIFT] & 0x80 ? 25 : 13);
	    break;
	  case VK_F4:
	    code = (keystate[VK_SHIFT] & 0x80 ? 26 : 14);
	    break;
	  case VK_F5:
	    code = (keystate[VK_SHIFT] & 0x80 ? 28 : 15);
	    break;
	  case VK_F6:
	    code = (keystate[VK_SHIFT] & 0x80 ? 29 : 17);
	    break;
	  case VK_F7:
	    code = (keystate[VK_SHIFT] & 0x80 ? 31 : 18);
	    break;
	  case VK_F8:
	    code = (keystate[VK_SHIFT] & 0x80 ? 32 : 19);
	    break;
	  case VK_F9:
	    code = (keystate[VK_SHIFT] & 0x80 ? 33 : 20);
	    break;
	  case VK_F10:
	    code = (keystate[VK_SHIFT] & 0x80 ? 34 : 21);
	    break;
	  case VK_F11:
	    code = 23;
	    break;
	  case VK_F12:
	    code = 24;
	    break;
	  case VK_F13:
	    code = 25;
	    break;
	  case VK_F14:
	    code = 26;
	    break;
	  case VK_F15:
	    code = 28;
	    break;
	  case VK_F16:
	    code = 29;
	    break;
	  case VK_F17:
	    code = 31;
	    break;
	  case VK_F18:
	    code = 32;
	    break;
	  case VK_F19:
	    code = 33;
	    break;
	  case VK_F20:
	    code = 34;
	    break;
	}
	if ((shift_state&2) == 0) switch (wParam) {
	  case VK_HOME:
	    code = 1;
	    break;
	  case VK_INSERT:
	    code = 2;
	    break;
	  case VK_DELETE:
	    code = 3;
	    break;
	  case VK_END:
	    code = 4;
	    break;
	  case VK_PRIOR:
	    code = 5;
	    break;
	  case VK_NEXT:
	    code = 6;
	    break;
	}
	/* Reorder edit keys to physical order */
	if (tabitem->cfg.funky_type == FUNKY_VT400 && code <= 6)
	    code = "\0\2\1\4\5\3\6"[code];

	if (tabitem->term->vt52_mode && code > 0 && code <= 6) {
	    p += sprintf((char *) p, "\x1B%c", " HLMEIG"[code]);
	    return p - output;
	}

	if (tabitem->cfg.funky_type == FUNKY_SCO &&     /* SCO function keys */
	    code >= 11 && code <= 34) {
	    char codes[] = "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
	    int index = 0;
	    switch (wParam) {
	      case VK_F1: index = 0; break;
	      case VK_F2: index = 1; break;
	      case VK_F3: index = 2; break;
	      case VK_F4: index = 3; break;
	      case VK_F5: index = 4; break;
	      case VK_F6: index = 5; break;
	      case VK_F7: index = 6; break;
	      case VK_F8: index = 7; break;
	      case VK_F9: index = 8; break;
	      case VK_F10: index = 9; break;
	      case VK_F11: index = 10; break;
	      case VK_F12: index = 11; break;
	    }
	    if (keystate[VK_SHIFT] & 0x80) index += 12;
	    if (keystate[VK_CONTROL] & 0x80) index += 24;
	    p += sprintf((char *) p, "\x1B[%c", codes[index]);
	    return p - output;
	}
	if (tabitem->cfg.funky_type == FUNKY_SCO &&     /* SCO small keypad */
	    code >= 1 && code <= 6) {
	    char codes[] = "HL.FIG";
	    if (code == 3) {
		*p++ = '\x7F';
	    } else {
		p += sprintf((char *) p, "\x1B[%c", codes[code-1]);
	    }
	    return p - output;
	}
	if ((tabitem->term->vt52_mode || tabitem->cfg.funky_type == FUNKY_VT100P) && code >= 11 && code <= 24) {
	    int offt = 0;
	    if (code > 15)
		offt++;
	    if (code > 21)
		offt++;
	    if (tabitem->term->vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11 - offt);
	    else
		p +=
		    sprintf((char *) p, "\x1BO%c", code + 'P' - 11 - offt);
	    return p - output;
	}
	if (tabitem->cfg.funky_type == FUNKY_LINUX && code >= 11 && code <= 15) {
	    p += sprintf((char *) p, "\x1B[[%c", code + 'A' - 11);
	    return p - output;
	}
	if (tabitem->cfg.funky_type == FUNKY_XTERM && code >= 11 && code <= 14) {
	    if (tabitem->term->vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11);
	    else
		p += sprintf((char *) p, "\x1BO%c", code + 'P' - 11);
	    return p - output;
	}
	if (tabitem->cfg.rxvt_homeend && (code == 1 || code == 4)) {
	    p += sprintf((char *) p, code == 1 ? "\x1B[H" : "\x1BOw");
	    return p - output;
	}
	if (code) {
	    p += sprintf((char *) p, "\x1B[%d~", code);
	    return p - output;
	}

	/*
	 * Now the remaining keys (arrows and Keypad 5. Keypad 5 for
	 * some reason seems to send VK_CLEAR to Windows...).
	 */
	{
	    char xkey = 0;
	    switch (wParam) {
	      case VK_UP:
		xkey = 'A';
		break;
	      case VK_DOWN:
		xkey = 'B';
		break;
	      case VK_RIGHT:
		xkey = 'C';
		break;
	      case VK_LEFT:
		xkey = 'D';
		break;
	      case VK_CLEAR:
		xkey = 'G';
		break;
	    }
	    if (xkey) {
		p += format_arrow_key(p, tabitem->term, xkey, shift_state);
		return p - output;
	    }
	}

	/*
	 * Finally, deal with Return ourselves. (Win95 seems to
	 * foul it up when Alt is pressed, for some reason.)
	 */
	if (wParam == VK_RETURN) {     /* Return */
	    *p++ = 0x0D;
	    *p++ = 0;
	    return -2;
	}

	if (left_alt && wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
	    alt_sum = alt_sum * 10 + wParam - VK_NUMPAD0;
	else
	    alt_sum = 0;
    }

    /* Okay we've done everything interesting; let windows deal with 
     * the boring stuff */
    {
	BOOL capsOn=0;

	/* helg: clear CAPS LOCK state if caps lock switches to cyrillic */
	if(tabitem->cfg.xlat_capslockcyr && keystate[VK_CAPITAL] != 0) {
	    capsOn= !left_alt;
	    keystate[VK_CAPITAL] = 0;
	}

	/* XXX how do we know what the max size of the keys array should
	 * be is? There's indication on MS' website of an Inquire/InquireEx
	 * functioning returning a KBINFO structure which tells us. */
	if (osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT) {
	    /* XXX 'keys' parameter is declared in MSDN documentation as
	     * 'LPWORD lpChar'.
	     * The experience of a French user indicates that on
	     * Win98, WORD[] should be passed in, but on Win2K, it should
	     * be BYTE[]. German WinXP and my Win2K with "US International"
	     * driver corroborate this.
	     * Experimentally I've conditionalised the behaviour on the
	     * Win9x/NT split, but I suspect it's worse than that.
	     * See wishlist item `win-dead-keys' for more horrible detail
	     * and speculations. */
	    BYTE keybs[3];
	    int i;
	    r = ToAsciiEx(wParam, scan, keystate, (LPWORD)keybs, 0, kbd_layout);
	    for (i=0; i<3; i++) keys[i] = keybs[i];
	} else {
	    r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
	}
#ifdef SHOW_TOASCII_RESULT
	if (r == 1 && !key_down) {
	    if (alt_sum) {
		if (in_utf(tabitem->term) || tabitem->ucsdata.dbcs_screenfont)
		    debug((", (U+%04x)", alt_sum));
		else
		    debug((", LCH(%d)", alt_sum));
	    } else {
		debug((", ACH(%d)", keys[0]));
	    }
	} else if (r > 0) {
	    int r1;
	    debug((", ASC("));
	    for (r1 = 0; r1 < r; r1++) {
		debug(("%s%d", r1 ? "," : "", keys[r1]));
	    }
	    debug((")"));
	}
#endif
	if (r > 0) {
	    WCHAR keybuf;

	    /*
	     * Interrupt an ongoing paste. I'm not sure this is
	     * sensible, but for the moment it's preferable to
	     * having to faff about buffering things.
	     */
	    term_nopaste(tabitem->term);

	    p = output;
	    for (i = 0; i < r; i++) {
		unsigned char ch = (unsigned char) keys[i];

		if (tabitem->compose_state == 2 && (ch & 0x80) == 0 && ch > ' ') {
		    compose_char = ch;
		    tabitem->compose_state++;
		    continue;
		}
		if (tabitem->compose_state == 3 && (ch & 0x80) == 0 && ch > ' ') {
		    int nc;
		    tabitem->compose_state = 0;

		    if ((nc = check_compose(compose_char, ch)) == -1) {
			MessageBeep(MB_ICONHAND);
			return 0;
		    }
		    keybuf = nc;
		    term_seen_key_event(tabitem->term);
		    if (tabitem->ldisc)
			luni_send(tabitem->ldisc, &keybuf, 1, 1);
		    continue;
		}

		tabitem->compose_state = 0;

		if (!key_down) {
		    if (alt_sum) {
			if (in_utf(tabitem->term) || tabitem->ucsdata.dbcs_screenfont) {
			    keybuf = alt_sum;
			    term_seen_key_event(tabitem->term);
			    if (tabitem->ldisc)
				luni_send(tabitem->ldisc, &keybuf, 1, 1);
			} else {
			    ch = (char) alt_sum;
			    /*
			     * We need not bother about stdin
			     * backlogs here, because in GUI PuTTY
			     * we can't do anything about it
			     * anyway; there's no means of asking
			     * Windows to hold off on KEYDOWN
			     * messages. We _have_ to buffer
			     * everything we're sent.
			     */
			    term_seen_key_event(tabitem->term);
			    if (tabitem->ldisc)
				ldisc_send(tabitem->ldisc, &ch, 1, 1);
			}
			alt_sum = 0;
		    } else {
			term_seen_key_event(tabitem->term);
			if (tabitem->ldisc)
			    lpage_send(tabitem->ldisc, kbd_codepage, &ch, 1, 1);
		    }
		} else {
		    if(capsOn && ch < 0x80) {
			WCHAR cbuf[2];
			cbuf[0] = 27;
			cbuf[1] = xlat_uskbd2cyrllic(ch);
			term_seen_key_event(tabitem->term);
			if (tabitem->ldisc)
			    luni_send(tabitem->ldisc, cbuf+!left_alt, 1+!!left_alt, 1);
		    } else {
			char cbuf[2];
			cbuf[0] = '\033';
			cbuf[1] = ch;
			term_seen_key_event(tabitem->term);
			if (tabitem->ldisc)
			    lpage_send(tabitem->ldisc, kbd_codepage,
				       cbuf+!left_alt, 1+!!left_alt, 1);
		    }
		}
		show_mouseptr(tabitem, 0);
	    }

	    /* This is so the ALT-Numpad and dead keys work correctly. */
	    keys[0] = 0;

	    return p - output;
	}
	/* If we're definitly not building up an ALT-54321 then clear it */
	if (!left_alt)
	    keys[0] = 0;
	/* If we will be using alt_sum fix the 256s */
	else if (keys[0] && (in_utf(tabitem->term) || tabitem->ucsdata.dbcs_screenfont))
	    keys[0] = 10;
    }

    /*
     * ALT alone may or may not want to bring up the System menu.
     * If it's not meant to, we return 0 on presses or releases of
     * ALT, to show that we've swallowed the keystroke. Otherwise
     * we return -1, which means Windows will give the keystroke
     * its default handling (i.e. bring up the System menu).
     */
    if (wParam == VK_MENU && !tabitem->cfg.alt_only)
	return 0;

    return -1;
}

void set_title(void *frontend, char *title)
{
    assert (frontend != NULL);
    if (!title || !*title) return;
    wintabitem *tabitem = (wintabitem*) frontend;
    sfree(tabitem->window_name);
    tabitem->window_name = snewn(1 + strlen(title), char);
    strcpy(tabitem->window_name, title);
    if (tabitem->cfg.win_name_always || !IsIconic(hwnd))
	SetWindowText(hwnd, title);
}

void set_icon(void *frontend, char *title)
{
    assert (frontend != NULL);
    if (!title || !*title) return;
    wintabitem *tabitem = (wintabitem*) frontend;
    sfree(tabitem->icon_name);
    tabitem->icon_name = snewn(1 + strlen(title), char);
    strcpy(tabitem->icon_name, title);
    if (!tabitem->cfg.win_name_always && IsIconic(hwnd))
	SetWindowText(hwnd, title);
}

void set_sbar(void *frontend, int total, int start, int page)
{
    assert (frontend != NULL);

    wintabitem *tabitem = (wintabitem*) frontend;
    SCROLLINFO si;

    if (is_full_screen() ? !tabitem->cfg.scrollbar_in_fullscreen : !tabitem->cfg.scrollbar)
	return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    if (tabitem->page.hwndCtrl)
    	SetScrollInfo(tabitem->page.hwndCtrl, SB_VERT, &si, TRUE);
}

Context get_ctx(void *frontend)
{
    assert(frontend != NULL);
    wintabitem *tabitem = (wintabitem *)frontend;
    if (tabitem->page.hwndCtrl){
    	tabitem->hdc = GetDC(tabitem->page.hwndCtrl);
    	if (tabitem->hdc && tabitem->pal){
    	    SelectPalette(tabitem->hdc, tabitem->pal, FALSE);
        }         
    }
    return tabitem;
}

void free_ctx(void *frontend, Context ctx)
{
    assert(frontend != NULL && ctx != NULL);
    wintabitem *tabitem = (wintabitem *)ctx;
    if (tabitem->hdc){
        SelectPalette(tabitem->hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
        ReleaseDC(tabitem->page.hwndCtrl, tabitem->hdc);
        tabitem->hdc = NULL;
    }
}

static void real_palette_set(void *frontend, int n, int r, int g, int b)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    if (tabitem->pal) {
    	tabitem->logpal->palPalEntry[n].peRed = r;
    	tabitem->logpal->palPalEntry[n].peGreen = g;
    	tabitem->logpal->palPalEntry[n].peBlue = b;
    	tabitem->logpal->palPalEntry[n].peFlags = PC_NOCOLLAPSE;
    	tabitem->colours[n] = PALETTERGB(r, g, b);
    	SetPaletteEntries(tabitem->pal, 0, NALLCOLOURS, tabitem->logpal->palPalEntry);
    } else
    	tabitem->colours[n] = RGB(r, g, b);
}

void palette_set(void *frontend, int n, int r, int g, int b)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    
    if (n >= 16)
	n += 256 - 16;
    if (n > NALLCOLOURS)
	return;
    real_palette_set(frontend, n, r, g, b);
    if (tabitem->pal) {
        wintabitem *tabitem = get_ctx(frontend);
        assert(tabitem != NULL);
    	HDC hdc = tabitem->hdc;
        assert (hdc != NULL);
    	UnrealizeObject(tabitem->pal);
    	RealizePalette(hdc);
    	free_ctx(tabitem, tabitem);
    } else {
	if (n == (ATTR_DEFBG>>ATTR_BGSHIFT))
	    /* If Default Background changes, we need to ensure any
	     * space between the text area and the window border is
	     * redrawn. */
	    InvalidateRect(tabitem->page.hwndCtrl, NULL, TRUE);
    }
}

void palette_reset(void *frontend)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    int i;

    /* And this */
    for (i = 0; i < NALLCOLOURS; i++) {
	if (tabitem->pal) {
	    tabitem->logpal->palPalEntry[i].peRed = tabitem->defpal[i].rgbtRed;
	    tabitem->logpal->palPalEntry[i].peGreen = tabitem->defpal[i].rgbtGreen;
	    tabitem->logpal->palPalEntry[i].peBlue = tabitem->defpal[i].rgbtBlue;
	    tabitem->logpal->palPalEntry[i].peFlags = 0;
	    tabitem->colours[i] = PALETTERGB(tabitem->defpal[i].rgbtRed,
				    tabitem->defpal[i].rgbtGreen,
				    tabitem->defpal[i].rgbtBlue);
	} else
	    tabitem->colours[i] = RGB(tabitem->defpal[i].rgbtRed,
			     tabitem->defpal[i].rgbtGreen, tabitem->defpal[i].rgbtBlue);
    }

    if (tabitem->pal) {
	HDC hdc;
	SetPaletteEntries(tabitem->pal, 0, NALLCOLOURS, tabitem->logpal->palPalEntry);
	wintabitem *tabitem = get_ctx(frontend);
    assert(tabitem != NULL);
    hdc = tabitem->hdc;
    assert (hdc != NULL);
	RealizePalette(hdc);
	free_ctx(tabitem, tabitem);
    } else {
    	/* Default Background may have changed. Ensure any space between
    	 * text area and window border is redrawn. */
    	InvalidateRect(tabitem->page.hwndCtrl, NULL, TRUE);
    }
}

void write_aclip(void *frontend, char *data, int len, int must_deselect)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    HGLOBAL clipdata;
    void *lock;

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
	return;
    lock = GlobalLock(clipdata);
    if (!lock)
	return;
    memcpy(lock, data, len);
    ((unsigned char *) lock)[len] = 0;
    GlobalUnlock(clipdata);

    if (!must_deselect)
	SendMessage(tabitem->page.hwndCtrl, WM_IGNORE_CLIP, TRUE, 0);

    if (OpenClipboard(tabitem->page.hwndCtrl)) {
	EmptyClipboard();
	SetClipboardData(CF_TEXT, clipdata);
	CloseClipboard();
    } else
	GlobalFree(clipdata);

    if (!must_deselect)
	SendMessage(tabitem->page.hwndCtrl, WM_IGNORE_CLIP, FALSE, 0);
}

/*
 * Note: unlike write_aclip() this will not append a nul.
 */
void write_clip(void *frontend, wchar_t * data, int *attr, int len, int must_deselect)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    HGLOBAL clipdata, clipdata2, clipdata3;
    int len2;
    void *lock, *lock2, *lock3;

    len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, NULL, NULL);

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE,
			   len * sizeof(wchar_t));
    clipdata2 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len2);

    if (!clipdata || !clipdata2) {
	if (clipdata)
	    GlobalFree(clipdata);
	if (clipdata2)
	    GlobalFree(clipdata2);
	return;
    }
    if (!(lock = GlobalLock(clipdata)))
	return;
    if (!(lock2 = GlobalLock(clipdata2)))
	return;

    memcpy(lock, data, len * sizeof(wchar_t));
    WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, NULL, NULL);

    if (tabitem->cfg.rtf_paste) {
	wchar_t unitab[256];
	char *rtf = NULL;
	unsigned char *tdata = (unsigned char *)lock2;
	wchar_t *udata = (wchar_t *)lock;
	int rtflen = 0, uindex = 0, tindex = 0;
	int rtfsize = 0;
	int multilen, blen, alen, totallen, i;
	char before[16], after[4];
	int fgcolour,  lastfgcolour  = 0;
	int bgcolour,  lastbgcolour  = 0;
	int attrBold,  lastAttrBold  = 0;
	int attrUnder, lastAttrUnder = 0;
	int palette[NALLCOLOURS];
	int numcolours;

	get_unitab(CP_ACP, unitab, 0);

	rtfsize = 100 + strlen(tabitem->cfg.font.name);
	rtf = snewn(rtfsize, char);
	rtflen = sprintf(rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl\\f0\\fmodern %s;}\\f0\\fs%d",
			 tabitem->cfg.font.name, tabitem->cfg.font.height*2);

	/*
	 * Add colour palette
	 * {\colortbl ;\red255\green0\blue0;\red0\green0\blue128;}
	 */

	/*
	 * First - Determine all colours in use
	 *    o  Foregound and background colours share the same palette
	 */
	if (attr) {
	    memset(palette, 0, sizeof(palette));
	    for (i = 0; i < (len-1); i++) {
		fgcolour = ((attr[i] & ATTR_FGMASK) >> ATTR_FGSHIFT);
		bgcolour = ((attr[i] & ATTR_BGMASK) >> ATTR_BGSHIFT);

		if (attr[i] & ATTR_REVERSE) {
		    int tmpcolour = fgcolour;	/* Swap foreground and background */
		    fgcolour = bgcolour;
		    bgcolour = tmpcolour;
		}

		if (tabitem->bold_mode == BOLD_COLOURS && (attr[i] & ATTR_BOLD)) {
		    if (fgcolour  <   8)	/* ANSI colours */
			fgcolour +=   8;
		    else if (fgcolour >= 256)	/* Default colours */
			fgcolour ++;
		}

		if (attr[i] & ATTR_BLINK) {
		    if (bgcolour  <   8)	/* ANSI colours */
			bgcolour +=   8;
    		    else if (bgcolour >= 256)	/* Default colours */
			bgcolour ++;
		}

		palette[fgcolour]++;
		palette[bgcolour]++;
	    }

	    /*
	     * Next - Create a reduced palette
	     */
	    numcolours = 0;
	    for (i = 0; i < NALLCOLOURS; i++) {
		if (palette[i] != 0)
		    palette[i]  = ++numcolours;
	    }

	    /*
	     * Finally - Write the colour table
	     */
	    rtf = sresize(rtf, rtfsize + (numcolours * 25), char);
	    strcat(rtf, "{\\colortbl ;");
	    rtflen = strlen(rtf);

	    for (i = 0; i < NALLCOLOURS; i++) {
		if (palette[i] != 0) {
		    rtflen += sprintf(&rtf[rtflen], "\\red%d\\green%d\\blue%d;", tabitem->defpal[i].rgbtRed, tabitem->defpal[i].rgbtGreen, tabitem->defpal[i].rgbtBlue);
		}
	    }
	    strcpy(&rtf[rtflen], "}");
	    rtflen ++;
	}

	/*
	 * We want to construct a piece of RTF that specifies the
	 * same Unicode text. To do this we will read back in
	 * parallel from the Unicode data in `udata' and the
	 * non-Unicode data in `tdata'. For each character in
	 * `tdata' which becomes the right thing in `udata' when
	 * looked up in `unitab', we just copy straight over from
	 * tdata. For each one that doesn't, we must WCToMB it
	 * individually and produce a \u escape sequence.
	 * 
	 * It would probably be more robust to just bite the bullet
	 * and WCToMB each individual Unicode character one by one,
	 * then MBToWC each one back to see if it was an accurate
	 * translation; but that strikes me as a horrifying number
	 * of Windows API calls so I want to see if this faster way
	 * will work. If it screws up badly we can always revert to
	 * the simple and slow way.
	 */
	while (tindex < len2 && uindex < len &&
	       tdata[tindex] && udata[uindex]) {
	    if (tindex + 1 < len2 &&
		tdata[tindex] == '\r' &&
		tdata[tindex+1] == '\n') {
		tindex++;
		uindex++;
            }

            /*
             * Set text attributes
             */
            if (attr) {
                if (rtfsize < rtflen + 64) {
		    rtfsize = rtflen + 512;
		    rtf = sresize(rtf, rtfsize, char);
                }

                /*
                 * Determine foreground and background colours
                 */
                fgcolour = ((attr[tindex] & ATTR_FGMASK) >> ATTR_FGSHIFT);
                bgcolour = ((attr[tindex] & ATTR_BGMASK) >> ATTR_BGSHIFT);

		if (attr[tindex] & ATTR_REVERSE) {
		    int tmpcolour = fgcolour;	    /* Swap foreground and background */
		    fgcolour = bgcolour;
		    bgcolour = tmpcolour;
		}

		if (tabitem->bold_mode == BOLD_COLOURS && (attr[tindex] & ATTR_BOLD)) {
		    if (fgcolour  <   8)	    /* ANSI colours */
			fgcolour +=   8;
		    else if (fgcolour >= 256)	    /* Default colours */
			fgcolour ++;
                }

		if (attr[tindex] & ATTR_BLINK) {
		    if (bgcolour  <   8)	    /* ANSI colours */
			bgcolour +=   8;
		    else if (bgcolour >= 256)	    /* Default colours */
			bgcolour ++;
                }

                /*
                 * Collect other attributes
                 */
		if (tabitem->bold_mode != BOLD_COLOURS)
		    attrBold  = attr[tindex] & ATTR_BOLD;
		else
		    attrBold  = 0;
                
		attrUnder = attr[tindex] & ATTR_UNDER;

                /*
                 * Reverse video
		 *   o  If video isn't reversed, ignore colour attributes for default foregound
	         *	or background.
		 *   o  Special case where bolded text is displayed using the default foregound
		 *      and background colours - force to bolded RTF.
                 */
		if (!(attr[tindex] & ATTR_REVERSE)) {
		    if (bgcolour >= 256)	    /* Default color */
			bgcolour  = -1;		    /* No coloring */

		    if (fgcolour >= 256) {	    /* Default colour */
			if (tabitem->bold_mode == BOLD_COLOURS && (fgcolour & 1) && bgcolour == -1)
			    attrBold = ATTR_BOLD;   /* Emphasize text with bold attribute */

			fgcolour  = -1;		    /* No coloring */
		    }
		}

                /*
                 * Write RTF text attributes
                 */
		if (lastfgcolour != fgcolour) {
                    lastfgcolour  = fgcolour;
		    rtflen       += sprintf(&rtf[rtflen], "\\cf%d ", (fgcolour >= 0) ? palette[fgcolour] : 0);
                }

                if (lastbgcolour != bgcolour) {
                    lastbgcolour  = bgcolour;
                    rtflen       += sprintf(&rtf[rtflen], "\\highlight%d ", (bgcolour >= 0) ? palette[bgcolour] : 0);
                }

		if (lastAttrBold != attrBold) {
		    lastAttrBold  = attrBold;
		    rtflen       += sprintf(&rtf[rtflen], "%s", attrBold ? "\\b " : "\\b0 ");
		}

                if (lastAttrUnder != attrUnder) {
                    lastAttrUnder  = attrUnder;
                    rtflen        += sprintf(&rtf[rtflen], "%s", attrUnder ? "\\ul " : "\\ulnone ");
                }
	    }

	    if (unitab[tdata[tindex]] == udata[uindex]) {
		multilen = 1;
		before[0] = '\0';
		after[0] = '\0';
		blen = alen = 0;
	    } else {
		multilen = WideCharToMultiByte(CP_ACP, 0, unitab+uindex, 1,
					       NULL, 0, NULL, NULL);
		if (multilen != 1) {
		    blen = sprintf(before, "{\\uc%d\\u%d", multilen,
				   udata[uindex]);
		    alen = 1; strcpy(after, "}");
		} else {
		    blen = sprintf(before, "\\u%d", udata[uindex]);
		    alen = 0; after[0] = '\0';
		}
	    }
	    assert(tindex + multilen <= len2);
	    totallen = blen + alen;
	    for (i = 0; i < multilen; i++) {
		if (tdata[tindex+i] == '\\' ||
		    tdata[tindex+i] == '{' ||
		    tdata[tindex+i] == '}')
		    totallen += 2;
		else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A)
		    totallen += 6;     /* \par\r\n */
		else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20)
		    totallen += 4;
		else
		    totallen++;
	    }

	    if (rtfsize < rtflen + totallen + 3) {
		rtfsize = rtflen + totallen + 512;
		rtf = sresize(rtf, rtfsize, char);
	    }

	    strcpy(rtf + rtflen, before); rtflen += blen;
	    for (i = 0; i < multilen; i++) {
		if (tdata[tindex+i] == '\\' ||
		    tdata[tindex+i] == '{' ||
		    tdata[tindex+i] == '}') {
		    rtf[rtflen++] = '\\';
		    rtf[rtflen++] = tdata[tindex+i];
		} else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A) {
		    rtflen += sprintf(rtf+rtflen, "\\par\r\n");
		} else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20) {
		    rtflen += sprintf(rtf+rtflen, "\\'%02x", tdata[tindex+i]);
		} else {
		    rtf[rtflen++] = tdata[tindex+i];
		}
	    }
	    strcpy(rtf + rtflen, after); rtflen += alen;

	    tindex += multilen;
	    uindex++;
	}

        rtf[rtflen++] = '}';	       /* Terminate RTF stream */
        rtf[rtflen++] = '\0';
        rtf[rtflen++] = '\0';

	clipdata3 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, rtflen);
	if (clipdata3 && (lock3 = GlobalLock(clipdata3)) != NULL) {
	    memcpy(lock3, rtf, rtflen);
	    GlobalUnlock(clipdata3);
	}
	sfree(rtf);
    } else
	clipdata3 = NULL;

    GlobalUnlock(clipdata);
    GlobalUnlock(clipdata2);

    if (!must_deselect)
	SendMessage(tabitem->page.hwndCtrl, WM_IGNORE_CLIP, TRUE, 0);

    if (OpenClipboard(tabitem->page.hwndCtrl)) {
	EmptyClipboard();
	SetClipboardData(CF_UNICODETEXT, clipdata);
	SetClipboardData(CF_TEXT, clipdata2);
	if (clipdata3)
	    SetClipboardData(RegisterClipboardFormat(CF_RTF), clipdata3);
	CloseClipboard();
    } else {
	GlobalFree(clipdata);
	GlobalFree(clipdata2);
    }

    if (!must_deselect)
	SendMessage(tabitem->page.hwndCtrl, WM_IGNORE_CLIP, FALSE, 0);
}

static DWORD WINAPI clipboard_read_threadfunc(void *param)
{
    HWND hwnd = (HWND)param;
    HGLOBAL clipdata;

    if (OpenClipboard(NULL)) {
	if ((clipdata = GetClipboardData(CF_UNICODETEXT))) {
	    SendMessage(hwnd, WM_GOT_CLIPDATA, (WPARAM)1, (LPARAM)clipdata);
	} else if ((clipdata = GetClipboardData(CF_TEXT))) {
	    SendMessage(hwnd, WM_GOT_CLIPDATA, (WPARAM)0, (LPARAM)clipdata);
	}
	CloseClipboard();
    }

    return 0;
}

int process_clipdata(HGLOBAL clipdata, int unicode)
{
    sfree(clipboard_contents);
    clipboard_contents = NULL;
    clipboard_length = 0;

    if (unicode) {
	wchar_t *p = GlobalLock(clipdata);
	wchar_t *p2;

	if (p) {
	    /* Unwilling to rely on Windows having wcslen() */
	    for (p2 = p; *p2; p2++);
	    clipboard_length = p2 - p;
	    clipboard_contents = snewn(clipboard_length + 1, wchar_t);
	    memcpy(clipboard_contents, p, clipboard_length * sizeof(wchar_t));
	    clipboard_contents[clipboard_length] = L'\0';
	    return TRUE;
	}
    } else {
	char *s = GlobalLock(clipdata);
	int i;

	if (s) {
	    i = MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, 0, 0);
	    clipboard_contents = snewn(i, wchar_t);
	    MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1,
				clipboard_contents, i);
	    clipboard_length = i - 1;
	    clipboard_contents[clipboard_length] = L'\0';
	    return TRUE;
	}
    }

    return FALSE;
}

void request_paste(void *frontend)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    /*
     * I always thought pasting was synchronous in Windows; the
     * clipboard access functions certainly _look_ synchronous,
     * unlike the X ones. But in fact it seems that in some
     * situations the contents of the clipboard might not be
     * immediately available, and the clipboard-reading functions
     * may block. This leads to trouble if the application
     * delivering the clipboard data has to get hold of it by -
     * for example - talking over a network connection which is
     * forwarded through this very PuTTY.
     *
     * Hence, we spawn a subthread to read the clipboard, and do
     * our paste when it's finished. The thread will send a
     * message back to our main window when it terminates, and
     * that tells us it's OK to paste.
     */
    DWORD in_threadid; /* required for Win9x */
    CreateThread(NULL, 0, clipboard_read_threadfunc,
		 tabitem->page.hwndCtrl, 0, &in_threadid);
}

void get_clip(void *frontend, wchar_t **p, int *len)
{
    if (p) {
	*p = clipboard_contents;
	*len = clipboard_length;
    }
}

#if 0
/*
 * Move `lines' lines from position `from' to position `to' in the
 * window.
 */
void optimised_move(void *frontend, int to, int from, int lines)
{
    RECT r;
    int min, max;

    min = (to < from ? to : from);
    max = to + from - min;

    r.left = offset_width;
    r.right = offset_width + term->cols * font_width;
    r.top = offset_height + min * font_height;
    r.bottom = offset_height + (max + lines) * font_height;
    ScrollWindow(hwnd, 0, (to - from) * font_height, &r, &r);
}
#endif

/*
 * Print a message box and perform a fatal exit.
 */
void fatalbox(char *fmt, ...)
{
    va_list ap;
    char *stuff, morestuff[100];

    va_start(ap, fmt);
    stuff = dupvprintf(fmt, ap);
    va_end(ap);
    sprintf(morestuff, "%.70s Fatal Error", appname);
    MessageBox(hwnd, stuff, morestuff, MB_ICONERROR | MB_OK);
    sfree(stuff);
    cleanup_exit(1);
}

/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(char *fmt, ...)
{
    va_list ap;
    char *stuff, morestuff[100];

    va_start(ap, fmt);
    stuff = dupvprintf(fmt, ap);
    va_end(ap);
    sprintf(morestuff, "%.70s Fatal Error", appname);
    MessageBox(hwnd, stuff, morestuff,
	       MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(stuff);
    cleanup_exit(1);
}

DECL_WINDOWS_FUNCTION(static, BOOL, FlashWindowEx, (PFLASHWINFO));

static void init_flashwindow(void)
{
    HMODULE user32_module = load_system32_dll("user32.dll");
    GET_WINDOWS_FUNCTION(user32_module, FlashWindowEx);
}

static BOOL flash_window_ex(DWORD dwFlags, UINT uCount, DWORD dwTimeout)
{
    if (p_FlashWindowEx) {
	FLASHWINFO fi;
	fi.cbSize = sizeof(fi);
	fi.hwnd = hwnd;
	fi.dwFlags = dwFlags;
	fi.uCount = uCount;
	fi.dwTimeout = dwTimeout;
	return (*p_FlashWindowEx)(&fi);
    }
    else
	return FALSE; /* shrug */
}

static void flash_window(int mode);
static long next_flash;
static int flashing = 0;

/*
 * Timer for platforms where we must maintain window flashing manually
 * (e.g., Win95).
 */
static void flash_window_timer(void *ctx, long now)
{
    if (flashing && now - next_flash >= 0) {
	flash_window(1);
    }
}

/*
 * Manage window caption / taskbar flashing, if enabled.
 * 0 = stop, 1 = maintain, 2 = start
 */
static void flash_window(int mode)
{
    if ((mode == 0) || (cfg.beep_ind == B_IND_DISABLED)) {
	/* stop */
	if (flashing) {
	    flashing = 0;
	    if (p_FlashWindowEx)
		flash_window_ex(FLASHW_STOP, 0, 0);
	    else
		FlashWindow(hwnd, FALSE);
	}

    } else if (mode == 2) {
	/* start */
	if (!flashing) {
	    flashing = 1;
	    if (p_FlashWindowEx) {
		/* For so-called "steady" mode, we use uCount=2, which
		 * seems to be the traditional number of flashes used
		 * by user notifications (e.g., by Explorer).
		 * uCount=0 appears to enable continuous flashing, per
		 * "flashing" mode, although I haven't seen this
		 * documented. */
		flash_window_ex(FLASHW_ALL | FLASHW_TIMER,
				(cfg.beep_ind == B_IND_FLASH ? 0 : 2),
				0 /* system cursor blink rate */);
		/* No need to schedule timer */
	    } else {
		FlashWindow(hwnd, TRUE);
		next_flash = schedule_timer(450, flash_window_timer, hwnd);
	    }
	}

    } else if ((mode == 1) && (cfg.beep_ind == B_IND_FLASH)) {
	/* maintain */
	if (flashing && !p_FlashWindowEx) {
	    FlashWindow(hwnd, TRUE);	/* toggle */
	    next_flash = schedule_timer(450, flash_window_timer, hwnd);
	}
    }
}

/*
 * Beep.
 */
void do_beep(void *frontend, int mode)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    if (mode == BELL_DEFAULT) {
	/*
	 * For MessageBeep style bells, we want to be careful of
	 * timing, because they don't have the nice property of
	 * PlaySound bells that each one cancels the previous
	 * active one. So we limit the rate to one per 50ms or so.
	 */
	static long lastbeep = 0;
	long beepdiff;

	beepdiff = GetTickCount() - lastbeep;
	if (beepdiff >= 0 && beepdiff < 50)
	    return;
	MessageBeep(MB_OK);
	/*
	 * The above MessageBeep call takes time, so we record the
	 * time _after_ it finishes rather than before it starts.
	 */
	lastbeep = GetTickCount();
    } else if (mode == BELL_WAVEFILE) {
	if (!PlaySound(tabitem->cfg.bell_wavefile.path, NULL,
		       SND_ASYNC | SND_FILENAME)) {
	    char buf[sizeof(tabitem->cfg.bell_wavefile.path) + 80];
	    char otherbuf[100];
	    sprintf(buf, "Unable to play sound file\n%s\n"
		    "Using default sound instead", tabitem->cfg.bell_wavefile.path);
	    sprintf(otherbuf, "%.70s Sound Error", appname);
	    MessageBox(hwnd, buf, otherbuf,
		       MB_OK | MB_ICONEXCLAMATION);
	    tabitem->cfg.beep = BELL_DEFAULT;
	}
    } else if (mode == BELL_PCSPEAKER) {
	static long lastbeep = 0;
	long beepdiff;

	beepdiff = GetTickCount() - lastbeep;
	if (beepdiff >= 0 && beepdiff < 50)
	    return;

	/*
	 * We must beep in different ways depending on whether this
	 * is a 95-series or NT-series OS.
	 */
	if(osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT)
	    Beep(800, 100);
	else
	    MessageBeep(-1);
	lastbeep = GetTickCount();
    }
    /* Otherwise, either visual bell or disabled; do nothing here */
    if (!tabitem->term->has_focus) {
	flash_window(2);	       /* start */
    }
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void set_iconic(void *frontend, int iconic)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    if (IsIconic(tabitem->page.hwndCtrl)) {
	if (!iconic)
	    ShowWindow(tabitem->page.hwndCtrl, SW_RESTORE);
    } else {
	if (iconic)
	    ShowWindow(tabitem->page.hwndCtrl, SW_MINIMIZE);
    }
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(void *frontend, int x, int y)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    if (tabitem->cfg.resize_action == RESIZE_DISABLED || 
        tabitem->cfg.resize_action == RESIZE_FONT ||
	IsZoomed(hwnd))
       return;

    SetWindowPos(tabitem->page.hwndCtrl, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(void *frontend, int top)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    if (tabitem->cfg.alwaysontop)
	return;			       /* ignore */
    SetWindowPos(tabitem->page.hwndCtrl, top ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
		 SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void *frontend)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    InvalidateRect(tabitem->page.hwndCtrl, NULL, TRUE);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(void *frontend, int zoomed)
{
    if (IsZoomed(hwnd)) {
        if (!zoomed)
	    ShowWindow(hwnd, SW_RESTORE);
    } else {
	if (zoomed)
	    ShowWindow(hwnd, SW_MAXIMIZE);
    }
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void *frontend)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    return IsIconic(tabitem->page.hwndCtrl);
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(void *frontend, int *x, int *y)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    RECT r;
    GetWindowRect(tabitem->page.hwndCtrl, &r);
    *x = r.left;
    *y = r.top;
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void *frontend, int *x, int *y)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    RECT r;
    GetWindowRect(tabitem->page.hwndCtrl, &r);
    *x = r.right - r.left;
    *y = r.bottom - r.top;
}

/*
 * Return the window or icon title.
 */
char *get_window_title(void *frontend, int icon)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    return icon ? tabitem->icon_name : tabitem->window_name;
}

/*
 * See if we're in full-screen mode.
 */
static int is_full_screen()
{
    //if (!IsZoomed(hwnd))
	//return FALSE;
    //if (!(GetMenuState(popup_menus[0].menu, IDM_FULLSCREEN,MF_BYCOMMAND) & MF_CHECKED))
	return FALSE;
    //return TRUE;
}

/* Get the rect/size of a full screen window using the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static int get_fullscreen_rect(RECT * ss)
{
#if defined(MONITOR_DEFAULTTONEAREST) && !defined(NO_MULTIMON)
	HMONITOR mon;
	MONITORINFO mi;
	mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(mon, &mi);

	/* structure copy */
	*ss = mi.rcMonitor;
	return TRUE;
#else
/* could also use code like this:
	ss->left = ss->top = 0;
	ss->right = GetSystemMetrics(SM_CXSCREEN);
	ss->bottom = GetSystemMetrics(SM_CYSCREEN);
*/ 
	return GetClientRect(GetDesktopWindow(), ss);
#endif
}


/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void make_full_screen(wintabitem* tabitem)
{
    DWORD win_style;
    DWORD page_style;
	RECT ss;

    assert(IsZoomed(hwnd));

	if (is_full_screen())
		return;
	
    /* Remove the window furniture. */
    win_style = GetWindowLongPtr(hwnd, GWL_STYLE);
    win_style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    SetWindowLongPtr(hwnd, GWL_STYLE, win_style);

    /* remove the page scroll bar */
    page_style = GetWindowLongPtr(tabitem->page.hwndCtrl, GWL_STYLE);
    if (tabitem->cfg.scrollbar_in_fullscreen)
    	page_style |= WS_VSCROLL;
    else
    	page_style &= ~WS_VSCROLL; 
    SetWindowLongPtr(tabitem->page.hwndCtrl, GWL_STYLE, page_style);

    /* Resize ourselves to exactly cover the nearest monitor. */
	get_fullscreen_rect(&ss);
    SetWindowPos(hwnd, HWND_TOP, ss.left, ss.top,
			ss.right - ss.left,
			ss.bottom - ss.top,
			SWP_FRAMECHANGED);
    SetWindowPos(tabitem->page.hwndCtrl, HWND_TOPMOST, ss.left, ss.top,
			ss.right - ss.left,
			ss.bottom - ss.top,
			SWP_FRAMECHANGED);
    /* We may have changed size as a result */

    reset_window(tabitem, 0);

    /* Tick the menu item in the System and context menus. */
    {
	int i;
	for (i = 0; i < lenof(popup_menus); i++)
	    CheckMenuItem(popup_menus[i].menu, IDM_FULLSCREEN, MF_CHECKED);
    }
}

/*
 * Clear the full-screen attributes.
 */
static void clear_full_screen(wintabitem* tabitem)
{
    DWORD oldstyle, style;
    DWORD page_oldstyle, page_style;

    /* Reinstate the window furniture. */
    style = oldstyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    if (style != oldstyle) {
    	SetWindowLongPtr(hwnd, GWL_STYLE, style);
    	SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
    		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
    		     SWP_FRAMECHANGED);
    }

    /* page resize */
    page_style = page_oldstyle = GetWindowLongPtr(tabitem->page.hwndCtrl, GWL_STYLE);
    if (tabitem->cfg.scrollbar)
    	page_style |= WS_VSCROLL;
    else
    	page_style &= ~WS_VSCROLL;
    if (page_style != page_oldstyle) {
    	SetWindowLongPtr(tabitem->page.hwndCtrl, GWL_STYLE, page_style);	
    }
    RECT rc;
    GetClientRect(hwnd, &rc);
    wintab_resize(&tab, &rc);

    /* Untick the menu item in the System and context menus. */
    {
	int i;
	for (i = 0; i < lenof(popup_menus); i++)
	    CheckMenuItem(popup_menus[i].menu, IDM_FULLSCREEN, MF_UNCHECKED);
    }
}

/*
 * Toggle full-screen mode.
 */
static void flip_full_screen()
{
    if (is_full_screen()) {
	ShowWindow(hwnd, SW_RESTORE);
    } else if (IsZoomed(hwnd)) {
	make_full_screen(wintab_get_active_item(&tab));
    } else {
	SendMessage(hwnd, WM_FULLSCR_ON_MAX, 0, 0);
	ShowWindow(hwnd, SW_MAXIMIZE);
    }
}

void frontend_keypress(void *handle)
{
    /*
     * Keypress termination in non-Close-On-Exit mode is not
     * currently supported in PuTTY proper, because the window
     * always has a perfectly good Close button anyway. So we do
     * nothing here.
     */
    return;
}

int from_backend(void *frontend, int is_stderr, const char *data, int len)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    return term_data(tabitem->term, is_stderr, data, len);
}

int from_backend_untrusted(void *frontend, const char *data, int len)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    return term_data_untrusted(tabitem->term, data, len);
}

int get_userpass_input(void *frontend, Config *cfg, prompts_t *p, unsigned char *in, int inlen)
{
    assert (frontend != NULL);
    wintabitem *tabitem = (wintabitem*) frontend;
    int ret;
    ret = autocmd_get_passwd_input(p, cfg);
    if (ret == -1)
        ret = cmdline_get_passwd_input(p, in, inlen);
    if (ret == -1)
    	ret = term_get_userpass_input(tabitem->term, p, in, inlen);
    return ret;
}

void agent_schedule_callback(void (*callback)(void *, void *, int),
			     void *callback_ctx, void *data, int len)
{
    struct agent_callback *c = snew(struct agent_callback);
    c->callback = callback;
    c->callback_ctx = callback_ctx;
    c->data = data;
    c->len = len;
    PostMessage(hwnd, WM_AGENT_CALLBACK, 0, (LPARAM)c);
}
