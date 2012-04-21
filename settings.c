/*
 * settings.c: read and write saved sessions. (platform-independent)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "putty.h"
#include "storage.h"

/* The cipher order given here is the default order. */
static const struct keyval ciphernames[] = {
    { "aes",	    CIPHER_AES },
    { "blowfish",   CIPHER_BLOWFISH },
    { "3des",	    CIPHER_3DES },
    { "WARN",	    CIPHER_WARN },
    { "arcfour",    CIPHER_ARCFOUR },
    { "des",	    CIPHER_DES }
};

static const struct keyval kexnames[] = {
    { "dh-gex-sha1",	    KEX_DHGEX },
    { "dh-group14-sha1",    KEX_DHGROUP14 },
    { "dh-group1-sha1",	    KEX_DHGROUP1 },
    { "rsa",		    KEX_RSA },
    { "WARN",		    KEX_WARN }
};

/*
 * All the terminal modes that we know about for the "TerminalModes"
 * setting. (Also used by config.c for the drop-down list.)
 * This is currently precisely the same as the set in ssh.c, but could
 * in principle differ if other backends started to support tty modes
 * (e.g., the pty backend).
 */
const char *const ttymodes[] = {
    "INTR",	"QUIT",     "ERASE",	"KILL",     "EOF",
    "EOL",	"EOL2",     "START",	"STOP",     "SUSP",
    "DSUSP",	"REPRINT",  "WERASE",	"LNEXT",    "FLUSH",
    "SWTCH",	"STATUS",   "DISCARD",	"IGNPAR",   "PARMRK",
    "INPCK",	"ISTRIP",   "INLCR",	"IGNCR",    "ICRNL",
    "IUCLC",	"IXON",     "IXANY",	"IXOFF",    "IMAXBEL",
    "ISIG",	"ICANON",   "XCASE",	"ECHO",     "ECHOE",
    "ECHOK",	"ECHONL",   "NOFLSH",	"TOSTOP",   "IEXTEN",
    "ECHOCTL",	"ECHOKE",   "PENDIN",	"OPOST",    "OLCUC",
    "ONLCR",	"OCRNL",    "ONOCR",	"ONLRET",   "CS7",
    "CS8",	"PARENB",   "PARODD",	NULL
};

/*
 * Convenience functions to access the backends[] array
 * (which is only present in tools that manage settings).
 */

Backend *backend_from_name(const char *name)
{
    Backend **p;
    for (p = backends; *p != NULL; p++)
	if (!strcmp((*p)->name, name))
	    return *p;
    return NULL;
}

Backend *backend_from_proto(int proto)
{
    Backend **p;
    for (p = backends; *p != NULL; p++)
	if ((*p)->protocol == proto)
	    return *p;
    return NULL;
}

int get_remote_username(Config *cfg, char *user, size_t len)
{
    if (*cfg->username) {
	strncpy(user, cfg->username, len);
	user[len-1] = '\0';
    } else {
	if (cfg->username_from_env) {
	    /* Use local username. */
	    char *luser = get_username();
	    if (luser) {
		strncpy(user, luser, len);
		user[len-1] = '\0';
		sfree(luser);
	    } else {
		*user = '\0';
	    }
	} else {
	    *user = '\0';
	}
    }
    return (*user != '\0');
}

static void gpps(IStore* iStorage, void *handle, const char *name, const char *def,
		 char *val, int len)
{
    if (!iStorage->read_setting_s(handle, name, val, len)) {
	char *pdef;

	pdef = platform_default_s(name);
	if (pdef) {
	    strncpy(val, pdef, len);
	    sfree(pdef);
	} else {
	    strncpy(val, def, len);
	}

	val[len - 1] = '\0';
    }
}

/*
 * gppfont and gppfile cannot have local defaults, since the very
 * format of a Filename or Font is platform-dependent. So the
 * platform-dependent functions MUST return some sort of value.
 */
static void gppfont(IStore* iStorage, void *handle, const char *name, FontSpec *result)
{
    if (!iStorage->read_setting_fontspec(handle, name, result))
	*result = platform_default_fontspec(name);
}
static void gppfile(IStore* iStorage, void *handle, const char *name, Filename *result)
{
    if (!iStorage->read_setting_filename(handle, name, result))
	*result = platform_default_filename(name);
}

static void gppi(IStore* iStorage, void *handle, char *name, int def, int *i)
{
    def = platform_default_i(name, def);
    *i = iStorage->read_setting_i(handle, name, def);
}

/*
 * Read a set of name-value pairs in the format we occasionally use:
 *   NAME\tVALUE\0NAME\tVALUE\0\0 in memory
 *   NAME=VALUE,NAME=VALUE, in storage
 * `def' is in the storage format.
 */
static void gppmap(IStore* iStorage, void *handle, char *name, char *def, char *val, int len)
{
    char *buf = snewn(2*len, char), *p, *q;
    gpps(iStorage, handle, name, def, buf, 2*len);
    p = buf;
    q = val;
    while (*p) {
	while (*p && *p != ',') {
	    int c = *p++;
	    if (c == '=')
		c = '\t';
	    if (c == '\\')
		c = *p++;
	    *q++ = c;
	}
	if (*p == ',')
	    p++;
	*q++ = '\0';
    }
    *q = '\0';
    sfree(buf);
}

/*
 * Write a set of name/value pairs in the above format.
 */
static void wmap(IStore* iStorage, void *handle, char const *key, char const *value, int len)
{
    char *buf = snewn(2*len, char), *p;
    const char *q;
    p = buf;
    q = value;
    while (*q) {
	while (*q) {
	    int c = *q++;
	    if (c == '=' || c == ',' || c == '\\')
		*p++ = '\\';
	    if (c == '\t')
		c = '=';
	    *p++ = c;
	}
	*p++ = ',';
	q++;
    }
    *p = '\0';
    iStorage->write_setting_s(handle, key, buf);
    sfree(buf);
}

static int key2val(const struct keyval *mapping, int nmaps, char *key)
{
    int i;
    for (i = 0; i < nmaps; i++)
	if (!strcmp(mapping[i].s, key)) return mapping[i].v;
    return -1;
}

static const char *val2key(const struct keyval *mapping, int nmaps, int val)
{
    int i;
    for (i = 0; i < nmaps; i++)
	if (mapping[i].v == val) return mapping[i].s;
    return NULL;
}

/*
 * Helper function to parse a comma-separated list of strings into
 * a preference list array of values. Any missing values are added
 * to the end and duplicates are weeded.
 * XXX: assumes vals in 'mapping' are small +ve integers
 */
static void gprefs(IStore* iStorage, void *sesskey, char *name, char *def,
		   const struct keyval *mapping, int nvals,
		   int *array)
{
    char commalist[80];
    char *tokarg = commalist;
    int n;
    unsigned long seen = 0;	       /* bitmap for weeding dups etc */
    gpps(iStorage, sesskey, name, def, commalist, sizeof(commalist));

    /* Grotty parsing of commalist. */
    n = 0;
    do {
	int v;
	char *key;
	key = strtok(tokarg, ","); /* sorry */
	tokarg = NULL;
	if (!key) break;
	if (((v = key2val(mapping, nvals, key)) != -1) &&
	    !(seen & 1<<v)) {
	    array[n] = v;
	    n++;
	    seen |= 1<<v;
	}
    } while (n < nvals);
    /* Add any missing values (backward compatibility ect). */
    {
	int i;
	for (i = 0; i < nvals; i++) {
	    assert(mapping[i].v < 32);
	    if (!(seen & 1<<mapping[i].v)) {
		array[n] = mapping[i].v;
		n++;
	    }
	}
    }
}

