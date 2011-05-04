/*
 * scp.c  -  Scp (Secure Copy) client for PuTTY.
 * Joris van Rantwijk, Simon Tatham
 *
 * This is mainly based on ssh-1.2.26/scp.c by Timo Rinne & Tatu Ylonen.
 * They, in turn, used stuff from BSD rcp.
 * 
 * (SGT, 2001-09-10: Joris van Rantwijk assures me that although
 * this file as originally submitted was inspired by, and
 * _structurally_ based on, ssh-1.2.26's scp.c, there wasn't any
 * actual code duplicated, so the above comment shouldn't give rise
 * to licensing issues.)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>

#define PUTTY_DO_GLOBALS
#include "putty.h"
#include "psftp.h"
#include "ssh.h"
#include "sftp.h"
#include "storage.h"
#include "int64.h"

static int list = 0;
static int recursive = 0;
static int preserve = 0;
static int targetshouldbedirectory = 0;
static int statistics = 1;
static int prev_stats_len = 0;
static int scp_unsafe_mode = 0;
static int errs = 0;
static int try_scp = 1;
static int try_sftp = 1;
static int main_cmd_is_sftp = 0;
static int fallback_cmd_is_sftp = 0;
static int using_sftp = 0;

static void source(sftp_handle* sftp, char *src);
static void rsource(sftp_handle* sftp, char *src);
static void sink(sftp_handle* sftp, char *targ, char *src);

const char *const appname = "PSCP";

/*
 * The maximum amount of queued data we accept before we stop and
 * wait for the server to process some.
 */
#define MAX_SCP_BUFSIZE 16384

void ldisc_send(void *handle, char *buf, int len, int interactive)
{
    /*
     * This is only here because of the calls to ldisc_send(NULL,
     * 0) in ssh.c. Nothing in PSCP actually needs to use the ldisc
     * as an ldisc. So if we get called with any real data, I want
     * to know about it.
     */
    assert(len == 0);
}

static void tell_char(FILE * stream, char c)
{
    fputc(c, stream);
}

static void tell_str(FILE * stream, char *str)
{
    unsigned int i;

    for (i = 0; i < strlen(str); ++i)
	tell_char(stream, str[i]);
}

static void tell_user(FILE * stream, char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    va_end(ap);
    str2 = dupcat(str, "\n", NULL);
    sfree(str);
    tell_str(stream, str2);
    sfree(str2);
}

/*
 *  Print an error message and perform a fatal exit.
 */
void fatalbox(char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    str2 = dupcat("Fatal: ", str, "\n", NULL);
    sfree(str);
    va_end(ap);
    tell_str(stderr, str2);
    sfree(str2);
    errs++;

    cleanup_exit(1);
}
void modalfatalbox(char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    str2 = dupcat("Fatal: ", str, "\n", NULL);
    sfree(str);
    va_end(ap);
    tell_str(stderr, str2);
    sfree(str2);
    errs++;

    cleanup_exit(1);
}
void connection_fatal(void *frontend, char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    str2 = dupcat("Fatal: ", str, "\n", NULL);
    sfree(str);
    va_end(ap);
    tell_str(stderr, str2);
    sfree(str2);
    errs++;

    cleanup_exit(1);
}

/*
 * In pscp, all agent requests should be synchronous, so this is a
 * never-called stub.
 */
void agent_schedule_callback(void (*callback)(void *, void *, int),
			     void *callback_ctx, void *data, int len)
{
    assert(!"We shouldn't be here");
}

/*
 * Receive a block of data from the SSH link. Block until all data
 * is available.
 *
 * To do this, we repeatedly call the SSH protocol module, with our
 * own trap in from_backend() to catch the data that comes sftp->back. We
 * do this until we have enough data.
 */
int from_backend(void *frontend, int is_stderr, const char *data, int datalen)
{
    unsigned char *p = (unsigned char *) data;
    unsigned len = (unsigned) datalen;
    assert(frontend != NULL);
    sftp_handle* sftp = frontend;
    
    /*
     * stderr data is just spouted to local stderr and otherwise
     * ignored.
     */
    if (is_stderr) {
	if (len > 0)
	    if (fwrite(data, 1, len, stderr) < len)
		/* oh well */;
	return 0;
    }

    if ((sftp->outlen > 0) && (len > 0)) {
	unsigned used = sftp->outlen;
	if (used > len)
	    used = len;
	memcpy(sftp->outptr, p, used);
	sftp->outptr += used;
	sftp->outlen -= used;
	p += used;
	len -= used;
    }

    if (len > 0) {
	if (sftp->pendsize < sftp->pendlen + len) {
	    sftp->pendsize = sftp->pendlen + len + 4096;
	    sftp->pending = sresize(sftp->pending, sftp->pendsize, unsigned char);
	}
	memcpy(sftp->pending + sftp->pendlen, p, len);
	sftp->pendlen += len;
    }

    return 0;
}
int from_backend_untrusted(void *frontend_handle, const char *data, int len)
{
    /*
     * No "untrusted" output should get here (the way the code is
     * currently, it's all diverted by FLAG_STDERR).
     */
    assert(!"Unexpected call to from_backend_untrusted()");
    return 0; /* not reached */
}
static int ssh_scp_recv(sftp_handle* sftp, unsigned char *buf, int len)
{
    sftp->outptr = buf;
    sftp->outlen = len;

    /*
     * See if the sftp->pending-input block contains some of what we
     * need.
     */
    if (sftp->pendlen > 0) {
	unsigned pendused = sftp->pendlen;
	if (pendused > sftp->outlen)
	    pendused = sftp->outlen;
	memcpy(sftp->outptr, sftp->pending, pendused);
	memmove(sftp->pending, sftp->pending + pendused, sftp->pendlen - pendused);
	sftp->outptr += pendused;
	sftp->outlen -= pendused;
	sftp->pendlen -= pendused;
	if (sftp->pendlen == 0) {
	    sftp->pendsize = 0;
	    sfree(sftp->pending);
	    sftp->pending = NULL;
	}
	if (sftp->outlen == 0)
	    return len;
    }

    while (sftp->outlen > 0) {
	if (sftp->back->exitcode(sftp->backhandle) >= 0 || ssh_sftp_loop_iteration() < 0)
	    return 0;		       /* doom */
    }

    return len;
}

/*
 * Loop through the ssh connection and authentication process.
 */
static void ssh_scp_init(sftp_handle* sftp)
{
    while (!sftp->back->sendok(sftp->backhandle)) {
        if (sftp->back->exitcode(sftp->backhandle) >= 0) {
            errs++;
            return;
        }
	if (ssh_sftp_loop_iteration() < 0) {
            errs++;
	    return;		       /* doom */
        }
    }

    /* Work out which backend we ended up using. */
    if (!ssh_fallback_cmd(sftp->backhandle))
	using_sftp = main_cmd_is_sftp;
    else
	using_sftp = fallback_cmd_is_sftp;

    if (sftp->verbose) {
	if (using_sftp)
	    tell_user(stderr, "Using SFTP");
	else
	    tell_user(stderr, "Using SCP1");
    }
}

/*
 *  Print an error message and exit after closing the SSH link.
 */
static void bump(sftp_handle* sftp, char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    va_end(ap);
    str2 = dupcat(str, "\n", NULL);
    sfree(str);
    tell_str(stderr, str2);
    sfree(str2);
    errs++;

    if (sftp->back != NULL && sftp->back->connected(sftp->backhandle)) {
	char ch;
	sftp->back->special(sftp->backhandle, TS_EOF);
	ssh_scp_recv(sftp, (unsigned char *) &ch, 1);
    }

    cleanup_exit(1);
}

/*
 *  Open an SSH connection to user@host and execute cmd.
 */
