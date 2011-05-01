/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
 
/*
 * 1999-02-22 Arkadiusz Mi�kiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include "nls.h"

#include "widechar.h"
#include "c.h"
#include "xalloc.h"

#ifdef HAVE_WIDECHAR
#define wcs_width(s) wcswidth(s,wcslen(s))
static wchar_t *mbs_to_wcs(const char *);
#else
#define wcs_width(s) strlen(s)
#define mbs_to_wcs(s) xstrdup(s)
static char *mtsafe_strtok(char *, const char *, char **);
#define wcstok mtsafe_strtok
#endif

#define DEFCOLS     25
#define TAB         8
#define DEFNUM      1000
#define MAXLINELEN  (LINE_MAX + 1)

static void c_columnate(void);
static void input(FILE *);
static void maketbl(void);
static void print(void);
static void r_columnate(void);

typedef struct _tbl {
	wchar_t **list;
	int cols, *len;
} TBL;

int termwidth = 80;		/* default terminal width */

int entries;			/* number of records */
int eval;			/* exit value */
int maxlength;			/* longest record */
wchar_t **list;			/* array of pointers to records */
wchar_t default_separator[] = { '\t', ' ', 0 };
wchar_t *separator = default_separator;	/* field separator for table option */

static const struct option longopts[] =
{
	{ "help",	0, 0, 'h' },
	{ "version",    0, 0, 'V' },
	{ "columns",	0, 0, 'c' },
	{ "table",	0, 0, 't' },
	{ "separator",	0, 0, 's' },
	{ "fillrows",	0, 0, 'x' },
	{ NULL,		0, 0, 0 },
};

static void __attribute__((__noreturn__)) usage(int rc)
{
	FILE *out = rc == EXIT_FAILURE ? stderr : stdout;

	fprintf(out, _("\nUsage: %s [options] [file ...]\n"),
				program_invocation_short_name);
	fprintf(out, _("\nOptions:\n"));

	fprintf(out, _(
	" -h, --help               displays this help text\n"
	" -V, --version            output version information and exit\n"
	" -c, --columns <width>    width of output in number of characters\n"
	" -t, --table              create a table\n"
	" -s, --separator <string> table delimeter\n"
	" -x, --fillrows           fill rows before columns\n"));

	fprintf(out, _("\nFor more information see column(1).\n"));
	exit(rc);
}

int main(int argc, char **argv)
{
	struct winsize win;
	FILE *fp;
	int ch, tflag, xflag;
	char *p;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &win) == -1 || !win.ws_col) {
		if ((p = getenv("COLUMNS")) != NULL)
			termwidth = atoi(p);
	} else
		termwidth = win.ws_col;

	tflag = xflag = 0;
	while ((ch = getopt_long(argc, argv, "hVc:s:tx", longopts, NULL)) != -1)
		switch(ch) {
		case 'h':
			usage(EXIT_SUCCESS);
			break;
		case 'V':
			printf(_("%s from %s\n"), program_invocation_short_name,
				 PACKAGE_STRING);
				 return(EXIT_SUCCESS);
		case 'c':
			termwidth = atoi(optarg);
			break;
		case 's':
			separator = mbs_to_wcs(optarg);
			break;
		case 't':
			tflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage(EXIT_FAILURE);
	}
	argc -= optind;
	argv += optind;

	if (!*argv)
		input(stdin);
	else
		for (; *argv; ++argv) {
			if ((fp = fopen(*argv, "r")) != NULL) {
				input(fp);
				(void)fclose(fp);
			} else {
				warn("%s", *argv);
				eval = EXIT_FAILURE;
			}
		}

	if (!entries)
		exit(eval);

	if (tflag)
		maketbl();
	else if (maxlength >= termwidth)
		print();
	else if (xflag)
		c_columnate();
	else
		r_columnate();
	if (ferror(stdout) || fclose(stdout))
		eval = EXIT_FAILURE;
	exit(eval);
}