/* 
 * Write out a preference list.
 */
static void wprefs(IStore* iStorage, void *sesskey, char *name,
		   const struct keyval *mapping, int nvals,
		   int *array)
{
    char buf[80] = "";	/* XXX assumed big enough */
    int l = sizeof(buf)-1, i;
    buf[l] = '\0';
    for (i = 0; l > 0 && i < nvals; i++) {
	const char *s = val2key(mapping, nvals, array[i]);
	if (s) {
	    int sl = strlen(s);
	    if (i > 0) {
		strncat(buf, ",", l);
		l--;
	    }
	    strncat(buf, s, l);
	    l -= sl;
	}
    }
    iStorage->write_setting_s(sesskey, name, buf);
}

char *save_settings(const char *section, Config * cfg)
{
    void *sesskey;
    char *errmsg;

    if (!strcmp(section, DEFAULT_SESSION_NAME)) return NULL;

    //debug
    /*
    {
        Config tmpCfg;
        load_settings(section, &tmpCfg);
        if (*tmpCfg.host){
            assert (!strcmp(tmpCfg.host, cfg->host)
                && tmpCfg.port == cfg->port);
        }
    }
    */
    sesskey = gStorage->open_settings_w(section, &errmsg);
    if (!sesskey)
	return errmsg;
    save_open_settings(gStorage, sesskey, cfg);
    gStorage->close_settings_w(sesskey);
    return NULL;
}

char *backup_settings(const char *section,const char* path)
{
    void *sesskey;
    char *errmsg;
	FileStore fileStore(path);
	Config cfg;

    if (!strcmp(section, DEFAULT_SESSION_NAME)) return NULL;

    sesskey = fileStore.open_settings_w(section, &errmsg);
    if (!sesskey)
	return errmsg;
	load_settings(section, &cfg);
    save_open_settings(&fileStore, sesskey, &cfg);
    fileStore.close_settings_w(sesskey);
    return NULL;
}

char *save_isetting(const char *section, char* setting, int value)
{
    void *sesskey;
    char *errmsg;

    if (!strcmp(section, DEFAULT_SESSION_NAME) || !setting || !*setting) 
        return NULL;
    sesskey = gStorage->open_settings_w(section, &errmsg);
    if (!sesskey)
	return errmsg;
    gStorage->write_setting_i(sesskey, setting, value);
    gStorage->close_settings_w(sesskey);
    return NULL;
}