static void do_cmd(sftp_handle* sftp, char *host, char *user, char *cmd)
{
    const char *err;
    char *realhost;
    void *logctx;

    if (host == NULL || host[0] == '\0')
	bump(sftp, "Empty host name");

    /*
     * Remove fiddly bits of address: remove a colon suffix, and
     * the square brackets around an IPv6 literal address.
     */
    if (host[0] == '[') {
	host++;
	host[strcspn(host, "]")] = '\0';
    } else {
	host[strcspn(host, ":")] = '\0';
    }

    /*
     * If we haven't loaded session details already (e.g., from -load),
     * try looking for a session called "host".
     */
    if (!loaded_session) {
	/* Try to load settings for `host' into a temporary config */
	Config cfg2;
	cfg2.host[0] = '\0';
	do_defaults(host, &cfg2);
	if (cfg2.host[0] != '\0') {
	    /* Settings present and include hostname */
	    /* Re-load data into the real config. */
	    do_defaults(host, &sftp->cfg);
	} else {
	    /* Session doesn't exist or mention a hostname. */
	    /* Use `host' as a bare hostname. */
	    strncpy(sftp->cfg.host, host, sizeof(sftp->cfg.host) - 1);
	    sftp->cfg.host[sizeof(sftp->cfg.host) - 1] = '\0';
	}
    } else {
	/* Patch in hostname `host' to session details. */
	strncpy(sftp->cfg.host, host, sizeof(sftp->cfg.host) - 1);
	sftp->cfg.host[sizeof(sftp->cfg.host) - 1] = '\0';
    }

    /*
     * Force use of SSH. (If they got the protocol wrong we assume the
     * port is useless too.)
     */
    if (sftp->cfg.protocol != PROT_SSH) {
        sftp->cfg.protocol = PROT_SSH;
        sftp->cfg.port = 22;
    }

    /*
     * Enact command-line overrides.
     */
    cmdline_run_saved(&sftp->cfg);

    /*
     * Trim leading whitespace off the hostname if it's there.
     */
    {
	int space = strspn(sftp->cfg.host, " \t");
	memmove(sftp->cfg.host, sftp->cfg.host+space, 1+strlen(sftp->cfg.host)-space);
    }

    /* See if host is of the form user@host */
    if (sftp->cfg.host[0] != '\0') {
	char *atsign = strrchr(sftp->cfg.host, '@');
	/* Make sure we're not overflowing the user field */
	if (atsign) {
	    if (atsign - sftp->cfg.host < sizeof sftp->cfg.username) {
		strncpy(sftp->cfg.username, sftp->cfg.host, atsign - sftp->cfg.host);
		sftp->cfg.username[atsign - sftp->cfg.host] = '\0';
	    }
	    memmove(sftp->cfg.host, atsign + 1, 1 + strlen(atsign + 1));
	}
    }

    /*
     * Remove any remaining whitespace from the hostname.
     */
    {
	int p1 = 0, p2 = 0;
	while (sftp->cfg.host[p2] != '\0') {
	    if (sftp->cfg.host[p2] != ' ' && sftp->cfg.host[p2] != '\t') {
		sftp->cfg.host[p1] = sftp->cfg.host[p2];
		p1++;
	    }
	    p2++;
	}
	sftp->cfg.host[p1] = '\0';
    }

    /* Set username */
    if (user != NULL && user[0] != '\0') {
	strncpy(sftp->cfg.username, user, sizeof(sftp->cfg.username) - 1);
	sftp->cfg.username[sizeof(sftp->cfg.username) - 1] = '\0';
    } else if (sftp->cfg.username[0] == '\0') {
	user = get_username();
	if (!user)
	    bump(sftp, "Empty user name");
	else {
	    if (sftp->verbose)
		tell_user(stderr, "Guessing user name: %s", user);
	    strncpy(sftp->cfg.username, user, sizeof(sftp->cfg.username) - 1);
	    sftp->cfg.username[sizeof(sftp->cfg.username) - 1] = '\0';
	    sfree(user);
	}
    }

    /*
     * Disable scary things which shouldn't be enabled for simple
     * things like SCP and SFTP: agent forwarding, port forwarding,
     * X forwarding.
     */
    sftp->cfg.x11_forward = 0;
    sftp->cfg.agentfwd = 0;
    sftp->cfg.portfwd[0] = sftp->cfg.portfwd[1] = '\0';
    sftp->cfg.ssh_simple = TRUE;

    /*
     * Set up main and possibly fallback command depending on
     * options specified by user.
     * Attempt to start the SFTP subsystem as a first choice,
     * falling sftp->back to the provided scp command if that fails.
     */
    sftp->cfg.remote_cmd_ptr2 = NULL;
    if (try_sftp) {
	/* First choice is SFTP subsystem. */
	main_cmd_is_sftp = 1;
	strcpy(sftp->cfg.remote_cmd, "sftp");
	sftp->cfg.ssh_subsys = TRUE;
	if (try_scp) {
	    /* Fallback is to use the provided scp command. */
	    fallback_cmd_is_sftp = 0;
	    sftp->cfg.remote_cmd_ptr2 = cmd;
	    sftp->cfg.ssh_subsys2 = FALSE;
	} else {
	    /* Since we're not going to try SCP, we may as well try
	     * harder to find an SFTP server, since in the current
	     * implementation we have a spare slot. */
	    fallback_cmd_is_sftp = 1;
	    /* see psftp.c for full explanation of this kludge */
	    sftp->cfg.remote_cmd_ptr2 = 
		"test -x /usr/lib/sftp-server && exec /usr/lib/sftp-server\n"
		"test -x /usr/local/lib/sftp-server && exec /usr/local/lib/sftp-server\n"
		"exec sftp-server";
	    sftp->cfg.ssh_subsys2 = FALSE;
	}
    } else {
	/* Don't try SFTP at all; just try the scp command. */
	main_cmd_is_sftp = 0;
	sftp->cfg.remote_cmd_ptr = cmd;
	sftp->cfg.ssh_subsys = FALSE;
    }
    sftp->cfg.nopty = TRUE;

    sftp->back = &ssh_backend;

    err = sftp->back->init(sftp, &sftp->backhandle, &sftp->cfg, sftp->cfg.host, sftp->cfg.port, &realhost, 
		     0, sftp->cfg.tcp_keepalives);
    if (err != NULL)
	bump(sftp, "ssh_init: %s", err);
    logctx = log_init(sftp, &sftp->cfg);
    sftp->back->provide_logctx(sftp->backhandle, logctx);
    console_provide_logctx(logctx);
    ssh_scp_init(sftp);
    if (sftp->verbose && realhost != NULL && errs == 0)
	tell_user(stderr, "Connected to %s\n", realhost);
    sfree(realhost);
}

/*
 *  Update statistic information about current file.
 */
static void print_stats(char *name, uint64 size, uint64 done,
			time_t start, time_t now)
{
    float ratebs;
    unsigned long eta;
    char *etastr;
    int pct;
    int len;
    int elap;
    double donedbl;
    double sizedbl;

    elap = (unsigned long) difftime(now, start);

    if (now > start)
	ratebs = (float) (uint64_to_double(done) / elap);
    else
	ratebs = (float) uint64_to_double(done);

    if (ratebs < 1.0)
	eta = (unsigned long) (uint64_to_double(uint64_subtract(size, done)));
    else {
        eta = (unsigned long)
	    ((uint64_to_double(uint64_subtract(size, done)) / ratebs));
    }

    etastr = dupprintf("%02ld:%02ld:%02ld",
		       eta / 3600, (eta % 3600) / 60, eta % 60);

    donedbl = uint64_to_double(done);
    sizedbl = uint64_to_double(size);
    pct = (int) (100 * (donedbl * 1.0 / sizedbl));

    {
	char donekb[40];
	/* divide by 1024 to provide kB */
	uint64_decimal(uint64_shift_right(done, 10), donekb);
	len = printf("\r%-25.25s | %s kB | %5.1f kB/s | ETA: %8s | %3d%%",
		     name,
		     donekb, ratebs / 1024.0, etastr, pct);
	if (len < prev_stats_len)
	    printf("%*s", prev_stats_len - len, "");
	prev_stats_len = len;

	if (uint64_compare(done, size) == 0)
	    printf("\n");

	fflush(stdout);
    }

    free(etastr);
}

/*
 *  Find a colon in str and return a pointer to the colon.
 *  This is used to separate hostname from filename.
 */
static char *colon(char *str)
{
    /* We ignore a leading colon, since the hostname cannot be
       empty. We also ignore a colon as second character because
       of filenames like f:myfile.txt. */
    if (str[0] == '\0' || str[0] == ':' ||
        (str[0] != '[' && str[1] == ':'))
	return (NULL);
    while (*str != '\0' && *str != ':' && *str != '/' && *str != '\\') {
	if (*str == '[') {
	    /* Skip over IPv6 literal addresses
	     * (eg: 'jeroen@[2001:db8::1]:myfile.txt') */
	    char *ipv6_end = strchr(str, ']');
	    if (ipv6_end) {
		str = ipv6_end;
	    }
	}
	str++;
    }
    if (*str == ':')
	return (str);
    else
	return (NULL);
}

/*
 * Return a pointer to the portion of str that comes after the last
 * slash (or backslash or colon, if `local' is TRUE).
 */
static char *stripslashes(char *str, int local)
{
    char *p;

    if (local) {
        p = strchr(str, ':');
        if (p) str = p+1;
    }

    p = strrchr(str, '/');
    if (p) str = p+1;

    if (local) {
	p = strrchr(str, '\\');
	if (p) str = p+1;
    }

    return str;
}

/*
 * Determine whether a string is entirely composed of dots.
 */
