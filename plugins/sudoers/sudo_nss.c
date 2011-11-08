/*
 * Copyright (c) 2007-2011 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

#include "sudoers.h"
#include "lbuf.h"

extern struct sudo_nss sudo_nss_file;
#ifdef HAVE_LDAP
extern struct sudo_nss sudo_nss_ldap;
#endif

#if defined(HAVE_LDAP) && defined(_PATH_NSSWITCH_CONF)
/*
 * Read in /etc/nsswitch.conf
 * Returns a tail queue of matches.
 */
struct sudo_nss_list *
sudo_read_nss(void)
{
    FILE *fp;
    char *cp;
    int saw_files = FALSE;
    int saw_ldap = FALSE;
    int got_match = FALSE;
    static struct sudo_nss_list snl;
    debug_decl(sudo_read_nss, SUDO_DEBUG_NSS)

    if ((fp = fopen(_PATH_NSSWITCH_CONF, "r")) == NULL)
	goto nomatch;

    while ((cp = sudo_parseln(fp)) != NULL) {
	/* Skip blank or comment lines */
	if (*cp == '\0')
	    continue;

	/* Look for a line starting with "sudoers:" */
	if (strncasecmp(cp, "sudoers:", 8) != 0)
	    continue;

	/* Parse line */
	for ((cp = strtok(cp + 8, " \t")); cp != NULL; (cp = strtok(NULL, " \t"))) {
	    if (strcasecmp(cp, "files") == 0 && !saw_files) {
		tq_append(&snl, &sudo_nss_file);
		got_match = TRUE;
	    } else if (strcasecmp(cp, "ldap") == 0 && !saw_ldap) {
		tq_append(&snl, &sudo_nss_ldap);
		got_match = TRUE;
	    } else if (strcasecmp(cp, "[NOTFOUND=return]") == 0 && got_match) {
		/* NOTFOUND affects the most recent entry */
		tq_last(&snl)->ret_if_notfound = TRUE;
		got_match = FALSE;
	    } else
		got_match = FALSE;
	}
	/* Only parse the first "sudoers:" line */
	break;
    }
    fclose(fp);

nomatch:
    /* Default to files only if no matches */
    if (tq_empty(&snl))
	tq_append(&snl, &sudo_nss_file);

    debug_return_ptr(&snl);
}

#else /* HAVE_LDAP && _PATH_NSSWITCH_CONF */

# if defined(HAVE_LDAP) && defined(_PATH_NETSVC_CONF)

/*
 * Read in /etc/netsvc.conf (like nsswitch.conf on AIX)
 * Returns a tail queue of matches.
 */
struct sudo_nss_list *
sudo_read_nss(void)
{
    FILE *fp;
    char *cp, *ep;
    int saw_files = FALSE;
    int saw_ldap = FALSE;
    int got_match = FALSE;
    static struct sudo_nss_list snl;
    debug_decl(sudo_read_nss, SUDO_DEBUG_NSS)

    if ((fp = fopen(_PATH_NETSVC_CONF, "r")) == NULL)
	goto nomatch;

    while ((cp = sudo_parseln(fp)) != NULL) {
	/* Skip blank or comment lines */
	if (*cp == '\0')
	    continue;

	/* Look for a line starting with "sudoers = " */
	if (strncasecmp(cp, "sudoers", 7) != 0)
	    continue;
	cp += 7;
	while (isspace((unsigned char)*cp))
	    cp++;
	if (*cp++ != '=')
	    continue;

	/* Parse line */
	for ((cp = strtok(cp, ",")); cp != NULL; (cp = strtok(NULL, ","))) {
	    /* Trim leading whitespace. */
	    while (isspace((unsigned char)*cp))
		cp++;

	    if (!saw_files && strncasecmp(cp, "files", 5) == 0 &&
		(isspace((unsigned char)cp[5]) || cp[5] == '\0')) {
		tq_append(&snl, &sudo_nss_file);
		got_match = TRUE;
		ep = &cp[5];
	    } else if (!saw_ldap && strncasecmp(cp, "ldap", 4) == 0 &&
		(isspace((unsigned char)cp[4]) || cp[4] == '\0')) {
		tq_append(&snl, &sudo_nss_ldap);
		got_match = TRUE;
		ep = &cp[4];
	    } else {
		got_match = FALSE;
	    }

	    /* check for = auth qualifier */
	    if (got_match && *ep) {
		cp = ep;
		while (isspace((unsigned char)*cp) || *cp == '=')
		    cp++;
		if (strncasecmp(cp, "auth", 4) == 0 &&
		    (isspace((unsigned char)cp[4]) || cp[4] == '\0')) {
		    tq_last(&snl)->ret_if_found = TRUE;
		}
	    }
	}
	/* Only parse the first "sudoers" line */
	break;
    }
    fclose(fp);

nomatch:
    /* Default to files only if no matches */
    if (tq_empty(&snl))
	tq_append(&snl, &sudo_nss_file);

    debug_return_ptr(&snl);
}