void save_open_settings(IStore* iStorage, void *sesskey, Config *cfg)
{
    int i;
    char *p;

    iStorage->write_setting_i(sesskey, "Present", 1);
    iStorage->write_setting_s(sesskey, "HostName", cfg->host);
    iStorage->write_setting_filename(sesskey, "LogFileName", cfg->logfilename);
    iStorage->write_setting_i(sesskey, "LogType", cfg->logtype);
    iStorage->write_setting_i(sesskey, "LogFileClash", cfg->logxfovr);
    iStorage->write_setting_i(sesskey, "LogFlush", cfg->logflush);
    iStorage->write_setting_i(sesskey, "SSHLogOmitPasswords", cfg->logomitpass);
    iStorage->write_setting_i(sesskey, "SSHLogOmitData", cfg->logomitdata);
    p = "raw";
    {
	const Backend *b = backend_from_proto(cfg->protocol);
	if (b)
	    p = b->name;
    }
    iStorage->write_setting_s(sesskey, "Protocol", p);
    iStorage->write_setting_i(sesskey, "PortNumber", cfg->port);
    /* The CloseOnExit numbers are arranged in a different order from
     * the standard FORCE_ON / FORCE_OFF / AUTO. */
    iStorage->write_setting_i(sesskey, "CloseOnExit", (cfg->close_on_exit+2)%3);
    iStorage->write_setting_i(sesskey, "WarnOnClose", !!cfg->warn_on_close);
    iStorage->write_setting_i(sesskey, "PingInterval", cfg->ping_interval / 60);	/* minutes */
    iStorage->write_setting_i(sesskey, "PingIntervalSecs", cfg->ping_interval % 60);	/* seconds */
    iStorage->write_setting_i(sesskey, "TCPNoDelay", cfg->tcp_nodelay);
    iStorage->write_setting_i(sesskey, "TCPKeepalives", cfg->tcp_keepalives);
    iStorage->write_setting_s(sesskey, "TerminalType", cfg->termtype);
    iStorage->write_setting_s(sesskey, "TerminalSpeed", cfg->termspeed);
    wmap(iStorage, sesskey, "TerminalModes", cfg->ttymodes, lenof(cfg->ttymodes));

    /* Address family selection */
    iStorage->write_setting_i(sesskey, "AddressFamily", cfg->addressfamily);

    /* proxy settings */
    iStorage->write_setting_s(sesskey, "ProxyExcludeList", cfg->proxy_exclude_list);
    iStorage->write_setting_i(sesskey, "ProxyDNS", (cfg->proxy_dns+2)%3);
    iStorage->write_setting_i(sesskey, "ProxyLocalhost", cfg->even_proxy_localhost);
    iStorage->write_setting_i(sesskey, "ProxyMethod", cfg->proxy_type);
    iStorage->write_setting_s(sesskey, "ProxyHost", cfg->proxy_host);
    iStorage->write_setting_i(sesskey, "ProxyPort", cfg->proxy_port);
    iStorage->write_setting_s(sesskey, "ProxyUsername", cfg->proxy_username);
    iStorage->write_setting_s(sesskey, "ProxyPassword", cfg->proxy_password);
    iStorage->write_setting_s(sesskey, "ProxyTelnetCommand", cfg->proxy_telnet_command);
    wmap(iStorage, sesskey, "Environment", cfg->environmt, lenof(cfg->environmt));
    iStorage->write_setting_s(sesskey, "UserName", cfg->username);
    iStorage->write_setting_i(sesskey, "UserNameFromEnvironment", cfg->username_from_env);
    iStorage->write_setting_s(sesskey, "LocalUserName", cfg->localusername);
    iStorage->write_setting_i(sesskey, "NoPTY", cfg->nopty);
    iStorage->write_setting_i(sesskey, "Compression", cfg->compression);
    iStorage->write_setting_i(sesskey, "TryAgent", cfg->tryagent);
    iStorage->write_setting_i(sesskey, "AgentFwd", cfg->agentfwd);
    iStorage->write_setting_i(sesskey, "GssapiFwd", cfg->gssapifwd);
    iStorage->write_setting_i(sesskey, "ChangeUsername", cfg->change_username);
    wprefs(iStorage, sesskey, "Cipher", ciphernames, CIPHER_MAX,
	   cfg->ssh_cipherlist);
    wprefs(iStorage, sesskey, "KEX", kexnames, KEX_MAX, cfg->ssh_kexlist);
    iStorage->write_setting_i(sesskey, "RekeyTime", cfg->ssh_rekey_time);
    iStorage->write_setting_s(sesskey, "RekeyBytes", cfg->ssh_rekey_data);
    iStorage->write_setting_i(sesskey, "SshNoAuth", cfg->ssh_no_userauth);
    iStorage->write_setting_i(sesskey, "SshBanner", cfg->ssh_show_banner);
    iStorage->write_setting_i(sesskey, "AuthTIS", cfg->try_tis_auth);
    iStorage->write_setting_i(sesskey, "AuthKI", cfg->try_ki_auth);
    iStorage->write_setting_i(sesskey, "AuthGSSAPI", cfg->try_gssapi_auth);
#ifndef NO_GSSAPI
    wprefs(iStorage, sesskey, "GSSLibs", gsslibkeywords, ngsslibs,
	   cfg->ssh_gsslist);
    iStorage->write_setting_filename(sesskey, "GSSCustom", cfg->ssh_gss_custom);
#endif
    iStorage->write_setting_i(sesskey, "SshNoShell", cfg->ssh_no_shell);
    iStorage->write_setting_i(sesskey, "SshProt", cfg->sshprot);
    iStorage->write_setting_s(sesskey, "LogHost", cfg->loghost);
    iStorage->write_setting_i(sesskey, "SSH2DES", cfg->ssh2_des_cbc);
    iStorage->write_setting_filename(sesskey, "PublicKeyFile", cfg->keyfile);
    iStorage->write_setting_s(sesskey, "RemoteCommand", cfg->remote_cmd);
    iStorage->write_setting_i(sesskey, "RFCEnviron", cfg->rfc_environ);
    iStorage->write_setting_i(sesskey, "PassiveTelnet", cfg->passive_telnet);
    iStorage->write_setting_i(sesskey, "BackspaceIsDelete", cfg->bksp_is_delete);
    iStorage->write_setting_i(sesskey, "RXVTHomeEnd", cfg->rxvt_homeend);
    iStorage->write_setting_i(sesskey, "LinuxFunctionKeys", cfg->funky_type);
    iStorage->write_setting_i(sesskey, "NoApplicationKeys", cfg->no_applic_k);
    iStorage->write_setting_i(sesskey, "NoApplicationCursors", cfg->no_applic_c);
    iStorage->write_setting_i(sesskey, "NoMouseReporting", cfg->no_mouse_rep);
    iStorage->write_setting_i(sesskey, "NoRemoteResize", cfg->no_remote_resize);
    iStorage->write_setting_i(sesskey, "NoAltScreen", cfg->no_alt_screen);
    iStorage->write_setting_i(sesskey, "NoRemoteWinTitle", cfg->no_remote_wintitle);
    iStorage->write_setting_i(sesskey, "NoRemoteTabName", cfg->no_remote_tabname);
    iStorage->write_setting_i(sesskey, "RemoteQTitleAction", cfg->remote_qtitle_action);
    iStorage->write_setting_i(sesskey, "NoDBackspace", cfg->no_dbackspace);
    iStorage->write_setting_i(sesskey, "NoRemoteCharset", cfg->no_remote_charset);
    iStorage->write_setting_i(sesskey, "ApplicationCursorKeys", cfg->app_cursor);
    iStorage->write_setting_i(sesskey, "ApplicationKeypad", cfg->app_keypad);
    iStorage->write_setting_i(sesskey, "NetHackKeypad", cfg->nethack_keypad);
    iStorage->write_setting_i(sesskey, "AltF4", cfg->alt_f4);
    iStorage->write_setting_i(sesskey, "AltSpace", cfg->alt_space);
    iStorage->write_setting_i(sesskey, "AltOnly", cfg->alt_only);
    iStorage->write_setting_i(sesskey, "ComposeKey", cfg->compose_key);
    iStorage->write_setting_i(sesskey, "CtrlAltKeys", cfg->ctrlaltkeys);
    iStorage->write_setting_i(sesskey, "TelnetKey", cfg->telnet_keyboard);
    iStorage->write_setting_i(sesskey, "TelnetRet", cfg->telnet_newline);
    iStorage->write_setting_i(sesskey, "LocalEcho", cfg->localecho);
    iStorage->write_setting_i(sesskey, "LocalEdit", cfg->localedit);
    iStorage->write_setting_s(sesskey, "Answerback", cfg->answerback);
    iStorage->write_setting_i(sesskey, "AlwaysOnTop", cfg->alwaysontop);
    iStorage->write_setting_i(sesskey, "FullScreenOnAltEnter", cfg->fullscreenonaltenter);
    iStorage->write_setting_i(sesskey, "HideMousePtr", cfg->hide_mouseptr);
    iStorage->write_setting_i(sesskey, "SunkenEdge", cfg->sunken_edge);
    iStorage->write_setting_i(sesskey, "WindowBorder", cfg->window_border);
    iStorage->write_setting_i(sesskey, "CurType", cfg->cursor_type);
    iStorage->write_setting_i(sesskey, "BlinkCur", cfg->blink_cur);
    iStorage->write_setting_i(sesskey, "Beep", cfg->beep);
    iStorage->write_setting_i(sesskey, "BeepInd", cfg->beep_ind);
    iStorage->write_setting_filename(sesskey, "BellWaveFile", cfg->bell_wavefile);
    iStorage->write_setting_i(sesskey, "BellOverload", cfg->bellovl);
    iStorage->write_setting_i(sesskey, "BellOverloadN", cfg->bellovl_n);
    iStorage->write_setting_i(sesskey, "BellOverloadT", cfg->bellovl_t
#ifdef PUTTY_UNIX_H
		    * 1000
#endif
		    );
    iStorage->write_setting_i(sesskey, "BellOverloadS", cfg->bellovl_s
#ifdef PUTTY_UNIX_H
		    * 1000
#endif
		    );
    iStorage->write_setting_i(sesskey, "ScrollbackLines", cfg->savelines);
    iStorage->write_setting_i(sesskey, "LinesAtAScroll", cfg->scrolllines);
    iStorage->write_setting_i(sesskey, "DECOriginMode", cfg->dec_om);
    iStorage->write_setting_i(sesskey, "AutoWrapMode", cfg->wrap_mode);
    iStorage->write_setting_i(sesskey, "LFImpliesCR", cfg->lfhascr);
    iStorage->write_setting_i(sesskey, "CRImpliesLF", cfg->crhaslf);
    iStorage->write_setting_i(sesskey, "DisableArabicShaping", cfg->arabicshaping);
    iStorage->write_setting_i(sesskey, "DisableBidi", cfg->bidi);
    iStorage->write_setting_i(sesskey, "WinNameAlways", cfg->win_name_always);
    iStorage->write_setting_s(sesskey, "WinTitle", cfg->wintitle);
    iStorage->write_setting_i(sesskey, "TermWidth", cfg->width);
    iStorage->write_setting_i(sesskey, "TermHeight", cfg->height);
    iStorage->write_setting_fontspec(sesskey, "Font", cfg->font);
    iStorage->write_setting_i(sesskey, "FontQuality", cfg->font_quality);
    iStorage->write_setting_i(sesskey, "FontVTMode", cfg->vtmode);
    iStorage->write_setting_i(sesskey, "UseSystemColours", cfg->system_colour);
    iStorage->write_setting_i(sesskey, "TryPalette", cfg->try_palette);
    iStorage->write_setting_i(sesskey, "ANSIColour", cfg->ansi_colour);
    iStorage->write_setting_i(sesskey, "Xterm256Colour", cfg->xterm_256_colour);
    iStorage->write_setting_i(sesskey, "BoldAsColour", cfg->bold_colour);

    for (i = 0; i < 22; i++) {
	char buf[20], buf2[30];
	sprintf(buf, "Colour%d", i);
	sprintf(buf2, "%d,%d,%d", cfg->colours[i][0],
		cfg->colours[i][1], cfg->colours[i][2]);
	iStorage->write_setting_s(sesskey, buf, buf2);
    }
    iStorage->write_setting_i(sesskey, "RawCNP", cfg->rawcnp);
    iStorage->write_setting_i(sesskey, "PasteRTF", cfg->rtf_paste);
    iStorage->write_setting_i(sesskey, "MouseIsXterm", cfg->mouse_is_xterm);
    iStorage->write_setting_i(sesskey, "RectSelect", cfg->rect_select);
    iStorage->write_setting_i(sesskey, "MouseOverride", cfg->mouse_override);
    for (i = 0; i < 256; i += 32) {
	char buf[20], buf2[256];
	int j;
	sprintf(buf, "Wordness%d", i);
	*buf2 = '\0';
	for (j = i; j < i + 32; j++) {
	    sprintf(buf2 + strlen(buf2), "%s%d",
		    (*buf2 ? "," : ""), cfg->wordness[j]);
	}
	iStorage->write_setting_s(sesskey, buf, buf2);
    }
    iStorage->write_setting_s(sesskey, "LineCodePage", cfg->line_codepage);
    iStorage->write_setting_i(sesskey, "CJKAmbigWide", cfg->cjk_ambig_wide);
    iStorage->write_setting_i(sesskey, "UTF8Override", cfg->utf8_override);
    iStorage->write_setting_s(sesskey, "Printer", cfg->printer);
    iStorage->write_setting_i(sesskey, "CapsLockCyr", cfg->xlat_capslockcyr);
    iStorage->write_setting_i(sesskey, "ScrollBar", cfg->scrollbar);
    iStorage->write_setting_i(sesskey, "ScrollBarFullScreen", cfg->scrollbar_in_fullscreen);
    iStorage->write_setting_i(sesskey, "ScrollOnKey", cfg->scroll_on_key);
    iStorage->write_setting_i(sesskey, "ScrollOnDisp", cfg->scroll_on_disp);
    iStorage->write_setting_i(sesskey, "EraseToScrollback", cfg->erase_to_scrollback);
    iStorage->write_setting_i(sesskey, "LockSize", cfg->resize_action);
    iStorage->write_setting_i(sesskey, "BCE", cfg->bce);
    iStorage->write_setting_i(sesskey, "BlinkText", cfg->blinktext);
    iStorage->write_setting_i(sesskey, "X11Forward", cfg->x11_forward);
    iStorage->write_setting_s(sesskey, "X11Display", cfg->x11_display);
    iStorage->write_setting_i(sesskey, "X11AuthType", cfg->x11_auth);
    iStorage->write_setting_filename(sesskey, "X11AuthFile", cfg->xauthfile);
    iStorage->write_setting_i(sesskey, "LocalPortAcceptAll", cfg->lport_acceptall);
    iStorage->write_setting_i(sesskey, "RemotePortAcceptAll", cfg->rport_acceptall);
    wmap(iStorage, sesskey, "PortForwardings", cfg->portfwd, lenof(cfg->portfwd));
    iStorage->write_setting_i(sesskey, "BugIgnore1", 2-cfg->sshbug_ignore1);
    iStorage->write_setting_i(sesskey, "BugPlainPW1", 2-cfg->sshbug_plainpw1);
    iStorage->write_setting_i(sesskey, "BugRSA1", 2-cfg->sshbug_rsa1);
    iStorage->write_setting_i(sesskey, "BugIgnore2", 2-cfg->sshbug_ignore2);
    iStorage->write_setting_i(sesskey, "BugHMAC2", 2-cfg->sshbug_hmac2);
    iStorage->write_setting_i(sesskey, "BugDeriveKey2", 2-cfg->sshbug_derivekey2);
    iStorage->write_setting_i(sesskey, "BugRSAPad2", 2-cfg->sshbug_rsapad2);
    iStorage->write_setting_i(sesskey, "BugPKSessID2", 2-cfg->sshbug_pksessid2);
    iStorage->write_setting_i(sesskey, "BugRekey2", 2-cfg->sshbug_rekey2);
    iStorage->write_setting_i(sesskey, "BugMaxPkt2", 2-cfg->sshbug_maxpkt2);
    iStorage->write_setting_i(sesskey, "StampUtmp", cfg->stamp_utmp);
    iStorage->write_setting_i(sesskey, "LoginShell", cfg->login_shell);
    iStorage->write_setting_i(sesskey, "ScrollbarOnLeft", cfg->scrollbar_on_left);
    iStorage->write_setting_fontspec(sesskey, "BoldFont", cfg->boldfont);
    iStorage->write_setting_fontspec(sesskey, "WideFont", cfg->widefont);
    iStorage->write_setting_fontspec(sesskey, "WideBoldFont", cfg->wideboldfont);
    iStorage->write_setting_i(sesskey, "ShadowBold", cfg->shadowbold);
    iStorage->write_setting_i(sesskey, "ShadowBoldOffset", cfg->shadowboldoffset);
    iStorage->write_setting_s(sesskey, "SerialLine", cfg->serline);
    iStorage->write_setting_i(sesskey, "SerialSpeed", cfg->serspeed);
    iStorage->write_setting_i(sesskey, "SerialDataBits", cfg->serdatabits);
    iStorage->write_setting_i(sesskey, "SerialStopHalfbits", cfg->serstopbits);
    iStorage->write_setting_i(sesskey, "SerialParity", cfg->serparity);
    iStorage->write_setting_i(sesskey, "SerialFlowControl", cfg->serflow);
    iStorage->write_setting_s(sesskey, "WindowClass", cfg->winclass);

    for (i = 0; i < AUTOCMD_COUNT; i++){
        char buf[20];
	    sprintf(buf, "AutocmdExpect%d", i);
        iStorage->write_setting_s(sesskey, buf, cfg->expect[i]);
        sprintf(buf, "Autocmd%d", i);
        iStorage->write_setting_s(sesskey, buf, cfg->autocmd[i]);
        sprintf(buf, "AutocmdHide%d", i);
        iStorage->write_setting_i(sesskey, buf, cfg->autocmd_hide[i]);
        sprintf(buf, "AutocmdEnable%d", i);
        iStorage->write_setting_i(sesskey, buf, cfg->autocmd_enable[i]);
    }
}