static int is_dots(char *str)
{
    return str[strspn(str, ".")] == '\0';
}

/*
 *  Wait for a response from the other side.
 *  Return 0 if ok, -1 if error.
 */
static int response(sftp_handle* sftp)
{
    char ch, resp, rbuf[2048];
    int p;

    if (ssh_scp_recv(sftp, (unsigned char *) &resp, 1) <= 0)
	bump(sftp, "Lost connection");

    p = 0;
    switch (resp) {
      case 0:			       /* ok */
	return (0);
      default:
	rbuf[p++] = resp;
	/* fallthrough */
      case 1:			       /* error */
      case 2:			       /* fatal error */
	do {
	    if (ssh_scp_recv(sftp, (unsigned char *) &ch, 1) <= 0)
		bump(sftp, "Protocol error: Lost connection");
	    rbuf[p++] = ch;
	} while (p < sizeof(rbuf) && ch != '\n');
	rbuf[p - 1] = '\0';
	if (resp == 1)
	    tell_user(stderr, "%s\n", rbuf);
	else
	    bump(sftp, "%s", rbuf);
	errs++;
	return (-1);
    }
}

int sftp_recvdata(sftp_handle* sftp, char *buf, int len)
{
    return ssh_scp_recv(sftp, (unsigned char *) buf, len);
}
int sftp_senddata(sftp_handle* sftp, char *buf, int len)
{
    sftp->back->send(sftp->backhandle, buf, len);
    return 1;
}

/* ----------------------------------------------------------------------
 * sftp-based replacement for the hacky `pscp -ls'.
 */
static int sftp_ls_compare(const void *av, const void *bv)
{
    const struct fxp_name *a = (const struct fxp_name *) av;
    const struct fxp_name *b = (const struct fxp_name *) bv;
    return strcmp(a->filename, b->filename);
}
void scp_sftp_listdir(sftp_handle* sftp, char *dirname)
{
    struct fxp_handle *dirh;
    struct fxp_names *names;
    struct fxp_name *ournames;
    struct sftp_packet *pktin;
    struct sftp_request *req, *rreq;
    int nnames, namesize;
    int i;

    if (!fxp_init(sftp)) {
	tell_user(stderr, "unable to initialise SFTP: %s", fxp_error(sftp));
	errs++;
	return;
    }

    printf("Listing directory %s\n", dirname);

    sftp_register(sftp, req = fxp_opendir_send(sftp, dirname));
    rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
    assert(rreq == req);
    dirh = fxp_opendir_recv(sftp, pktin, rreq);

    if (dirh == NULL) {
	printf("Unable to open %s: %s\n", dirname, fxp_error(sftp));
    } else {
	nnames = namesize = 0;
	ournames = NULL;

	while (1) {

	    sftp_register(sftp, req = fxp_readdir_send(sftp, dirh));
	    rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	    assert(rreq == req);
	    names = fxp_readdir_recv(sftp, pktin, rreq);

	    if (names == NULL) {
		if (fxp_error_type(sftp) == SSH_FX_EOF)
		    break;
		printf("Reading directory %s: %s\n", dirname, fxp_error(sftp));
		break;
	    }
	    if (names->nnames == 0) {
		fxp_free_names(sftp, names);
		break;
	    }

	    if (nnames + names->nnames >= namesize) {
		namesize += names->nnames + 128;
		ournames = sresize(ournames, namesize, struct fxp_name);
	    }

	    for (i = 0; i < names->nnames; i++)
		ournames[nnames++] = names->names[i];
	    names->nnames = 0;	       /* prevent free_names */
	    fxp_free_names(sftp, names);
	}
	sftp_register(sftp, req = fxp_close_send(sftp, dirh));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	fxp_close_recv(sftp, pktin, rreq);

	/*
	 * Now we have our filenames. Sort them by actual file
	 * name, and then output the longname parts.
	 */
	qsort(ournames, nnames, sizeof(*ournames), sftp_ls_compare);

	/*
	 * And print them.
	 */
	for (i = 0; i < nnames; i++)
	    printf("%s\n", ournames[i].longname);
    }
}

/* ----------------------------------------------------------------------
 * Helper routines that contain the actual SCP protocol elements,
 * implemented both as SCP1 and SFTP.
 */

static struct scp_sftp_dirstack {
    struct scp_sftp_dirstack *next;
    struct fxp_name *names;
    int namepos, namelen;
    char *dirpath;
    char *wildcard;
    int matched_something;	       /* wildcard match set was non-empty */
} *scp_sftp_dirstack_head;
static char *scp_sftp_remotepath, *scp_sftp_currentname;
static char *scp_sftp_wildcard;
static int scp_sftp_targetisdir, scp_sftp_donethistarget;
static int scp_sftp_preserve, scp_sftp_recursive;
static unsigned long scp_sftp_mtime, scp_sftp_atime;
static int scp_has_times;
static struct fxp_handle *scp_sftp_filehandle;
static struct fxp_xfer *scp_sftp_xfer;
static uint64 scp_sftp_fileoffset;

int scp_source_setup(sftp_handle* sftp, char *target, int shouldbedir)
{
    if (using_sftp) {
	/*
	 * Find out whether the target filespec is in fact a
	 * directory.
	 */
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;
	struct fxp_attrs attrs;
	int ret;

	if (!fxp_init(sftp)) {
	    tell_user(stderr, "unable to initialise SFTP: %s", fxp_error(sftp));
	    errs++;
	    return 1;
	}

	sftp_register(sftp, req = fxp_stat_send(sftp, target));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	ret = fxp_stat_recv(sftp, pktin, rreq, &attrs);

	if (!ret || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS))
	    scp_sftp_targetisdir = 0;
	else
	    scp_sftp_targetisdir = (attrs.permissions & 0040000) != 0;

	if (shouldbedir && !scp_sftp_targetisdir) {
	    bump(sftp, "pscp: remote filespec %s: not a directory\n", target);
	}

	scp_sftp_remotepath = dupstr(target);

	scp_has_times = 0;
    } else {
	(void) response(sftp);
    }
    return 0;
}

int scp_send_errmsg(sftp_handle* sftp, char *str)
{
    if (using_sftp) {
	/* do nothing; we never need to send our errors to the server */
    } else {
	sftp->back->send(sftp->backhandle, "\001", 1);/* scp protocol error prefix */
	sftp->back->send(sftp->backhandle, str, strlen(str));
    }
    return 0;			       /* can't fail */
}

int scp_send_filetimes(sftp_handle* sftp, unsigned long mtime, unsigned long atime)
{
    if (using_sftp) {
	scp_sftp_mtime = mtime;
	scp_sftp_atime = atime;
	scp_has_times = 1;
	return 0;
    } else {
	char buf[80];
	sprintf(buf, "T%lu 0 %lu 0\n", mtime, atime);
	sftp->back->send(sftp->backhandle, buf, strlen(buf));
	return response(sftp);
    }
}

int scp_send_filename(sftp_handle* sftp, char *name, uint64 size, int modes)
{
    if (using_sftp) {
	char *fullname;
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;

	if (scp_sftp_targetisdir) {
	    fullname = dupcat(scp_sftp_remotepath, "/", name, NULL);
	} else {
	    fullname = dupstr(scp_sftp_remotepath);
	}

	sftp_register(sftp, req = fxp_open_send(sftp, fullname, SSH_FXF_WRITE |
					  SSH_FXF_CREAT | SSH_FXF_TRUNC));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	scp_sftp_filehandle = fxp_open_recv(sftp, pktin, rreq);

	if (!scp_sftp_filehandle) {
	    tell_user(stderr, "pscp: unable to open %s: %s",
		      fullname, fxp_error(sftp));
	    errs++;
	    return 1;
	}
	scp_sftp_fileoffset = uint64_make(0, 0);
	scp_sftp_xfer = xfer_upload_init(sftp, scp_sftp_filehandle,
					 scp_sftp_fileoffset);
	sfree(fullname);
	return 0;
    } else {
	char buf[40];
	char sizestr[40];
	uint64_decimal(size, sizestr);
	sprintf(buf, "C%04o %s ", modes, sizestr);
	sftp->back->send(sftp->backhandle, buf, strlen(buf));
	sftp->back->send(sftp->backhandle, name, strlen(name));
	sftp->back->send(sftp->backhandle, "\n", 1);
	return response(sftp);
    }
}

