
/*
 * $Id: HttpHeaderTools.c,v 1.25.2.8 2000/02/09 23:29:51 wessels Exp $
 *
 * DEBUG: section 66    HTTP Header Tools
 * AUTHOR: Alex Rousskov
 *
 * SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by the
 *  National Science Foundation.  Squid is Copyrighted (C) 1998 by
 *  the Regents of the University of California.  Please see the
 *  COPYRIGHT file for full details.  Squid incorporates software
 *  developed and/or copyrighted by other sources.  Please see the
 *  CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "squid.h"

static int httpHeaderStrCmp(const char *h1, const char *h2, int len);
static void httpHeaderPutStrvf(HttpHeader * hdr, http_hdr_type id, const char *fmt, va_list vargs);


HttpHeaderFieldInfo *
httpHeaderBuildFieldsInfo(const HttpHeaderFieldAttrs * attrs, int count)
{
    int i;
    HttpHeaderFieldInfo *table = NULL;
    assert(attrs && count);

    /* allocate space */
    table = xcalloc(count, sizeof(HttpHeaderFieldInfo));

    for (i = 0; i < count; ++i) {
	const int id = attrs[i].id;
	HttpHeaderFieldInfo *info = table + id;
	/* sanity checks */
	assert(id >= 0 && id < count);
	assert(attrs[i].name);
	assert(info->id == 0 && info->type == 0);	/* was not set before */
	/* copy and init fields */
	info->id = id;
	info->type = attrs[i].type;
	stringInit(&info->name, attrs[i].name);
	assert(strLen(info->name));
	/* init stats */
	memset(&info->stat, 0, sizeof(info->stat));
    }
    return table;
}

void
httpHeaderDestroyFieldsInfo(HttpHeaderFieldInfo * table, int count)
{
    int i;
    for (i = 0; i < count; ++i)
	stringClean(&table[i].name);
    xfree(table);
}

void
httpHeaderMaskInit(HttpHeaderMask * mask, int value)
{
    memset(mask, value, sizeof(*mask));
}

/* calculates a bit mask of a given array; does not reset mask! */
void
httpHeaderCalcMask(HttpHeaderMask * mask, const int *enums, int count)
{
    int i;
    assert(mask && enums);
    assert(count < sizeof(*mask) * 8);	/* check for overflow */

    for (i = 0; i < count; ++i) {
	assert(!CBIT_TEST(*mask, enums[i]));	/* check for duplicates */
	CBIT_SET(*mask, enums[i]);
    }
}

/* same as httpHeaderPutStr, but formats the string using snprintf first */
void
#if STDC_HEADERS
httpHeaderPutStrf(HttpHeader * hdr, http_hdr_type id, const char *fmt,...)
#else
httpHeaderPutStrf(va_alist)
     va_dcl
#endif
{
#if STDC_HEADERS
    va_list args;
    va_start(args, fmt);
#else
    va_list args;
    HttpHeader *hdr = NULL;
    http_hdr_type id = HDR_ENUM_END;
    const char *fmt = NULL;
    va_start(args);
    hdr = va_arg(args, HttpHeader *);
    id = va_arg(args, http_hdr_type);
    fmt = va_arg(args, char *);
#endif
    httpHeaderPutStrvf(hdr, id, fmt, args);
    va_end(args);
}

/* used by httpHeaderPutStrf */
static void
httpHeaderPutStrvf(HttpHeader * hdr, http_hdr_type id, const char *fmt, va_list vargs)
{
    MemBuf mb;
    memBufDefInit(&mb);
    memBufVPrintf(&mb, fmt, vargs);
    httpHeaderPutStr(hdr, id, mb.buf);
    memBufClean(&mb);
}


/* wrapper arrounf PutContRange */
void
httpHeaderAddContRange(HttpHeader * hdr, HttpHdrRangeSpec spec, ssize_t ent_len)
{
    HttpHdrContRange *cr = httpHdrContRangeCreate();
    assert(hdr && ent_len >= 0);
    httpHdrContRangeSet(cr, spec, ent_len);
    httpHeaderPutContRange(hdr, cr);
    httpHdrContRangeDestroy(cr);
}


/*
 * return true if a given directive is found in at least one of
 * the "connection" header-fields note: if HDR_PROXY_CONNECTION is
 * present we ignore HDR_CONNECTION.
 */
int
httpHeaderHasConnDir(const HttpHeader * hdr, const char *directive)
{
    String list;
    http_hdr_type ht;
    int res;
    /* what type of header do we have? */
    if (httpHeaderHas(hdr, HDR_PROXY_CONNECTION))
	ht = HDR_PROXY_CONNECTION;
    else if (httpHeaderHas(hdr, HDR_CONNECTION))
	ht = HDR_CONNECTION;
    else
	return 0;

    list = httpHeaderGetList(hdr, ht);
    res = strListIsMember(&list, directive, ',');
    stringClean(&list);
    return res;
}

/* returns true iff "m" is a member of the list */
int
strListIsMember(const String * list, const char *m, char del)
{
    const char *pos = NULL;
    const char *item;
    int ilen = 0;
    int mlen;
    assert(list && m);
    mlen = strlen(m);
    while (strListGetItem(list, del, &item, &ilen, &pos)) {
	if (mlen == ilen && !strncasecmp(item, m, ilen))
	    return 1;
    }
    return 0;
}