void load_settings(const char *section, Config * cfg)
{
    void *sesskey;

    sesskey = gStorage->open_settings_r(section);
    load_open_settings(gStorage, sesskey, cfg);
    gStorage->close_settings_r(sesskey);
/*
	gStorage->open_read_settings_s(
		"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 
		"Desktop",
		cfg->default_log_path, 
		sizeof(cfg->default_log_path));
		*/
	
    if (!section || !*section)
		strncpy(cfg->session_name, DEFAULT_SESSION_NAME, sizeof cfg->session_name);
	else 
		strncpy(cfg->session_name, section, sizeof cfg->session_name);

    if (cfg_launchable(cfg))
        add_session_to_jumplist(section);

}

int load_isetting(const char *section, char* setting, int defvalue)
{
    void *sesskey;
    int res = 0;

    sesskey = gStorage->open_settings_r(section);
    gppi(gStorage,  sesskey, setting, defvalue, &res);
    gStorage->close_settings_r(sesskey);
    return res;
}

void load_open_settings(IStore* iStorage, void *sesskey, Config *cfg)
{
    int i;
    char prot[10];

    cfg->ssh_subsys = 0;	       /* FIXME: load this properly */
    cfg->remote_cmd_ptr = NULL;
    cfg->remote_cmd_ptr2 = NULL;
    cfg->ssh_nc_host[0] = '\0';

    gpps(iStorage,  sesskey, "HostName", "", cfg->host, sizeof(cfg->host));
    gppfile(iStorage,  sesskey, "LogFileName", &cfg->logfilename);
    gppi(iStorage,  sesskey, "LogType", 0, &cfg->logtype);
    gppi(iStorage,  sesskey, "LogFileClash", LGXF_ASK, &cfg->logxfovr);
    gppi(iStorage,  sesskey, "LogFlush", 1, &cfg->logflush);
    gppi(iStorage,  sesskey, "SSHLogOmitPasswords", 1, &cfg->logomitpass);
    gppi(iStorage,  sesskey, "SSHLogOmitData", 0, &cfg->logomitdata);

    gpps(iStorage,  sesskey, "Protocol", "default", prot, 10);
    cfg->protocol = default_protocol;
    cfg->port = default_port;
    {
	const Backend *b = backend_from_name(prot);
	if (b) {
	    cfg->protocol = b->protocol;
	    gppi(iStorage,  sesskey, "PortNumber", default_port, &cfg->port);
	}
    }

    /* Address family selection */
    gppi(iStorage,  sesskey, "AddressFamily", ADDRTYPE_UNSPEC, &cfg->addressfamily);

    /* The CloseOnExit numbers are arranged in a different order from
     * the standard FORCE_ON / FORCE_OFF / AUTO. */
    gppi(iStorage,  sesskey, "CloseOnExit", 1, &i); cfg->close_on_exit = (i+1)%3;
    gppi(iStorage,  sesskey, "WarnOnClose", 1, &cfg->warn_on_close);
    {
	/* This is two values for backward compatibility with 0.50/0.51 */
	int pingmin, pingsec;
	gppi(iStorage,  sesskey, "PingInterval", 0, &pingmin);
	gppi(iStorage,  sesskey, "PingIntervalSecs", 0, &pingsec);
	cfg->ping_interval = pingmin * 60 + pingsec;
    }
    gppi(iStorage,  sesskey, "TCPNoDelay", 1, &cfg->tcp_nodelay);
    gppi(iStorage,  sesskey, "TCPKeepalives", 0, &cfg->tcp_keepalives);
    gpps(iStorage,  sesskey, "TerminalType", "xterm", cfg->termtype,
	 sizeof(cfg->termtype));
    gpps(iStorage,  sesskey, "TerminalSpeed", "38400,38400", cfg->termspeed,
	 sizeof(cfg->termspeed));
    {
	/* This hardcodes a big set of defaults in any new saved
	 * sessions. Let's hope we don't change our mind. */
	int i;
	char *def = dupstr("");
	/* Default: all set to "auto" */
	for (i = 0; ttymodes[i]; i++) {
	    char *def2 = dupprintf("%s%s=A,", def, ttymodes[i]);
	    sfree(def);
	    def = def2;
	}
	gppmap(iStorage,  sesskey, "TerminalModes", def,
	       cfg->ttymodes, lenof(cfg->ttymodes));
	sfree(def);
    }

    /* proxy settings */
    gpps(iStorage,  sesskey, "ProxyExcludeList", "", cfg->proxy_exclude_list,
	 sizeof(cfg->proxy_exclude_list));
    gppi(iStorage,  sesskey, "ProxyDNS", 1, &i); cfg->proxy_dns = (i+1)%3;
    gppi(iStorage,  sesskey, "ProxyLocalhost", 0, &cfg->even_proxy_localhost);
    gppi(iStorage,  sesskey, "ProxyMethod", -1, &cfg->proxy_type);
    if (cfg->proxy_type == -1) {
        int i;
        gppi(iStorage,  sesskey, "ProxyType", 0, &i);
        if (i == 0)
            cfg->proxy_type = PROXY_NONE;
        else if (i == 1)
            cfg->proxy_type = PROXY_HTTP;
        else if (i == 3)
            cfg->proxy_type = PROXY_TELNET;
        else if (i == 4)
            cfg->proxy_type = PROXY_CMD;
        else {
            gppi(iStorage,  sesskey, "ProxySOCKSVersion", 5, &i);
            if (i == 5)
                cfg->proxy_type = PROXY_SOCKS5;
            else
                cfg->proxy_type = PROXY_SOCKS4;
        }
    }
    gpps(iStorage,  sesskey, "ProxyHost", "proxy", cfg->proxy_host,
	 sizeof(cfg->proxy_host));
    gppi(iStorage,  sesskey, "ProxyPort", 80, &cfg->proxy_port);
    gpps(iStorage,  sesskey, "ProxyUsername", "", cfg->proxy_username,
	 sizeof(cfg->proxy_username));
    gpps(iStorage,  sesskey, "ProxyPassword", "", cfg->proxy_password,
	 sizeof(cfg->proxy_password));
    gpps(iStorage,  sesskey, "ProxyTelnetCommand", "connect %host %port\\n",
	 cfg->proxy_telnet_command, sizeof(cfg->proxy_telnet_command));
    gppmap(iStorage,  sesskey, "Environment", "", cfg->environmt, lenof(cfg->environmt));
    gpps(iStorage,  sesskey, "UserName", "", cfg->username, sizeof(cfg->username));
    gppi(iStorage,  sesskey, "UserNameFromEnvironment", 0, &cfg->username_from_env);
    gpps(iStorage,  sesskey, "LocalUserName", "", cfg->localusername,
	 sizeof(cfg->localusername));
    gppi(iStorage,  sesskey, "NoPTY", 0, &cfg->nopty);
    gppi(iStorage,  sesskey, "Compression", 0, &cfg->compression);
    gppi(iStorage,  sesskey, "TryAgent", 1, &cfg->tryagent);
    gppi(iStorage,  sesskey, "AgentFwd", 0, &cfg->agentfwd);
    gppi(iStorage,  sesskey, "ChangeUsername", 0, &cfg->change_username);
    gppi(iStorage,  sesskey, "GssapiFwd", 0, &cfg->gssapifwd);
    gprefs(iStorage,  sesskey, "Cipher", "\0",
	   ciphernames, CIPHER_MAX, cfg->ssh_cipherlist);
    {
	/* Backward-compatibility: we used to have an option to
	 * disable gex under the "bugs" panel after one report of
	 * a server which offered it then choked, but we never got
	 * a server version string or any other reports. */
	char *default_kexes;
	gppi(iStorage,  sesskey, "BugDHGEx2", 0, &i); i = 2-i;
	if (i == FORCE_ON)
	    default_kexes = "dh-group14-sha1,dh-group1-sha1,rsa,WARN,dh-gex-sha1";
	else
	    default_kexes = "dh-gex-sha1,dh-group14-sha1,dh-group1-sha1,rsa,WARN";
	gprefs(iStorage,  sesskey, "KEX", default_kexes,
	       kexnames, KEX_MAX, cfg->ssh_kexlist);
    }
    gppi(iStorage,  sesskey, "RekeyTime", 60, &cfg->ssh_rekey_time);
    gpps(iStorage,  sesskey, "RekeyBytes", "1G", cfg->ssh_rekey_data,
	 sizeof(cfg->ssh_rekey_data));
    gppi(iStorage,  sesskey, "SshProt", 2, &cfg->sshprot);
    gpps(iStorage,  sesskey, "LogHost", "", cfg->loghost, sizeof(cfg->loghost));
    gppi(iStorage,  sesskey, "SSH2DES", 0, &cfg->ssh2_des_cbc);
    gppi(iStorage,  sesskey, "SshNoAuth", 0, &cfg->ssh_no_userauth);
    gppi(iStorage,  sesskey, "SshBanner", 1, &cfg->ssh_show_banner);
    gppi(iStorage,  sesskey, "AuthTIS", 0, &cfg->try_tis_auth);
    gppi(iStorage,  sesskey, "AuthKI", 1, &cfg->try_ki_auth);
    gppi(iStorage,  sesskey, "AuthGSSAPI", 1, &cfg->try_gssapi_auth);
#ifndef NO_GSSAPI
    gprefs(iStorage,  sesskey, "GSSLibs", "\0",
	   gsslibkeywords, ngsslibs, cfg->ssh_gsslist);
    gppfile(iStorage,  sesskey, "GSSCustom", &cfg->ssh_gss_custom);
#endif
    gppi(iStorage,  sesskey, "SshNoShell", 0, &cfg->ssh_no_shell);
    gppfile(iStorage,  sesskey, "PublicKeyFile", &cfg->keyfile);
    gpps(iStorage,  sesskey, "RemoteCommand", "", cfg->remote_cmd,
	 sizeof(cfg->remote_cmd));
    gppi(iStorage,  sesskey, "RFCEnviron", 0, &cfg->rfc_environ);
    gppi(iStorage,  sesskey, "PassiveTelnet", 0, &cfg->passive_telnet);
    gppi(iStorage,  sesskey, "BackspaceIsDelete", 1, &cfg->bksp_is_delete);
    gppi(iStorage,  sesskey, "RXVTHomeEnd", 0, &cfg->rxvt_homeend);
    gppi(iStorage,  sesskey, "LinuxFunctionKeys", 0, &cfg->funky_type);
    gppi(iStorage,  sesskey, "NoApplicationKeys", 0, &cfg->no_applic_k);
    gppi(iStorage,  sesskey, "NoApplicationCursors", 0, &cfg->no_applic_c);
    gppi(iStorage,  sesskey, "NoMouseReporting", 0, &cfg->no_mouse_rep);
    gppi(iStorage,  sesskey, "NoRemoteResize", 0, &cfg->no_remote_resize);
    gppi(iStorage,  sesskey, "NoAltScreen", 0, &cfg->no_alt_screen);
    gppi(iStorage,  sesskey, "NoRemoteWinTitle", 0, &cfg->no_remote_wintitle);
    gppi(iStorage,  sesskey, "NoRemoteTabName", 1, &cfg->no_remote_tabname);
    {
	/* Backward compatibility */
	int no_remote_qtitle;
	gppi(iStorage,  sesskey, "NoRemoteQTitle", 1, &no_remote_qtitle);
	/* We deliberately interpret the old setting of "no response" as
	 * "empty string". This changes the behaviour, but hopefully for
	 * the better; the user can always recover the old behaviour. */
	gppi(iStorage,  sesskey, "RemoteQTitleAction",
	     no_remote_qtitle ? TITLE_EMPTY : TITLE_REAL,
	     &cfg->remote_qtitle_action);
    }
    gppi(iStorage,  sesskey, "NoDBackspace", 0, &cfg->no_dbackspace);
    gppi(iStorage,  sesskey, "NoRemoteCharset", 0, &cfg->no_remote_charset);
    gppi(iStorage,  sesskey, "ApplicationCursorKeys", 0, &cfg->app_cursor);
    gppi(iStorage,  sesskey, "ApplicationKeypad", 0, &cfg->app_keypad);
    gppi(iStorage,  sesskey, "NetHackKeypad", 0, &cfg->nethack_keypad);
    gppi(iStorage,  sesskey, "AltF4", 1, &cfg->alt_f4);
    gppi(iStorage,  sesskey, "AltSpace", 0, &cfg->alt_space);
    gppi(iStorage,  sesskey, "AltOnly", 0, &cfg->alt_only);
    gppi(iStorage,  sesskey, "ComposeKey", 0, &cfg->compose_key);
    gppi(iStorage,  sesskey, "CtrlAltKeys", 1, &cfg->ctrlaltkeys);
    gppi(iStorage,  sesskey, "TelnetKey", 0, &cfg->telnet_keyboard);
    gppi(iStorage,  sesskey, "TelnetRet", 1, &cfg->telnet_newline);
    gppi(iStorage,  sesskey, "LocalEcho", AUTO, &cfg->localecho);
    gppi(iStorage,  sesskey, "LocalEdit", AUTO, &cfg->localedit);
    gpps(iStorage,  sesskey, "Answerback", "PuTTY", cfg->answerback,
	 sizeof(cfg->answerback));
    gppi(iStorage,  sesskey, "AlwaysOnTop", 0, &cfg->alwaysontop);
    gppi(iStorage,  sesskey, "FullScreenOnAltEnter", 0, &cfg->fullscreenonaltenter);
    gppi(iStorage,  sesskey, "HideMousePtr", 0, &cfg->hide_mouseptr);
    gppi(iStorage,  sesskey, "SunkenEdge", 0, &cfg->sunken_edge);
    gppi(iStorage,  sesskey, "WindowBorder", 1, &cfg->window_border);
    gppi(iStorage,  sesskey, "CurType", 0, &cfg->cursor_type);
    gppi(iStorage,  sesskey, "BlinkCur", 0, &cfg->blink_cur);
    /* pedantic compiler tells me I can't use &cfg->beep as an int * :-) */
    gppi(iStorage,  sesskey, "Beep", 1, &cfg->beep);
    gppi(iStorage,  sesskey, "BeepInd", 0, &cfg->beep_ind);
    gppfile(iStorage,  sesskey, "BellWaveFile", &cfg->bell_wavefile);
    gppi(iStorage,  sesskey, "BellOverload", 1, &cfg->bellovl);
    gppi(iStorage,  sesskey, "BellOverloadN", 5, &cfg->bellovl_n);
    gppi(iStorage,  sesskey, "BellOverloadT", 2*TICKSPERSEC
#ifdef PUTTY_UNIX_H
				   *1000
#endif
				   , &i);
    cfg->bellovl_t = i
#ifdef PUTTY_UNIX_H
		    / 1000
#endif
	;
    gppi(iStorage,  sesskey, "BellOverloadS", 5*TICKSPERSEC
#ifdef PUTTY_UNIX_H
				   *1000
#endif
				   , &i);
    cfg->bellovl_s = i
#ifdef PUTTY_UNIX_H
		    / 1000
#endif
	;
    gppi(iStorage,  sesskey, "ScrollbackLines", 20000, &cfg->savelines);
    gppi(iStorage,  sesskey, "LinesAtAScroll", 3, &cfg->scrolllines);
    gppi(iStorage,  sesskey, "DECOriginMode", 0, &cfg->dec_om);
    gppi(iStorage,  sesskey, "AutoWrapMode", 1, &cfg->wrap_mode);
    gppi(iStorage,  sesskey, "LFImpliesCR", 0, &cfg->lfhascr);
    gppi(iStorage,  sesskey, "CRImpliesLF", 0, &cfg->crhaslf);
    gppi(iStorage,  sesskey, "DisableArabicShaping", 0, &cfg->arabicshaping);
    gppi(iStorage,  sesskey, "DisableBidi", 0, &cfg->bidi);
    gppi(iStorage,  sesskey, "WinNameAlways", 1, &cfg->win_name_always);
    gpps(iStorage,  sesskey, "WinTitle", "", cfg->wintitle, sizeof(cfg->wintitle));
    gppi(iStorage,  sesskey, "TermWidth", 80, &cfg->width);
    gppi(iStorage,  sesskey, "TermHeight", 24, &cfg->height);
    gppfont(iStorage,  sesskey, "Font", &cfg->font);
    gppi(iStorage,  sesskey, "FontQuality", FQ_DEFAULT, &cfg->font_quality);
    gppi(iStorage,  sesskey, "FontVTMode", VT_UNICODE, (int *) &cfg->vtmode);
    gppi(iStorage,  sesskey, "UseSystemColours", 0, &cfg->system_colour);
    gppi(iStorage,  sesskey, "TryPalette", 0, &cfg->try_palette);
    gppi(iStorage,  sesskey, "ANSIColour", 1, &cfg->ansi_colour);
    gppi(iStorage,  sesskey, "Xterm256Colour", 1, &cfg->xterm_256_colour);
    gppi(iStorage,  sesskey, "BoldAsColour", 1, &cfg->bold_colour);

    for (i = 0; i < 22; i++) {
	static const char *const defaults[] = {
	    "187,187,187", "255,255,255", "0,0,0", "85,85,85", "0,0,0",
	    "0,255,0", "0,0,0", "85,85,85", "187,0,0", "255,85,85",
	    "0,187,0", "85,255,85", "187,187,0", "255,255,85", "0,0,187",
	    "85,85,255", "187,0,187", "255,85,255", "0,187,187",
	    "85,255,255", "187,187,187", "255,255,255"
	};
	char buf[20], buf2[30];
	int c0, c1, c2;
	sprintf(buf, "Colour%d", i);
	gpps(iStorage,  sesskey, buf, defaults[i], buf2, sizeof(buf2));
	if (sscanf(buf2, "%d,%d,%d", &c0, &c1, &c2) == 3) {
	    cfg->colours[i][0] = c0;
	    cfg->colours[i][1] = c1;
	    cfg->colours[i][2] = c2;
	}
    }
    gppi(iStorage,  sesskey, "RawCNP", 0, &cfg->rawcnp);
    gppi(iStorage,  sesskey, "PasteRTF", 0, &cfg->rtf_paste);
    gppi(iStorage,  sesskey, "MouseIsXterm", 0, &cfg->mouse_is_xterm);
    gppi(iStorage,  sesskey, "RectSelect", 0, &cfg->rect_select);
    gppi(iStorage,  sesskey, "MouseOverride", 1, &cfg->mouse_override);
    for (i = 0; i < 256; i += 32) {
	static const char *const defaults[] = {
	    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
	    "0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2"
	};
	char buf[20], buf2[256], *p;
	int j;
	sprintf(buf, "Wordness%d", i);
	gpps(iStorage,  sesskey, buf, defaults[i / 32], buf2, sizeof(buf2));
	p = buf2;
	for (j = i; j < i + 32; j++) {
	    char *q = p;
	    while (*p && *p != ',')
		p++;
	    if (*p == ',')
		*p++ = '\0';
	    cfg->wordness[j] = atoi(q);
	}
    }
    /*
     * The empty default for LineCodePage will be converted later
     * into a plausible default for the locale.
     */
    gpps(iStorage,  sesskey, "LineCodePage", "", cfg->line_codepage,
	 sizeof(cfg->line_codepage));
    gppi(iStorage,  sesskey, "CJKAmbigWide", 0, &cfg->cjk_ambig_wide);
    gppi(iStorage,  sesskey, "UTF8Override", 1, &cfg->utf8_override);
    gpps(iStorage,  sesskey, "Printer", "", cfg->printer, sizeof(cfg->printer));
    gppi(iStorage,  sesskey, "CapsLockCyr", 0, &cfg->xlat_capslockcyr);
    gppi(iStorage,  sesskey, "ScrollBar", 1, &cfg->scrollbar);
    gppi(iStorage,  sesskey, "ScrollBarFullScreen", 0, &cfg->scrollbar_in_fullscreen);
    gppi(iStorage,  sesskey, "ScrollOnKey", 0, &cfg->scroll_on_key);
    gppi(iStorage,  sesskey, "ScrollOnDisp", 1, &cfg->scroll_on_disp);
    gppi(iStorage,  sesskey, "EraseToScrollback", 1, &cfg->erase_to_scrollback);
    gppi(iStorage,  sesskey, "LockSize", 0, &cfg->resize_action);
    gppi(iStorage,  sesskey, "BCE", 1, &cfg->bce);
    gppi(iStorage,  sesskey, "BlinkText", 0, &cfg->blinktext);
    gppi(iStorage,  sesskey, "X11Forward", 0, &cfg->x11_forward);
    gpps(iStorage,  sesskey, "X11Display", "", cfg->x11_display,
	 sizeof(cfg->x11_display));
    gppi(iStorage,  sesskey, "X11AuthType", X11_MIT, &cfg->x11_auth);
    gppfile(iStorage,  sesskey, "X11AuthFile", &cfg->xauthfile);

    gppi(iStorage,  sesskey, "LocalPortAcceptAll", 0, &cfg->lport_acceptall);
    gppi(iStorage,  sesskey, "RemotePortAcceptAll", 0, &cfg->rport_acceptall);
    gppmap(iStorage,  sesskey, "PortForwardings", "", cfg->portfwd, lenof(cfg->portfwd));
    gppi(iStorage,  sesskey, "BugIgnore1", 0, &i); cfg->sshbug_ignore1 = 2-i;
    gppi(iStorage,  sesskey, "BugPlainPW1", 0, &i); cfg->sshbug_plainpw1 = 2-i;
    gppi(iStorage,  sesskey, "BugRSA1", 0, &i); cfg->sshbug_rsa1 = 2-i;
    gppi(iStorage,  sesskey, "BugIgnore2", 0, &i); cfg->sshbug_ignore2 = 2-i;
    {
	int i;
	gppi(iStorage,  sesskey, "BugHMAC2", 0, &i); cfg->sshbug_hmac2 = 2-i;
	if (cfg->sshbug_hmac2 == AUTO) {
	    gppi(iStorage,  sesskey, "BuggyMAC", 0, &i);
	    if (i == 1)
		cfg->sshbug_hmac2 = FORCE_ON;
	}
    }
    gppi(iStorage,  sesskey, "BugDeriveKey2", 0, &i); cfg->sshbug_derivekey2 = 2-i;
    gppi(iStorage,  sesskey, "BugRSAPad2", 0, &i); cfg->sshbug_rsapad2 = 2-i;
    gppi(iStorage,  sesskey, "BugPKSessID2", 0, &i); cfg->sshbug_pksessid2 = 2-i;
    gppi(iStorage,  sesskey, "BugRekey2", 0, &i); cfg->sshbug_rekey2 = 2-i;
    gppi(iStorage,  sesskey, "BugMaxPkt2", 0, &i); cfg->sshbug_maxpkt2 = 2-i;
    cfg->ssh_simple = FALSE;
    gppi(iStorage,  sesskey, "StampUtmp", 1, &cfg->stamp_utmp);
    gppi(iStorage,  sesskey, "LoginShell", 1, &cfg->login_shell);
    gppi(iStorage,  sesskey, "ScrollbarOnLeft", 0, &cfg->scrollbar_on_left);
    gppi(iStorage,  sesskey, "ShadowBold", 0, &cfg->shadowbold);
    gppfont(iStorage,  sesskey, "BoldFont", &cfg->boldfont);
    gppfont(iStorage,  sesskey, "WideFont", &cfg->widefont);
    gppfont(iStorage,  sesskey, "WideBoldFont", &cfg->wideboldfont);
    gppi(iStorage,  sesskey, "ShadowBoldOffset", 1, &cfg->shadowboldoffset);
    gpps(iStorage,  sesskey, "SerialLine", "", cfg->serline, sizeof(cfg->serline));
    gppi(iStorage,  sesskey, "SerialSpeed", 9600, &cfg->serspeed);
    gppi(iStorage,  sesskey, "SerialDataBits", 8, &cfg->serdatabits);
    gppi(iStorage,  sesskey, "SerialStopHalfbits", 2, &cfg->serstopbits);
    gppi(iStorage,  sesskey, "SerialParity", SER_PAR_NONE, &cfg->serparity);
    gppi(iStorage,  sesskey, "SerialFlowControl", SER_FLOW_XONXOFF, &cfg->serflow);
    gpps(iStorage,  sesskey, "WindowClass", "", cfg->winclass, sizeof(cfg->winclass));

    for (i = 0; i < AUTOCMD_COUNT; i++){
        char buf[20];
	    sprintf(buf, "AutocmdExpect%d", i);
        gpps(iStorage,  sesskey, buf, i==0?"ogin: "
                           :i==1?"assword: "
                           :"", 
                           cfg->expect[i], sizeof(cfg->expect[i]));
                           
        sprintf(buf, "Autocmd%d", i);
        gpps(iStorage,  sesskey, buf, "", cfg->autocmd[i], sizeof(cfg->autocmd[i]));
        sprintf(buf, "AutocmdHide%d", i);
        gppi(iStorage,  sesskey, buf, i==1?1:0, &cfg->autocmd_hide[i]);
        sprintf(buf, "AutocmdEnable%d", i);
        gppi(iStorage,  sesskey, buf, 0, &cfg->autocmd_enable[i]);
        
    }
}