int scp_send_filedata(sftp_handle* sftp, char *data, int len)
{
    if (using_sftp) {
	int ret;
	struct sftp_packet *pktin;

	if (!scp_sftp_filehandle) {
	    return 1;
	}

	while (!xfer_upload_ready(sftp, scp_sftp_xfer)) {
	    pktin = sftp_recv(sftp);
	    ret = xfer_upload_gotpkt(sftp, scp_sftp_xfer, pktin);
	    if (!ret) {
		tell_user(stderr, "error while writing: %s\n", fxp_error(sftp));
		errs++;
		return 1;
	    }
	}

	xfer_upload_data(sftp, scp_sftp_xfer, data, len);

	scp_sftp_fileoffset = uint64_add32(scp_sftp_fileoffset, len);
	return 0;
    } else {
	int bufsize = sftp->back->send(sftp->backhandle, data, len);

	/*
	 * If the network transfer is backing up - that is, the
	 * remote site is not accepting data as fast as we can
	 * produce it - then we must loop on network events until
	 * we have space in the buffer again.
	 */
	while (bufsize > MAX_SCP_BUFSIZE) {
	    if (ssh_sftp_loop_iteration() < 0)
		return 1;
	    bufsize = sftp->back->sendbuffer(sftp->backhandle);
	}

	return 0;
    }
}

int scp_send_finish(sftp_handle* sftp)
{
    if (using_sftp) {
	struct fxp_attrs attrs;
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;
	int ret;

	while (!xfer_done(sftp, scp_sftp_xfer)) {
	    pktin = sftp_recv(sftp);
	    xfer_upload_gotpkt(sftp, scp_sftp_xfer, pktin);
	}
	xfer_cleanup(sftp, scp_sftp_xfer);

	if (!scp_sftp_filehandle) {
	    return 1;
	}
	if (scp_has_times) {
	    attrs.flags = SSH_FILEXFER_ATTR_ACMODTIME;
	    attrs.atime = scp_sftp_atime;
	    attrs.mtime = scp_sftp_mtime;
	    sftp_register(sftp, req = fxp_fsetstat_send(sftp, scp_sftp_filehandle, attrs));
	    rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	    assert(rreq == req);
	    ret = fxp_fsetstat_recv(sftp, pktin, rreq);
	    if (!ret) {
		tell_user(stderr, "unable to set file times: %s\n", fxp_error(sftp));
		errs++;
	    }
	}
	sftp_register(sftp, req = fxp_close_send(sftp, scp_sftp_filehandle));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	fxp_close_recv(sftp, pktin, rreq);
	scp_has_times = 0;
	return 0;
    } else {
	sftp->back->send(sftp->backhandle, "", 1);
	return response(sftp);
    }
}

char *scp_save_remotepath(void)
{
    if (using_sftp)
	return scp_sftp_remotepath;
    else
	return NULL;
}

void scp_restore_remotepath(char *data)
{
    if (using_sftp)
	scp_sftp_remotepath = data;
}

int scp_send_dirname(sftp_handle* sftp, char *name, int modes)
{
    if (using_sftp) {
	char *fullname;
	char const *err;
	struct fxp_attrs attrs;
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;
	int ret;

	if (scp_sftp_targetisdir) {
	    fullname = dupcat(scp_sftp_remotepath, "/", name, NULL);
	} else {
	    fullname = dupstr(scp_sftp_remotepath);
	}

	/*
	 * We don't worry about whether we managed to create the
	 * directory, because if it exists already it's OK just to
	 * use it. Instead, we will stat it afterwards, and if it
	 * exists and is a directory we will assume we were either
	 * successful or it didn't matter.
	 */
	sftp_register(sftp, req = fxp_mkdir_send(sftp, fullname));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	ret = fxp_mkdir_recv(sftp, pktin, rreq);

	if (!ret)
	    err = fxp_error(sftp);
	else
	    err = "server reported no error";

	sftp_register(sftp, req = fxp_stat_send(sftp, fullname));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	ret = fxp_stat_recv(sftp, pktin, rreq, &attrs);

	if (!ret || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) ||
	    !(attrs.permissions & 0040000)) {
	    tell_user(stderr, "unable to create directory %s: %s",
		      fullname, err);
	    errs++;
	    return 1;
	}

	scp_sftp_remotepath = fullname;

	return 0;
    } else {
	char buf[40];
	sprintf(buf, "D%04o 0 ", modes);
	sftp->back->send(sftp->backhandle, buf, strlen(buf));
	sftp->back->send(sftp->backhandle, name, strlen(name));
	sftp->back->send(sftp->backhandle, "\n", 1);
	return response(sftp);
    }
}

int scp_send_enddir(sftp_handle* sftp)
{
    if (using_sftp) {
	sfree(scp_sftp_remotepath);
	return 0;
    } else {
	sftp->back->send(sftp->backhandle, "E\n", 2);
	return response(sftp);
    }
}

/*
 * Yes, I know; I have an scp_sink_setup _and_ an scp_sink_init.
 * That's bad. The difference is that scp_sink_setup is called once
 * right at the start, whereas scp_sink_init is called to
 * initialise every level of recursion in the protocol.
 */
int scp_sink_setup(sftp_handle* sftp, char *source, int preserve, int recursive)
{
    if (using_sftp) {
	char *newsource;

	if (!fxp_init(sftp)) {
	    tell_user(stderr, "unable to initialise SFTP: %s", fxp_error(sftp));
	    errs++;
	    return 1;
	}
	/*
	 * It's possible that the source string we've been given
	 * contains a wildcard. If so, we must split the directory
	 * away from the wildcard itself (throwing an error if any
	 * wildcardness comes before the final slash) and arrange
	 * things so that a dirstack entry will be set up.
	 */
	newsource = snewn(1+strlen(source), char);
	if (!wc_unescape(newsource, source)) {
	    /* Yes, here we go; it's a wildcard. Bah. */
	    char *dupsource, *lastpart, *dirpart, *wildcard;
	    dupsource = dupstr(source);
	    lastpart = stripslashes(dupsource, 0);
	    wildcard = dupstr(lastpart);
	    *lastpart = '\0';
	    if (*dupsource && dupsource[1]) {
		/*
		 * The remains of dupsource are at least two
		 * characters long, meaning the pathname wasn't
		 * empty or just `/'. Hence, we remove the trailing
		 * slash.
		 */
		lastpart[-1] = '\0';
	    } else if (!*dupsource) {
		/*
		 * The remains of dupsource are _empty_ - the whole
		 * pathname was a wildcard. Hence we need to
		 * replace it with ".".
		 */
		sfree(dupsource);
		dupsource = dupstr(".");
	    }

	    /*
	     * Now we have separated our string into dupsource (the
	     * directory part) and wildcard. Both of these will
	     * need freeing at some point. Next step is to remove
	     * wildcard escapes from the directory part, throwing
	     * an error if it contains a real wildcard.
	     */
	    dirpart = snewn(1+strlen(dupsource), char);
	    if (!wc_unescape(dirpart, dupsource)) {
		tell_user(stderr, "%s: multiple-level wildcards unsupported",
			  source);
		errs++;
		sfree(dirpart);
		sfree(wildcard);
		sfree(dupsource);
		return 1;
	    }

	    /*
	     * Now we have dirpart (unescaped, ie a valid remote
	     * path), and wildcard (a wildcard). This will be
	     * sufficient to arrange a dirstack entry.
	     */
	    scp_sftp_remotepath = dirpart;
	    scp_sftp_wildcard = wildcard;
	    sfree(dupsource);
	} else {
	    scp_sftp_remotepath = newsource;
	    scp_sftp_wildcard = NULL;
	}
	scp_sftp_preserve = preserve;
	scp_sftp_recursive = recursive;
	scp_sftp_donethistarget = 0;
	scp_sftp_dirstack_head = NULL;
    }
    return 0;
}

int scp_sink_init(sftp_handle* sftp)
{
    if (!using_sftp) {
	sftp->back->send(sftp->backhandle, "", 1);
    }
    return 0;
}

#define SCP_SINK_FILE   1
#define SCP_SINK_DIR    2
#define SCP_SINK_ENDDIR 3
#define SCP_SINK_RETRY  4	       /* not an action; just try again */
struct scp_sink_action {
    int action;			       /* FILE, DIR, ENDDIR */
    char *buf;			       /* will need freeing after use */
    char *name;			       /* filename or dirname (not ENDDIR) */
    int mode;			       /* access mode (not ENDDIR) */
    uint64 size;		       /* file size (not ENDDIR) */
    int settime;		       /* 1 if atime and mtime are filled */
    unsigned long atime, mtime;	       /* access times for the file */
};