# else /* !_PATH_NETSVC_CONF && !_PATH_NSSWITCH_CONF */

/*
 * Non-nsswitch.conf version with hard-coded order.
 */
struct sudo_nss_list *
sudo_read_nss(void)
{
    static struct sudo_nss_list snl;
    debug_decl(sudo_read_nss, SUDO_DEBUG_NSS)

#  ifdef HAVE_LDAP
    tq_append(&snl, &sudo_nss_ldap);
#  endif
    tq_append(&snl, &sudo_nss_file);

    debug_return_ptr(&snl);
}

# endif /* !HAVE_LDAP || !_PATH_NETSVC_CONF */

#endif /* HAVE_LDAP && _PATH_NSSWITCH_CONF */

static int
output(const char *buf)
{
    struct sudo_conv_message msg;
    struct sudo_conv_reply repl;
    debug_decl(output, SUDO_DEBUG_NSS)

    /* Call conversation function */
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = SUDO_CONV_INFO_MSG;
    msg.msg = buf;
    memset(&repl, 0, sizeof(repl));
    if (sudo_conv(1, &msg, &repl) == -1)
	debug_return_int(0);
    debug_return_int(strlen(buf));
}

/*
 * Print out privileges for the specified user.
 * We only get here if the user is allowed to run something on this host.
 */
void
display_privs(struct sudo_nss_list *snl, struct passwd *pw)
{
    struct sudo_nss *nss;
    struct lbuf defs, privs;
    int count, olen;
    debug_decl(display_privs, SUDO_DEBUG_NSS)

    lbuf_init(&defs, output, 4, NULL, sudo_user.cols);
    lbuf_init(&privs, output, 4, NULL, sudo_user.cols);

    /* Display defaults from all sources. */
    lbuf_append(&defs, _("Matching Defaults entries for %s on this host:\n"),
	pw->pw_name);
    count = 0;
    tq_foreach_fwd(snl, nss) {
	count += nss->display_defaults(nss, pw, &defs);
    }
    if (count)
	lbuf_append(&defs, "\n\n");
    else
	defs.len = 0;

    /* Display Runas and Cmnd-specific defaults from all sources. */
    olen = defs.len;
    lbuf_append(&defs, _("Runas and Command-specific defaults for %s:\n"),
	pw->pw_name);
    count = 0;
    tq_foreach_fwd(snl, nss) {
	count += nss->display_bound_defaults(nss, pw, &defs);
    }
    if (count)
	lbuf_append(&defs, "\n\n");
    else
	defs.len = olen;

    /* Display privileges from all sources. */
    lbuf_append(&privs,
	_("User %s may run the following commands on this host:\n"),
	pw->pw_name);
    count = 0;
    tq_foreach_fwd(snl, nss) {
	count += nss->display_privs(nss, pw, &privs);
    }
    if (count) {
	lbuf_print(&defs);
	lbuf_print(&privs);
    } else {
	printf(_("User %s is not allowed to run sudo on %s.\n"), pw->pw_name,
	    user_shost);
    }

    lbuf_destroy(&defs);
    lbuf_destroy(&privs);

    debug_return;
}

/*
 * Check user_cmnd against sudoers and print the matching entry if the
 * command is allowed.
 * Returns TRUE if the command is allowed, else FALSE.
 */
int
display_cmnd(struct sudo_nss_list *snl, struct passwd *pw)
{
    struct sudo_nss *nss;
    debug_decl(display_cmnd, SUDO_DEBUG_NSS)

    tq_foreach_fwd(snl, nss) {
	if (nss->display_cmnd(nss, pw) == 0)
	    debug_return_bool(TRUE);
    }
    debug_return_bool(FALSE);
}