void move_settings(const char* fromsession, const char* tosession)
{
	copy_settings(fromsession, tosession);
	gStorage->del_settings(fromsession);
}

void copy_settings(const char* fromsession, const char* tosession)
{
    Config cfg;
	
	load_settings(fromsession, &cfg);
	char *errmsg = save_settings(tosession, &cfg);
	if (errmsg){
		return;
	}
}


void do_defaults(char *session, Config * cfg)
{
    load_settings(session, cfg);
}

static int sessioncmp(const void *av, const void *bv)
{
    const char *a = *(const char *const *) av;
    const char *b = *(const char *const *) bv;

    /*
     * Alphabetical order, except that DEFAULT_SESSION_NAME is a
     * special case and comes first.
     */
    if (!strcmp(a, DEFAULT_SESSION_NAME))
	return -1;		       /* a comes first */
    if (!strcmp(b, DEFAULT_SESSION_NAME))
	return +1;		       /* b comes first */
    /*
     * FIXME: perhaps we should ignore the first & in determining
     * sort order.
     */
    return strcmp(a, b);	       /* otherwise, compare normally */
}

void get_sesslist(struct sesslist *list, int allocate)
{
    char otherbuf[2048];
    int buflen, bufsize, i;
    char *p, *ret;
    void *handle;

    if (allocate) {

	buflen = bufsize = 0;
	list->buffer = NULL;
	if ((handle = gStorage->enum_settings_start()) != NULL) {
	    do {
		ret = gStorage->enum_settings_next(handle, otherbuf, sizeof(otherbuf));
		if (ret) {
		    int len = strlen(otherbuf) + 1;
		    if (bufsize < buflen + len) {
			bufsize = buflen + len + 2048;
			list->buffer = sresize(list->buffer, bufsize, char);
		    }
		    strcpy(list->buffer + buflen, otherbuf);
		    buflen += strlen(list->buffer + buflen) + 1;
		}
	    } while (ret);
	    gStorage->enum_settings_finish(handle);
	}
	list->buffer = sresize(list->buffer, buflen + 1, char);
	list->buffer[buflen] = '\0';

	/*
	 * Now set up the list of sessions. Note that "Default
	 * Settings" must always be claimed to exist, even if it
	 * doesn't really.
	 */

	p = list->buffer;
	list->nsessions = 1;	       /* DEFAULT_SESSION_NAME counts as one */
	while (*p) {
	    if (strcmp(p, DEFAULT_SESSION_NAME))
		list->nsessions++;
	    while (*p)
		p++;
	    p++;
	}

	list->sessions = snewn(list->nsessions + 1, char *);
	list->sessions[0] = DEFAULT_SESSION_NAME;
	p = list->buffer;
	i = 1;
	while (*p) {
	    if (strcmp(p, DEFAULT_SESSION_NAME))
		list->sessions[i++] = p;
	    while (*p)
		p++;
	    p++;
	}

	qsort(list->sessions, i, sizeof(char *), sessioncmp);
    } else {
	sfree(list->buffer);
	sfree(list->sessions);
	list->buffer = NULL;
	list->sessions = NULL;
    }
}

int lower_bound_in_sesslist(struct sesslist *list, const char* session)
{
	int first = 0;
	int last = list->nsessions;
	int count = last;
	int it, step;

	while (count > 0)
	{
		it = first; step = count/2; it += step;
		if (sessioncmp(&list->sessions[it], &session) == -1) 
		{ first=++it; count-=step+1;  }
		else count=step;
	}
	return first;
}