int scp_get_sink_action(sftp_handle* sftp, struct scp_sink_action *act)
{
    if (using_sftp) {
	char *fname;
	int must_free_fname;
	struct fxp_attrs attrs;
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;
	int ret;

	if (!scp_sftp_dirstack_head) {
	    if (!scp_sftp_donethistarget) {
		/*
		 * Simple case: we are only dealing with one file.
		 */
		fname = scp_sftp_remotepath;
		must_free_fname = 0;
		scp_sftp_donethistarget = 1;
	    } else {
		/*
		 * Even simpler case: one file _which we've done_.
		 * Return 1 (finished).
		 */
		return 1;
	    }
	} else {
	    /*
	     * We're now in the middle of stepping through a list
	     * of names returned from fxp_readdir(); so let's carry
	     * on.
	     */
	    struct scp_sftp_dirstack *head = scp_sftp_dirstack_head;
	    while (head->namepos < head->namelen &&
		   (is_dots(head->names[head->namepos].filename) ||
		    (head->wildcard &&
		     !wc_match(head->wildcard,
			       head->names[head->namepos].filename))))
		head->namepos++;       /* skip . and .. */
	    if (head->namepos < head->namelen) {
		head->matched_something = 1;
		fname = dupcat(head->dirpath, "/",
			       head->names[head->namepos++].filename,
			       NULL);
		must_free_fname = 1;
	    } else {
		/*
		 * We've come to the end of the list; pop it off
		 * the stack and return an ENDDIR action (or RETRY
		 * if this was a wildcard match).
		 */
		if (head->wildcard) {
		    act->action = SCP_SINK_RETRY;
		    if (!head->matched_something) {
			tell_user(stderr, "pscp: wildcard '%s' matched "
				  "no files", head->wildcard);
			errs++;
		    }
		    sfree(head->wildcard);

		} else {
		    act->action = SCP_SINK_ENDDIR;
		}

		sfree(head->dirpath);
		sfree(head->names);
		scp_sftp_dirstack_head = head->next;
		sfree(head);

		return 0;
	    }
	}

	/*
	 * Now we have a filename. Stat it, and see if it's a file
	 * or a directory.
	 */
	sftp_register(sftp, req = fxp_stat_send(sftp, fname));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	ret = fxp_stat_recv(sftp, pktin, rreq, &attrs);

	if (!ret || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS)) {
	    tell_user(stderr, "unable to identify %s: %s", fname,
		      ret ? "file type not supplied" : fxp_error(sftp));
	    errs++;
	    return 1;
	}

	if (attrs.permissions & 0040000) {
	    struct scp_sftp_dirstack *newitem;
	    struct fxp_handle *dirhandle;
	    int nnames, namesize;
	    struct fxp_name *ournames;
	    struct fxp_names *names;

	    /*
	     * It's a directory. If we're not in recursive mode,
	     * this merits a complaint (which is fatal if the name
	     * was specified directly, but not if it was matched by
	     * a wildcard).
	     * 
	     * We skip this complaint completely if
	     * scp_sftp_wildcard is set, because that's an
	     * indication that we're not actually supposed to
	     * _recursively_ transfer the dir, just scan it for
	     * things matching the wildcard.
	     */
	    if (!scp_sftp_recursive && !scp_sftp_wildcard) {
		tell_user(stderr, "pscp: %s: is a directory", fname);
		errs++;
		if (must_free_fname) sfree(fname);
		if (scp_sftp_dirstack_head) {
		    act->action = SCP_SINK_RETRY;
		    return 0;
		} else {
		    return 1;
		}
	    }

	    /*
	     * Otherwise, the fun begins. We must fxp_opendir() the
	     * directory, slurp the filenames into memory, return
	     * SCP_SINK_DIR (unless this is a wildcard match), and
	     * set targetisdir. The next time we're called, we will
	     * run through the list of filenames one by one,
	     * matching them against a wildcard if present.
	     * 
	     * If targetisdir is _already_ set (meaning we're
	     * already in the middle of going through another such
	     * list), we must push the other (target,namelist) pair
	     * on a stack.
	     */
	    sftp_register(sftp, req = fxp_opendir_send(sftp, fname));
	    rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	    assert(rreq == req);
	    dirhandle = fxp_opendir_recv(sftp, pktin, rreq);

	    if (!dirhandle) {
		tell_user(stderr, "scp: unable to open directory %s: %s",
			  fname, fxp_error(sftp));
		if (must_free_fname) sfree(fname);
		errs++;
		return 1;
	    }
	    nnames = namesize = 0;
	    ournames = NULL;
	    while (1) {
		int i;

		sftp_register(sftp, req = fxp_readdir_send(sftp, dirhandle));
		rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
		assert(rreq == req);
		names = fxp_readdir_recv(sftp, pktin, rreq);

		if (names == NULL) {
		    if (fxp_error_type(sftp) == SSH_FX_EOF)
			break;
		    tell_user(stderr, "scp: reading directory %s: %s\n",
			      fname, fxp_error(sftp));
		    if (must_free_fname) sfree(fname);
		    sfree(ournames);
		    errs++;
		    return 1;
		}
		if (names->nnames == 0) {
		    fxp_free_names(sftp, names);
		    break;
		}
		if (nnames + names->nnames >= namesize) {
		    namesize += names->nnames + 128;
		    ournames = sresize(ournames, namesize, struct fxp_name);
		}
		for (i = 0; i < names->nnames; i++) {
		    if (!strcmp(names->names[i].filename, ".") ||
			!strcmp(names->names[i].filename, "..")) {
			/*
			 * . and .. are normal consequences of
			 * reading a directory, and aren't worth
			 * complaining about.
			 */
		    } else if (!vet_filename(names->names[i].filename)) {
			tell_user(stderr, "ignoring potentially dangerous server-"
				  "supplied filename '%s'\n",
				  names->names[i].filename);
		    } else
			ournames[nnames++] = names->names[i];
		}
		names->nnames = 0;	       /* prevent free_names */
		fxp_free_names(sftp, names);
	    }
	    sftp_register(sftp, req = fxp_close_send(sftp, dirhandle));
	    rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	    assert(rreq == req);
	    fxp_close_recv(sftp, pktin, rreq);

	    newitem = snew(struct scp_sftp_dirstack);
	    newitem->next = scp_sftp_dirstack_head;
	    newitem->names = ournames;
	    newitem->namepos = 0;
	    newitem->namelen = nnames;
	    if (must_free_fname)
		newitem->dirpath = fname;
	    else
		newitem->dirpath = dupstr(fname);
	    if (scp_sftp_wildcard) {
		newitem->wildcard = scp_sftp_wildcard;
		newitem->matched_something = 0;
		scp_sftp_wildcard = NULL;
	    } else {
		newitem->wildcard = NULL;
	    }
	    scp_sftp_dirstack_head = newitem;

	    if (newitem->wildcard) {
		act->action = SCP_SINK_RETRY;
	    } else {
		act->action = SCP_SINK_DIR;
		act->buf = dupstr(stripslashes(fname, 0));
		act->name = act->buf;
		act->size = uint64_make(0,0);     /* duhh, it's a directory */
		act->mode = 07777 & attrs.permissions;
		if (scp_sftp_preserve &&
		    (attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME)) {
		    act->atime = attrs.atime;
		    act->mtime = attrs.mtime;
		    act->settime = 1;
		} else
		    act->settime = 0;
	    }
	    return 0;

	} else {
	    /*
	     * It's a file. Return SCP_SINK_FILE.
	     */
	    act->action = SCP_SINK_FILE;
	    act->buf = dupstr(stripslashes(fname, 0));
	    act->name = act->buf;
	    if (attrs.flags & SSH_FILEXFER_ATTR_SIZE) {
		act->size = attrs.size;
	    } else
		act->size = uint64_make(ULONG_MAX,ULONG_MAX);   /* no idea */
	    act->mode = 07777 & attrs.permissions;
	    if (scp_sftp_preserve &&
		(attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME)) {
		act->atime = attrs.atime;
		act->mtime = attrs.mtime;
		act->settime = 1;
	    } else
		act->settime = 0;
	    if (must_free_fname)
		scp_sftp_currentname = fname;
	    else
		scp_sftp_currentname = dupstr(fname);
	    return 0;
	}

    } else {
	int done = 0;
	int i, bufsize;
	int action;
	char ch;

	act->settime = 0;
	act->buf = NULL;
	bufsize = 0;

	while (!done) {
	    if (ssh_scp_recv(sftp, (unsigned char *) &ch, 1) <= 0)
		return 1;
	    if (ch == '\n')
		bump(sftp, "Protocol error: Unexpected newline");
	    i = 0;
	    action = ch;
	    do {
		if (ssh_scp_recv(sftp, (unsigned char *) &ch, 1) <= 0)
		    bump(sftp, "Lost connection");
		if (i >= bufsize) {
		    bufsize = i + 128;
		    act->buf = sresize(act->buf, bufsize, char);
		}
		act->buf[i++] = ch;
	    } while (ch != '\n');
	    act->buf[i - 1] = '\0';
	    switch (action) {
	      case '\01':		       /* error */
		tell_user(stderr, "%s\n", act->buf);
		errs++;
		continue;		       /* go round again */
	      case '\02':		       /* fatal error */
		bump(sftp, "%s", act->buf);
	      case 'E':
		sftp->back->send(sftp->backhandle, "", 1);
		act->action = SCP_SINK_ENDDIR;
		return 0;
	      case 'T':
		if (sscanf(act->buf, "%ld %*d %ld %*d",
			   &act->mtime, &act->atime) == 2) {
		    act->settime = 1;
		    sftp->back->send(sftp->backhandle, "", 1);
		    continue;	       /* go round again */
		}
		bump(sftp, "Protocol error: Illegal time format");
	      case 'C':
	      case 'D':
		act->action = (action == 'C' ? SCP_SINK_FILE : SCP_SINK_DIR);
		break;
	      default:
		bump(sftp, "Protocol error: Expected control record");
	    }
	    /*
	     * We will go round this loop only once, unless we hit
	     * `continue' above.
	     */
	    done = 1;
	}

	/*
	 * If we get here, we must have seen SCP_SINK_FILE or
	 * SCP_SINK_DIR.
	 */
	{
	    char sizestr[40];
	
	    if (sscanf(act->buf, "%o %s %n", &act->mode, sizestr, &i) != 2)
		bump(sftp, "Protocol error: Illegal file descriptor format");
	    act->size = uint64_from_decimal(sizestr);
	    act->name = act->buf + i;
	    return 0;
	}
    }
}