static void c_columnate()
{
	int chcnt, col, cnt, endcol, numcols;
	wchar_t **lp;

	maxlength = (maxlength + TAB) & ~(TAB - 1);
	numcols = termwidth / maxlength;
	endcol = maxlength;
	for (chcnt = col = 0, lp = list;; ++lp) {
		fputws(*lp, stdout);
		chcnt += wcs_width(*lp);
		if (!--entries)
			break;
		if (++col == numcols) {
			chcnt = col = 0;
			endcol = maxlength;
			putwchar('\n');
		} else {
			while ((cnt = ((chcnt + TAB) & ~(TAB - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += maxlength;
		}
	}
	if (chcnt)
		putwchar('\n');
}

static void r_columnate()
{
	int base, chcnt, cnt, col, endcol, numcols, numrows, row;

	maxlength = (maxlength + TAB) & ~(TAB - 1);
	numcols = termwidth / maxlength;
	if (!numcols) 
		numcols = 1;
	numrows = entries / numcols;
	if (entries % numcols)
		++numrows;

	for (row = 0; row < numrows; ++row) {
		endcol = maxlength;
		for (base = row, chcnt = col = 0; col < numcols; ++col) {
			fputws(list[base], stdout);
			chcnt += wcs_width(list[base]);
			if ((base += numrows) >= entries)
				break;
			while ((cnt = ((chcnt + TAB) & ~(TAB - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += maxlength;
		}
		putwchar('\n');
	}
}

static void print()
{
	int cnt;
	wchar_t **lp;

	for (cnt = entries, lp = list; cnt--; ++lp) {
		fputws(*lp, stdout);
		putwchar('\n');
	}
}

static void maketbl()
{
	TBL *t;
	int cnt, i;
	wchar_t *p, **lp;
	ssize_t *lens;
	ssize_t maxcols = DEFCOLS, coloff;
	TBL *tbl;
	wchar_t **cols;
	wchar_t *wcstok_state;

	t = tbl = xcalloc(entries, sizeof(TBL));
	cols = xcalloc(maxcols, sizeof(wchar_t *));
	lens = xcalloc(maxcols, sizeof(ssize_t));

	for (lp = list, cnt = 0; cnt < entries; ++cnt, ++lp, ++t) {
		coloff = 0;
		p = *lp;
		while ((cols[coloff] = wcstok(p, separator, &wcstok_state)) != NULL) {
			if (++coloff == maxcols) {
				maxcols += DEFCOLS;
				cols = xrealloc(cols, maxcols * sizeof(wchar_t *));
				lens = xrealloc(lens, maxcols * sizeof(ssize_t));
				/* zero fill only new memory */
				memset(lens + ((maxcols - DEFCOLS) * sizeof(ssize_t)), 0,
				       DEFCOLS * sizeof(int));
			}
			p = NULL;
		}
		t->list = xcalloc(coloff, sizeof(wchar_t *));
		t->len = xcalloc(coloff, sizeof(int));
		for (t->cols = coloff; --coloff >= 0;) {
			t->list[coloff] = cols[coloff];
			t->len[coloff] = wcs_width(cols[coloff]);
			if (t->len[coloff] > lens[coloff])
				lens[coloff] = t->len[coloff];
		}
	}

	for (t = tbl, cnt = 0; cnt < entries; ++cnt, ++t) {
		for (coloff = 0; coloff < t->cols - 1; ++coloff) {
			fputws(t->list[coloff], stdout);
			for (i = lens[coloff] - t->len[coloff] + 2; i > 0; i--)
				putwchar(' ');
		}
		if (coloff < t->cols) {
			fputws(t->list[coloff], stdout);
			putwchar('\n');
		}
	}
}

static void input(FILE *fp)
{
	static int maxentry = DEFNUM;
	int len, lineno = 1, reportedline = 0;
	wchar_t *p, buf[MAXLINELEN];

	if (!list)
		list = xcalloc(maxentry, sizeof(wchar_t *));
	while (fgetws(buf, MAXLINELEN, fp)) {
		for (p = buf; *p && iswspace(*p); ++p)
			;
		if (!*p)
			continue;
		if (!(p = wcschr(p, '\n')) && !feof(fp)) {
			if (reportedline < lineno) {
				warnx(_("line %d is too long, output will be truncated"),
					lineno);
				reportedline = lineno;
			}
			eval = EXIT_FAILURE;
			continue;
		}
		lineno++;
		if (!feof(fp))
			*p = '\0';
		len = wcs_width(buf);	/* len = p - buf; */
		if (maxlength < len)
			maxlength = len;
		if (entries == maxentry) {
			maxentry += DEFNUM;
			list = xrealloc(list, (u_int)maxentry * sizeof(wchar_t *));
		}
		list[entries++] = wcsdup(buf);
	}
}

#ifdef HAVE_WIDECHAR
static wchar_t *mbs_to_wcs(const char *s)
{
	ssize_t n;
	wchar_t *wcs;

	n = mbstowcs((wchar_t *)0, s, 0);
	if (n < 0)
		return NULL;
	wcs = malloc((n + 1) * sizeof(wchar_t));
	if (!wcs)
		return NULL;
	n = mbstowcs(wcs, s, n + 1);
	if (n < 0)
		return NULL;
	return wcs;
}
#endif

#ifndef HAVE_WIDECHAR
static char *mtsafe_strtok(char *str, const char *delim, char **ptr)
{
	if (str == NULL) {
		str = *ptr;
		if (str == NULL)
			return NULL;
	}
	str += strspn(str, delim);
	if (*str == '\0') {
		*ptr = NULL;
		return NULL;
	} else {
		char *token_end = strpbrk(str, delim);
		if (token_end) {
			*token_end = '\0';
			*ptr = token_end + 1;
		} else
			*ptr = NULL;
		return str;
	}
}
#endif