/* returns true iff "s" is a substring of a member of the list */
int
strListIsSubstr(const String * list, const char *s, char del)
{
    assert(list && del);
    return strStr(*list, s) != 0;

    /*
     * Note: the original code with a loop is broken because it uses strstr()
     * instead of strnstr(). If 's' contains a 'del', strListIsSubstr() may
     * return true when it should not. If 's' does not contain a 'del', the
     * implementaion is equavalent to strstr()! Thus, we replace the loop with
     * strstr() above until strnstr() is available.
     */

#ifdef BROKEN_CODE
    const char *pos = NULL;
    const char *item;
    assert(list && s);
    while (strListGetItem(list, del, &item, NULL, &pos)) {
	if (strstr(item, s))
	    return 1;
    }
    return 0;
#endif
}

/* appends an item to the list */
void
strListAdd(String * str, const char *item, char del)
{
    assert(str && item);
    if (strLen(*str)) {
	char buf[3];
	buf[0] = del;
	buf[1] = ' ';
	buf[2] = '\0';
	stringAppend(str, buf, 2);
    }
    stringAppend(str, item, strlen(item));
}

/*
 * iterates through a 0-terminated string of items separated by 'del's.
 * white space around 'del' is considered to be a part of 'del'
 * like strtok, but preserves the source, and can iterate several strings at once
 *
 * returns true if next item is found.
 * init pos with NULL to start iteration.
 */
int
strListGetItem(const String * str, char del, const char **item, int *ilen, const char **pos)
{
    size_t len;
    assert(str && item && pos);
    if (*pos) {
	if (!**pos)		/* end of string */
	    return 0;
	else
	    (*pos)++;
    } else {
	*pos = strBuf(*str);
	if (!*pos)
	    return 0;
    }

    /* skip leading ws (ltrim) */
    *pos += xcountws(*pos);
    *item = *pos;		/* remember item's start */
    /* find next delimiter */
    *pos = strchr(*item, del);
    if (!*pos)			/* last item */
	*pos = *item + strlen(*item);
    len = *pos - *item;		/* *pos points to del or '\0' */
    /* rtrim */
    while (len > 0 && xisspace((*item)[len - 1]))
	len--;
    if (ilen)
	*ilen = len;
    return len > 0;
}

/* handy to printf prefixes of potentially very long buffers */
const char *
getStringPrefix(const char *str, const char *end)
{
#define SHORT_PREFIX_SIZE 512
    LOCAL_ARRAY(char, buf, SHORT_PREFIX_SIZE);
    const int sz = 1 + (end ? end - str : strlen(str));
    xstrncpy(buf, str, (sz > SHORT_PREFIX_SIZE) ? SHORT_PREFIX_SIZE : sz);
    return buf;
}

/*
 * parses an int field, complains if soemthing went wrong, returns true on
 * success
 */
int
httpHeaderParseInt(const char *start, int *value)
{
    assert(value);
    *value = atoi(start);
    if (!*value && !xisdigit(*start)) {
	debug(66, 2) ("failed to parse an int header field near '%s'\n", start);
	return 0;
    }
    return 1;
}

int
httpHeaderParseSize(const char *start, ssize_t * value)
{
    int v;
    const int res = httpHeaderParseInt(start, &v);
    assert(value);
    *value = res ? v : 0;
    return res;
}


/*
 * parses a given string then packs compiled headers and compares the result
 * with the original, reports discrepancies
 */
void
httpHeaderTestParser(const char *hstr)
{
    static int bug_count = 0;
    int hstr_len;
    int parse_success;
    HttpHeader hdr;
    int pos;
    Packer p;
    MemBuf mb;
    assert(hstr);
    /* skip start line if any */
    if (!strncasecmp(hstr, "HTTP/", 5)) {
	const char *p = strchr(hstr, '\n');
	if (p)
	    hstr = p + 1;
    }
    /* skip invalid first line if any */
    if (xisspace(*hstr)) {
	const char *p = strchr(hstr, '\n');
	if (p)
	    hstr = p + 1;
    }
    hstr_len = strlen(hstr);
    /* skip terminator if any */
    if (strstr(hstr, "\n\r\n"))
	hstr_len -= 2;
    else if (strstr(hstr, "\n\n"))
	hstr_len -= 1;
    httpHeaderInit(&hdr, hoReply);
    /* debugLevels[55] = 8; */
    parse_success = httpHeaderParse(&hdr, hstr, hstr + hstr_len);
    /* debugLevels[55] = 2; */
    if (!parse_success) {
	debug(66, 2) ("TEST (%d): failed to parsed a header: {\n%s}\n", bug_count, hstr);
	return;
    }
    /* we think that we parsed it, veryfy */
    memBufDefInit(&mb);
    packerToMemInit(&p, &mb);
    httpHeaderPackInto(&hdr, &p);
    if ((pos = abs(httpHeaderStrCmp(hstr, mb.buf, hstr_len)))) {
	bug_count++;
	debug(66, 2) ("TEST (%d): hdr parsing bug (pos: %d near '%s'): expected: {\n%s} got: {\n%s}\n",
	    bug_count, pos, hstr + pos, hstr, mb.buf);
    }
    httpHeaderClean(&hdr);
    packerClean(&p);
    memBufClean(&mb);
}


/* like strncasecmp but ignores ws characters */
static int
httpHeaderStrCmp(const char *h1, const char *h2, int len)
{
    int len1 = 0;
    int len2 = 0;
    assert(h1 && h2);
    /* fast check first */
    if (!strncasecmp(h1, h2, len))
	return 0;
    while (1) {
	const char c1 = xtoupper(h1[len1 += xcountws(h1 + len1)]);
	const char c2 = xtoupper(h2[len2 += xcountws(h2 + len2)]);
	if (c1 < c2)
	    return -len1;
	if (c1 > c2)
	    return +len1;
	if (!c1 && !c2)
	    return 0;
	if (c1)
	    len1++;
	if (c2)
	    len2++;
    }
    /* NOTREACHED */
    return 0;
}