int scp_accept_filexfer(sftp_handle* sftp)
{
    if (using_sftp) {
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;

	sftp_register(sftp, req = fxp_open_send(sftp, scp_sftp_currentname, SSH_FXF_READ));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	scp_sftp_filehandle = fxp_open_recv(sftp, pktin, rreq);

	if (!scp_sftp_filehandle) {
	    tell_user(stderr, "pscp: unable to open %s: %s",
		      scp_sftp_currentname, fxp_error(sftp));
	    errs++;
	    return 1;
	}
	scp_sftp_fileoffset = uint64_make(0, 0);
	scp_sftp_xfer = xfer_download_init(sftp, scp_sftp_filehandle,
					   scp_sftp_fileoffset);
	sfree(scp_sftp_currentname);
	return 0;
    } else {
	sftp->back->send(sftp->backhandle, "", 1);
	return 0;		       /* can't fail */
    }
}

int scp_recv_filedata(sftp_handle* sftp, char *data, int len)
{
    if (using_sftp) {
	struct sftp_packet *pktin;
	int ret, actuallen;
	void *vbuf;

	xfer_download_queue(sftp, scp_sftp_xfer);
	pktin = sftp_recv(sftp);
	ret = xfer_download_gotpkt(sftp, scp_sftp_xfer, pktin);

	if (ret < 0) {
	    tell_user(stderr, "pscp: error while reading: %s", fxp_error(sftp));
	    errs++;
	    return -1;
	}

	if (xfer_download_data(sftp, scp_sftp_xfer, &vbuf, &actuallen)) {
	    /*
	     * This assertion relies on the fact that the natural
	     * block size used in the xfer manager is at most that
	     * used in this module. I don't like crossing layers in
	     * this way, but it'll do for now.
	     */
	    assert(actuallen <= len);
	    memcpy(data, vbuf, actuallen);
	    sfree(vbuf);
	} else
	    actuallen = 0;

	scp_sftp_fileoffset = uint64_add32(scp_sftp_fileoffset, actuallen);

	return actuallen;
    } else {
	return ssh_scp_recv(sftp, (unsigned char *) data, len);
    }
}

int scp_finish_filerecv(sftp_handle* sftp)
{
    if (using_sftp) {
	struct sftp_packet *pktin;
	struct sftp_request *req, *rreq;

	/*
	 * Ensure that xfer_done(sftp) will work correctly, so we can
	 * clean up any outstanding requests from the file
	 * transfer.
	 */
	xfer_set_error(sftp, scp_sftp_xfer);
	while (!xfer_done(sftp, scp_sftp_xfer)) {
	    void *vbuf;
	    int len;

	    pktin = sftp_recv(sftp);
	    xfer_download_gotpkt(sftp, scp_sftp_xfer, pktin);
	    if (xfer_download_data(sftp, scp_sftp_xfer, &vbuf, &len))
		sfree(vbuf);
	}
	xfer_cleanup(sftp, scp_sftp_xfer);

	sftp_register(sftp, req = fxp_close_send(sftp, scp_sftp_filehandle));
	rreq = sftp_find_request(sftp, pktin = sftp_recv(sftp));
	assert(rreq == req);
	fxp_close_recv(sftp, pktin, rreq);
	return 0;
    } else {
	sftp->back->send(sftp->backhandle, "", 1);
	return response(sftp);
    }
}

/* ----------------------------------------------------------------------
 *  Send an error message to the other side and to the screen.
 *  Increment error counter.
 */
static void run_err(sftp_handle* sftp, const char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    errs++;
    str = dupvprintf(fmt, ap);
    str2 = dupcat("scp: ", str, "\n", NULL);
    sfree(str);
    scp_send_errmsg(sftp, str2);
    tell_user(stderr, "%s", str2);
    va_end(ap);
    sfree(str2);
}

/*
 *  Execute the source part of the SCP protocol.
 */
static void source(sftp_handle* sftp, char *src)
{
    uint64 size;
    unsigned long mtime, atime;
    char *last;
    RFile *f;
    int attr;
    uint64 i;
    uint64 stat_bytes;
    time_t stat_starttime, stat_lasttime;

    attr = file_type(src);
    if (attr == FILE_TYPE_NONEXISTENT ||
	attr == FILE_TYPE_WEIRD) {
	run_err(sftp, "%s: %s file or directory", src,
		(attr == FILE_TYPE_WEIRD ? "Not a" : "No such"));
	return;
    }

    if (attr == FILE_TYPE_DIRECTORY) {
	if (recursive) {
	    /*
	     * Avoid . and .. directories.
	     */
	    char *p;
	    p = strrchr(src, '/');
	    if (!p)
		p = strrchr(src, '\\');
	    if (!p)
		p = src;
	    else
		p++;
	    if (!strcmp(p, ".") || !strcmp(p, ".."))
		/* skip . and .. */ ;
	    else
		rsource(sftp, src);
	} else {
	    run_err(sftp, "%s: not a regular file", src);
	}
	return;
    }

    if ((last = strrchr(src, '/')) == NULL)
	last = src;
    else
	last++;
    if (strrchr(last, '\\') != NULL)
	last = strrchr(last, '\\') + 1;
    if (last == src && strchr(src, ':') != NULL)
	last = strchr(src, ':') + 1;

    f = open_existing_file(src, &size, &mtime, &atime);
    if (f == NULL) {
	run_err(sftp, "%s: Cannot open file", src);
	return;
    }
    if (preserve) {
	if (scp_send_filetimes(sftp, mtime, atime))
	    return;
    }

    if (sftp->verbose) {
	char sizestr[40];
	uint64_decimal(size, sizestr);
	tell_user(stderr, "Sending file %s, size=%s", last, sizestr);
    }
    if (scp_send_filename(sftp, last, size, 0644))
	return;

    stat_bytes = uint64_make(0,0);
    stat_starttime = time(NULL);
    stat_lasttime = 0;

    for (i = uint64_make(0,0);
	 uint64_compare(i,size) < 0;
	 i = uint64_add32(i,4096)) {
	char transbuf[4096];
	int j, k = 4096;

	if (uint64_compare(uint64_add32(i, k),size) > 0) /* i + k > size */ 
	    k = (uint64_subtract(size, i)).lo; 	/* k = size - i; */
	if ((j = read_from_file(f, transbuf, k)) != k) {
	    if (statistics)
		printf("\n");
	    bump(sftp, "%s: Read error", src);
	}
	if (scp_send_filedata(sftp, transbuf, k))
	    bump(sftp, "%s: Network error occurred", src);

	if (statistics) {
	    stat_bytes = uint64_add32(stat_bytes, k);
	    if (time(NULL) != stat_lasttime ||
		(uint64_compare(uint64_add32(i, k), size) == 0)) {
		stat_lasttime = time(NULL);
		print_stats(last, size, stat_bytes,
			    stat_starttime, stat_lasttime);
	    }
	}

    }
    close_rfile(f);

    (void) scp_send_finish(sftp);
}

/*
 *  Recursively send the contents of a directory.
 */
static void rsource(sftp_handle* sftp, char *src)
{
    char *last;
    char *save_target;
    DirHandle *dir;

    if ((last = strrchr(src, '/')) == NULL)
	last = src;
    else
	last++;
    if (strrchr(last, '\\') != NULL)
	last = strrchr(last, '\\') + 1;
    if (last == src && strchr(src, ':') != NULL)
	last = strchr(src, ':') + 1;

    /* maybe send filetime */

    save_target = scp_save_remotepath();

    if (sftp->verbose)
	tell_user(stderr, "Entering directory: %s", last);
    if (scp_send_dirname(sftp, last, 0755))
	return;

    dir = open_directory(src);
    if (dir != NULL) {
	char *filename;
	while ((filename = read_filename(dir)) != NULL) {
	    char *foundfile = dupcat(src, "/", filename, NULL);
	    source(sftp, foundfile);
	    sfree(foundfile);
	    sfree(filename);
	}
    }
    close_directory(dir);

    (void) scp_send_enddir(sftp);

    scp_restore_remotepath(save_target);
}

/*
 * Execute the sink part of the SCP protocol.
 */
static void sink(sftp_handle* sftp, char *targ, char *src)
{
    char *destfname;
    int targisdir = 0;
    int exists;
    int attr;
    WFile *f;
    uint64 received;
    int wrerror = 0;
    uint64 stat_bytes;
    time_t stat_starttime, stat_lasttime;
    char *stat_name;

    attr = file_type(targ);
    if (attr == FILE_TYPE_DIRECTORY)
	targisdir = 1;

    if (targetshouldbedirectory && !targisdir)
	bump(sftp, "%s: Not a directory", targ);

    scp_sink_init(sftp);
    while (1) {
	struct scp_sink_action act;
	if (scp_get_sink_action(sftp, &act))
	    return;

	if (act.action == SCP_SINK_ENDDIR)
	    return;

	if (act.action == SCP_SINK_RETRY)
	    continue;

	if (targisdir) {
	    /*
	     * Prevent the remote side from maliciously writing to
	     * files outside the target area by sending a filename
	     * containing `../'. In fact, it shouldn't be sending
	     * filenames with any slashes or colons in at all; so
	     * we'll find the last slash, backslash or colon in the
	     * filename and use only the part after that. (And
	     * warn!)
	     * 
	     * In addition, we also ensure here that if we're
	     * copying a single file and the target is a directory
	     * (common usage: `pscp host:filename .') the remote
	     * can't send us a _different_ file name. We can
	     * distinguish this case because `src' will be non-NULL
	     * and the last component of that will fail to match
	     * (the last component of) the name sent.
	     * 
	     * Well, not always; if `src' is a wildcard, we do
	     * expect to get sftp->back filenames that don't correspond
	     * exactly to it. Ideally in this case, we would like
	     * to ensure that the returned filename actually
	     * matches the wildcard pattern - but one of SCP's
	     * protocol infelicities is that wildcard matching is
	     * done at the server end _by the server's rules_ and
	     * so in general this is infeasible. Hence, we only
	     * accept filenames that don't correspond to `src' if
	     * unsafe mode is enabled or we are using SFTP (which
	     * resolves remote wildcards on the client side and can
	     * be trusted).
	     */
	    char *striptarget, *stripsrc;

	    striptarget = stripslashes(act.name, 1);
	    if (striptarget != act.name) {
		tell_user(stderr, "warning: remote host sent a compound"
			  " pathname '%s'", act.name);
		tell_user(stderr, "         renaming local file to '%s'",
                          striptarget);
	    }

	    /*
	     * Also check to see if the target filename is '.' or
	     * '..', or indeed '...' and so on because Windows
	     * appears to interpret those like '..'.
	     */
	    if (is_dots(striptarget)) {
		bump(sftp, "security violation: remote host attempted to write to"
		     " a '.' or '..' path!");
	    }

	    if (src) {
		stripsrc = stripslashes(src, 1);
		if (strcmp(striptarget, stripsrc) &&
		    !using_sftp && !scp_unsafe_mode) {
		    tell_user(stderr, "warning: remote host tried to write "
			      "to a file called '%s'", striptarget);
		    tell_user(stderr, "         when we requested a file "
			      "called '%s'.", stripsrc);
		    tell_user(stderr, "         If this is a wildcard, "
			      "consider upgrading to SSH-2 or using");
		    tell_user(stderr, "         the '-unsafe' option. Renaming"
			      " of this file has been disallowed.");
		    /* Override the name the server provided with our own. */
		    striptarget = stripsrc;
		}
	    }

	    if (targ[0] != '\0')
		destfname = dir_file_cat(targ, striptarget);
	    else
		destfname = dupstr(striptarget);
	} else {
	    /*
	     * In this branch of the if, the target area is a
	     * single file with an explicitly specified name in any
	     * case, so there's no danger.
	     */
	    destfname = dupstr(targ);
	}
	attr = file_type(destfname);
	exists = (attr != FILE_TYPE_NONEXISTENT);

	if (act.action == SCP_SINK_DIR) {
	    if (exists && attr != FILE_TYPE_DIRECTORY) {
		run_err(sftp, "%s: Not a directory", destfname);
		continue;
	    }
	    if (!exists) {
		if (!create_directory(destfname)) {
		    run_err(sftp, "%s: Cannot create directory", destfname);
		    continue;
		}
	    }
	    sink(sftp, destfname, NULL);
	    /* can we set the timestamp for directories ? */
	    continue;
	}

	f = open_new_file(destfname);
	if (f == NULL) {
	    run_err(sftp, "%s: Cannot create file", destfname);
	    continue;
	}

	if (scp_accept_filexfer(sftp))
	    return;

	stat_bytes = uint64_make(0, 0);
	stat_starttime = time(NULL);
	stat_lasttime = 0;
	stat_name = stripslashes(destfname, 1);

	received = uint64_make(0, 0);
	while (uint64_compare(received,act.size) < 0) {
	    char transbuf[32768];
	    uint64 blksize;
	    int read;
	    blksize = uint64_make(0, 32768);
	    if (uint64_compare(blksize,uint64_subtract(act.size,received)) > 0)
	      blksize = uint64_subtract(act.size,received);
	    read = scp_recv_filedata(sftp, transbuf, (int)blksize.lo);
	    if (read <= 0)
		bump(sftp, "Lost connection");
	    if (wrerror)
		continue;
	    if (write_to_file(f, transbuf, read) != (int)read) {
		wrerror = 1;
		/* FIXME: in sftp we can actually abort the transfer */
		if (statistics)
		    printf("\r%-25.25s | %50s\n",
			   stat_name,
			   "Write error.. waiting for end of file");
		continue;
	    }
	    if (statistics) {
		stat_bytes = uint64_add32(stat_bytes,read);
		if (time(NULL) > stat_lasttime ||
		    uint64_compare(uint64_add32(received, read), act.size) == 0) {
		    stat_lasttime = time(NULL);
		    print_stats(stat_name, act.size, stat_bytes,
				stat_starttime, stat_lasttime);
		}
	    }
	    received = uint64_add32(received, read);
	}
	if (act.settime) {
	    set_file_times(f, act.mtime, act.atime);
	}

	close_wfile(f);
	if (wrerror) {
	    run_err(sftp, "%s: Write error", destfname);
	    continue;
	}
	(void) scp_finish_filerecv(sftp);
	sfree(destfname);
	sfree(act.buf);
    }
}

/*
 * We will copy local files to a remote server.
 */
static void toremote(sftp_handle* sftp, int argc, char *argv[])
{
    char *src, *targ, *host, *user;
    char *cmd;
    int i, wc_type;

    targ = argv[argc - 1];

    /* Separate host from filename */
    host = targ;
    targ = colon(targ);
    if (targ == NULL)
	bump(sftp, "targ == NULL in toremote(sftp)");
    *targ++ = '\0';
    if (*targ == '\0')
	targ = ".";
    /* Substitute "." for empty target */

    /* Separate host and username */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    if (argc == 2) {
	if (colon(argv[0]) != NULL)
	    bump(sftp, "%s: Remote to remote not supported", argv[0]);

	wc_type = test_wildcard(argv[0], 1);
	if (wc_type == WCTYPE_NONEXISTENT)
	    bump(sftp, "%s: No such file or directory\n", argv[0]);
	else if (wc_type == WCTYPE_WILDCARD)
	    targetshouldbedirectory = 1;
    }

    cmd = dupprintf("scp%s%s%s%s -t %s",
		    sftp->verbose ? " -v" : "",
		    recursive ? " -r" : "",
		    preserve ? " -p" : "",
		    targetshouldbedirectory ? " -d" : "", targ);
    do_cmd(sftp, host, user, cmd);
    sfree(cmd);

    if (scp_source_setup(sftp, targ, targetshouldbedirectory))
	return;

    for (i = 0; i < argc - 1; i++) {
	src = argv[i];
	if (colon(src) != NULL) {
	    tell_user(stderr, "%s: Remote to remote not supported\n", src);
	    errs++;
	    continue;
	}

	wc_type = test_wildcard(src, 1);
	if (wc_type == WCTYPE_NONEXISTENT) {
	    run_err(sftp, "%s: No such file or directory", src);
	    continue;
	} else if (wc_type == WCTYPE_FILENAME) {
	    source(sftp, src);
	    continue;
	} else {
	    WildcardMatcher *wc;
	    char *filename;

	    wc = begin_wildcard_matching(src);
	    if (wc == NULL) {
		run_err(sftp, "%s: No such file or directory", src);
		continue;
	    }

	    while ((filename = wildcard_get_filename(wc)) != NULL) {
		source(sftp, filename);
		sfree(filename);
	    }

	    finish_wildcard_matching(wc);
	}
    }
}

/*
 *  We will copy files from a remote server to the local machine.
 */
static void tolocal(sftp_handle* sftp, int argc, char *argv[])
{
    char *src, *targ, *host, *user;
    char *cmd;

    if (argc != 2)
	bump(sftp, "More than one remote source not supported");

    src = argv[0];
    targ = argv[1];

    /* Separate host from filename */
    host = src;
    src = colon(src);
    if (src == NULL)
	bump(sftp, "Local to local copy not supported");
    *src++ = '\0';
    if (*src == '\0')
	src = ".";
    /* Substitute "." for empty filename */

    /* Separate username and hostname */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    cmd = dupprintf("scp%s%s%s%s -f %s",
		    sftp->verbose ? " -v" : "",
		    recursive ? " -r" : "",
		    preserve ? " -p" : "",
		    targetshouldbedirectory ? " -d" : "", src);
    do_cmd(sftp, host, user, cmd);
    sfree(cmd);

    if (scp_sink_setup(sftp, src, preserve, recursive))
	return;

    sink(sftp, targ, src);
}

/*
 *  We will issue a list command to get a remote directory.
 */
static void get_dir_list(sftp_handle* sftp, int argc, char *argv[])
{
    char *src, *host, *user;
    char *cmd, *p, *q;
    char c;

    src = argv[0];

    /* Separate host from filename */
    host = src;
    src = colon(src);
    if (src == NULL)
	bump(sftp, "Local file listing not supported");
    *src++ = '\0';
    if (*src == '\0')
	src = ".";
    /* Substitute "." for empty filename */

    /* Separate username and hostname */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    cmd = snewn(4 * strlen(src) + 100, char);
    strcpy(cmd, "ls -la '");
    p = cmd + strlen(cmd);
    for (q = src; *q; q++) {
	if (*q == '\'') {
	    *p++ = '\'';
	    *p++ = '\\';
	    *p++ = '\'';
	    *p++ = '\'';
	} else {
	    *p++ = *q;
	}
    }
    *p++ = '\'';
    *p = '\0';

    do_cmd(sftp, host, user, cmd);
    sfree(cmd);

    if (using_sftp) {
	scp_sftp_listdir(sftp, src);
    } else {
	while (ssh_scp_recv(sftp, (unsigned char *) &c, 1) > 0)
	    tell_char(stdout, c);
    }
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("PuTTY Secure Copy client\n");
    printf("%s\n", ver);
    printf("Usage: pscp [options] [user@]host:source target\n");
    printf
	("       pscp [options] source [source...] [user@]host:target\n");
    printf("       pscp [options] -ls [user@]host:filespec\n");
    printf("Options:\n");
    printf("  -V        print version information and exit\n");
    printf("  -pgpfp    print PGP key fingerprints and exit\n");
    printf("  -p        preserve file attributes\n");
    printf("  -q        quiet, don't show statistics\n");
    printf("  -r        copy directories recursively\n");
    printf("  -v        show sftp->verbose messages\n");
    printf("  -load sessname  Load settings from saved session\n");
    printf("  -P port   connect to specified port\n");
    printf("  -l user   connect with specified username\n");
    printf("  -pw passw login with specified password\n");
    printf("  -1 -2     force use of particular SSH protocol version\n");
    printf("  -4 -6     force use of IPv4 or IPv6\n");
    printf("  -C        enable compression\n");
    printf("  -i key    private key file for authentication\n");
    printf("  -noagent  disable use of Pageant\n");
    printf("  -agent    enable use of Pageant\n");
    printf("  -batch    disable all interactive prompts\n");
    printf("  -unsafe   allow server-side wildcards (DANGEROUS)\n");
    printf("  -sftp     force use of SFTP protocol\n");
    printf("  -scp      force use of SCP protocol\n");
#if 0
    /*
     * -gui is an internal option, used by GUI front ends to get
     * pscp to pass progress reports sftp->back to them. It's not an
     * ordinary user-accessible option, so it shouldn't be part of
     * the command-line help. The only people who need to know
     * about it are programmers, and they can read the source.
     */
    printf
	("  -gui hWnd GUI mode with the windows handle for receiving messages\n");
#endif
    cleanup_exit(1);
}

void version(void)
{
    printf("pscp: %s\n", ver);
    cleanup_exit(1);
}

void cmdline_error(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "pscp: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fprintf(stderr, "\n      try typing just \"pscp\" for help\n");
    exit(1);
}

/*
 * Main program. (Called `psftp_main' because it gets called from
 * *sftp.c; bit silly, I know, but it had to be called _something_.)
 */
int psftp_main(int argc, char *argv[])
{
    int i;
    sftp_handle _sftp;
    sftp_handle *sftp = &_sftp;
    sftp_handle_init(sftp);

    default_protocol = PROT_TELNET;

    flags = FLAG_STDERR
#ifdef FLAG_SYNCAGENT
	| FLAG_SYNCAGENT
#endif
	;
    cmdline_tooltype = TOOLTYPE_FILETRANSFER;
    sk_init();

    /* Load Default Settings before doing anything else. */
    do_defaults(NULL, &sftp->cfg);
    loaded_session = FALSE;

    for (i = 1; i < argc; i++) {
	int ret;
	if (argv[i][0] != '-')
	    break;
	ret = cmdline_process_param(argv[i], i+1<argc?argv[i+1]:NULL, 1, &sftp->cfg);
	if (ret == -2) {
	    cmdline_error("option \"%s\" requires an argument", argv[i]);
	} else if (ret == 2) {
	    i++;	       /* skip next argument */
	} else if (ret == 1) {
	    /* We have our own verbosity in addition to `flags'. */
	    if (flags & FLAG_VERBOSE)
		sftp->verbose = 1;
        } else if (strcmp(argv[i], "-pgpfp") == 0) {
            pgp_fingerprints();
            return 1;
	} else if (strcmp(argv[i], "-r") == 0) {
	    recursive = 1;
	} else if (strcmp(argv[i], "-p") == 0) {
	    preserve = 1;
	} else if (strcmp(argv[i], "-q") == 0) {
	    statistics = 0;
	} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-?") == 0) {
	    usage();
	} else if (strcmp(argv[i], "-V") == 0) {
            version();
        } else if (strcmp(argv[i], "-ls") == 0) {
	    list = 1;
	} else if (strcmp(argv[i], "-batch") == 0) {
	    console_batch_mode = 1;
	} else if (strcmp(argv[i], "-unsafe") == 0) {
	    scp_unsafe_mode = 1;
	} else if (strcmp(argv[i], "-sftp") == 0) {
	    try_scp = 0; try_sftp = 1;
	} else if (strcmp(argv[i], "-scp") == 0) {
	    try_scp = 1; try_sftp = 0;
	} else if (strcmp(argv[i], "--") == 0) {
	    i++;
	    break;
	} else {
	    cmdline_error("unknown option \"%s\"", argv[i]);
	}
    }
    argc -= i;
    argv += i;
    sftp->back = NULL;

    if (list) {
	if (argc != 1)
	    usage();
	get_dir_list(sftp, argc, argv);

    } else {

	if (argc < 2)
	    usage();
	if (argc > 2)
	    targetshouldbedirectory = 1;

	if (colon(argv[argc - 1]) != NULL)
	    toremote(sftp, argc, argv);
	else
	    tolocal(sftp, argc, argv);
    }

    if (sftp->back != NULL && sftp->back->connected(sftp->backhandle)) {
	char ch;
	sftp->back->special(sftp->backhandle, TS_EOF);
	ssh_scp_recv(sftp, (unsigned char *) &ch, 1);
    }
    random_save_seed();

    cmdline_cleanup();
    console_provide_logctx(NULL);
    sftp->back->free(sftp->backhandle);
    sftp->backhandle = NULL;
    sftp->back = NULL;
    sk_cleanup();
    return (errs == 0 ? 0 : 1);
}

/* end */
