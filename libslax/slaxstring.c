/*
 * $Id: slaxstring.c,v 1.1 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * This module contains functions to build and handle a set of string
 * segments in a linked list.  It is used by both the slax reader and
 * writer code to simplify string construction.
 *
 * Each slax_string_t contains a segment of the string and a pointer
 * to the next slax_string_t.  Very simple, but makes building the
 * XSLT constructs during bison-based parsing fairly easy.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>

/*
 * If the string has both types of quotes (single and double), then
 * we have some additional work on our future.  Return SSF_BOTHQS
 * to let the caller know.
 */
static int
slaxStringQFlags (slax_string_t *ssp)
{
    const char *cp;
    char quote = 0;		/* The quote we've already hit */
    int rc = 0;

    for (cp = ssp->ss_token; *cp; cp++) {
	if (*cp == '\'')
	    rc |= SSF_SINGLEQ;
	else if (*cp == '\"')
	    rc |= SSF_DOUBLEQ;
	else
	    continue;

	/* If we hit a different quote than we've seen, we've seen both */
	if (quote && *cp != quote)
	    return (SSF_SINGLEQ | SSF_DOUBLEQ | SSF_BOTHQS);

	quote = *cp;
    }

    return rc;
}

static int
slaxHex (unsigned char x)
{
    if ('0' <= x && x <= '9')
	return x - '0';
    if ('a' <= x && x <= 'f')
	return x - 'a' + 10;
    if ('A' <= x && x <= 'F')
	return x - 'A' + 10;
    return -1;
}    

#define SLAX_UTF_INVALID_NUMBER 0xfffd

 unsigned
 slaxUtfWord (const char *cp, int width);
 unsigned
slaxUtfWord (const char *cp, int width)
{
    unsigned val;
    int v1 = slaxHex(*cp++);
    int v2 = slaxHex(*cp++);
    int v3 = slaxHex(*cp++);
    int v4 = slaxHex(*cp++);

    if (v1 < 0 || v2 < 0 || v3 < 0 || v4 < 0)
	return SLAX_UTF_INVALID_NUMBER;

    val = (((((v1 << 4) | v2) << 4) | v3) << 4) | v4;

    if (width == SLAX_UTF_WIDTH6) {
	int v5 = slaxHex(*cp++);
	int v6 = slaxHex(*cp++);

	if (v5 < 0 || v6 < 0)
	    return SLAX_UTF_INVALID_NUMBER;

	val = (((val << 4) | v5) << 4) | v6;
    }

    return val;
}

/**
 * Create a string.  Slax strings allow sections of strings (typically
 * tokens returned by the lexer) to be chained together to built
 * longer strings (typically an XPath expression).
 *
 * The string is located in sdp->sd_buf[] between sdp->sd_start
 * and sdp->sd_cur.
 *
 * @param sdp main slax data structure
 * @param ttype token type for the string to be created
 * @return newly allocated string structure
 */
slax_string_t *
slaxStringCreate (slax_data_t *sdp, int ttype)
{
    int len, i, width;
    char *cp;
    const char *start;
    slax_string_t *ssp;

    if (ttype == L_EOS)		/* Don't bother for ";" */
	return NULL;

    len = sdp->sd_cur - sdp->sd_start;
    start = sdp->sd_buf + sdp->sd_start;

    if (ttype == T_QUOTED) {
	len -= 2;		/* Strip the quotes */
	start += 1;
    }

    ssp = xmlMalloc(sizeof(*ssp) + len + 1);
    if (ssp) {
	ssp->ss_token[len] = 0;
	ssp->ss_ttype = ttype;
	ssp->ss_next = ssp->ss_concat = NULL;

	if (ttype != T_QUOTED)
	    memcpy(ssp->ss_token, start, len);
	else {
	    cp = ssp->ss_token;

	    for (i = 0; i < len; i++) {
		unsigned char ch = start[i];

		if (ch == '\\') {
		    ch = start[++i];

		    switch (ch) {
		    case 'n':
			ch = '\n';
			break;

		    case 'r':
			ch = '\r';
			break;

		    case 't':
			ch = '\t';
			break;

		    case 'x':
			{
			    int v1 = slaxHex(start[++i]);
			    int v2 = slaxHex(start[++i]);

			    if (v1 >= 0 && v2 >= 0) {
				ch = (v1 << 4) + v2;
				if (ch > 0x7f) {
				    /*
				     * If the value is >0x7f, then we have
				     * a UTF-8 character sequence to generate.
				     */
				    *cp++ = 0xc0 | (ch >> 6);
				    ch = 0x80 | (ch & 0x3f);
				}
			    }
			}
			break;

		    case 'u':
			ch = start[++i];
			if (ch == '+' && len - i >= SLAX_UTF_WIDTH4) {
			    width = SLAX_UTF_WIDTH4;

			} else if (ch == '-' && len - i >= SLAX_UTF_WIDTH6) {
			    width = SLAX_UTF_WIDTH6;

			} else {
			    ch = 'u';
			    break;
			}

			unsigned word = slaxUtfWord(start + i + 1, width);
			i += width + 1;

			if (word <= 0x7f) {
			    ch = word;

			} else if (word <= 0x7ff) {
			    *cp++ = 0xc0 | ((word >> 6) & 0x1f);
			    ch = 0x80 | (word & 0x3f);

			} else if (word <= 0xffff) {
			    *cp++ = 0xe0 | (word >> 12);
			    *cp++ = 0x80 | ((word >> 6) & 0x3f);
			    ch = 0x80 | (word & 0x3f);

			} else {
			    *cp++ = 0xf0 | ((word >> 18) & 0x7);
			    *cp++ = 0x80 | ((word >> 12) & 0x3f);
			    *cp++ = 0x80 | ((word >> 6) & 0x3f);
			    ch = 0x80 | (word & 0x3f);
			}
			break;
		    }
		}

		*cp++ = ch;
	    }
	    *cp = '\0';
	}

	ssp->ss_flags = slaxStringQFlags(ssp);
    }

    return ssp;
}

/**
 * Return a string for a literal string constant.
 *
 * @param str literal string
 * @param ttype token type
 * @return newly allocated string structure
 */
slax_string_t *
slaxStringLiteral (const char *str, int ttype)
{
    slax_string_t *ssp;
    int len = strlen(str);

    ssp = xmlMalloc(sizeof(*ssp) + len);
    if (ssp) {
	memcpy(ssp->ss_token, str, len);
	ssp->ss_token[len] = 0;
	ssp->ss_ttype = ttype;
	ssp->ss_next = ssp->ss_concat = NULL;
	ssp->ss_flags = slaxStringQFlags(ssp);
    }

    return ssp;
}

/**
 * Link all strings above "start" (and below "top") into a single
 * string.
 *
 * @param sdp main slax data structure (UNUSED)
 * @param start stack location to start linking
 * @param top stack location to stop linking
 * @return linked list of strings (via ss_next)
 */
slax_string_t *
slaxStringLink (slax_data_t *sdp UNUSED, slax_string_t **start,
		slax_string_t **top)
{
    slax_string_t **sspp, *ssp, **next, *results = NULL;

    next = &results;

    for (sspp = start; sspp <= top; sspp++) {
	ssp = *sspp;
	if (ssp == NULL)
	    continue;

	*sspp = NULL;		/* Clear stack */

	*next = ssp;
	while (ssp->ss_next)	/* Skip to end of the list */
	    ssp = ssp->ss_next;
	next = &ssp->ss_next;
    }

    *next = NULL;
    return results;
}

/**
 * Calculate the length of the string consisting of the concatenation
 * of all the string segments hung off "start".
 *
 * @param start string (linked via ss_next) to determine length
 * @param flags indicate how string data is marshalled
 * @return number of bytes required to hold this string
 */
int
slaxStringLength (slax_string_t *start, unsigned flags)
{
    slax_string_t *ssp;
    int len = 0;
    char *cp;

    for (ssp = start; ssp != NULL; ssp = ssp->ss_next) {
	len += strlen(ssp->ss_token);

	if (ssp->ss_ttype != T_AXIS_NAME && ssp->ss_ttype != L_DCOLON)
	    len += 1;		/* One for space or NUL */

	if (ssp->ss_ttype == T_QUOTED) {
	    if (flags & SSF_QUOTES) {
		len += 2;
		for (cp = strchr(ssp->ss_token, '"');
			     cp; cp = strchr(cp + 1, '"'))
		    len += 1;	/* Needs a backslash */
	    }
	    if (flags & SSF_BRACES) {
		for (cp = ssp->ss_token; *cp; cp++)
		    if (*cp == '{' || *cp == '}')
			len += 1; /* Needs a double */
	    }
	}
    }

    return len + 1;
}

/*
 * Should the character be stripped of surrounding whitespace?
 */
static int
slaxStringNoSpace (int ch)
{
    if (ch == '@' || ch == '/'
	|| ch == '('  || ch == ')'
	|| ch == '['  || ch == ']')
	return TRUE;
    return FALSE;
}


/**
 * Build a single string out of the string segments hung off "start".
 *
 * @param buf memory buffer to hold built string
 * @param bufsiz number of bytes available in buffer
 * @param start first link in linked list
 * @param flags indicate how string data is marshalled
 * @return number of bytes used to hold this string
 */
int
slaxStringCopy (char *buf, int bufsiz, slax_string_t *start, unsigned flags)
{
    slax_string_t *ssp;
    int len = 0, slen;
    const char *str, *cp;
    char *bp = buf;
    int squote = 0; /* Single quote string flag */

    for (ssp = start; ssp != NULL; ssp = ssp->ss_next) {
	str = ssp->ss_token;
	slen = strlen(str);
	len += slen + 1;	/* One for space or NUL */

	if ((flags & SSF_QUOTES) && ssp->ss_ttype == T_QUOTED)
	    len += 2;		/* Two quotes */

	if (len > bufsiz)
	    break;		/* Tragedy: not enough room */

	if (bp > &buf[1] && bp[-1] == ' ') {
	    /*
	     * Decide if we can trim off the last trailing space.  You
	     * would think this was an easy one, but it's not.
	     */
	    int trim = FALSE;

	    if (bp[-2] == '_' || *str == '_')
		trim = FALSE;
	    else if (slaxStringNoSpace(bp[-2]) || slaxStringNoSpace(*str))
		trim = TRUE;	/* foo/goo[@zoo] */

	    else if (*str == ',')
		trim = TRUE;	/* foo(1, 2, 3) */

	    else if (isdigit(bp[-2]) && *str == '.'
		     && ssp->ss_ttype != L_DOTDOTDOT)
		trim = TRUE;	/* 1.0 (looking at the '.') */

	    else if (bp[-2] == '.' && bp[-3] != '.' && isdigit(*str))
		trim = TRUE;	/* 1.0 (looking at the '0') */

	    else if (bp == &buf[2] && buf[0] == '-')
		trim = TRUE;	/* Negative number */

	    /*
	     * We only want to trim closers if the next thing is a closer
	     * or a slash, so that we handle "foo[goo]/zoo" correctly
	     */
	    if ((bp[-2] == ')' || bp[-2] == ']')
		    && !(*str == ')' || *str == ']' || *str == '/'))
		trim = FALSE;

	    if (trim) {
		bp -= 1;
		len -= 1;
	    }
	}

	if ((flags & SSF_QUOTES) && ssp->ss_ttype == T_QUOTED) {
	    /*
	     * If it's a quoted string, we surround it by quotes, but
	     * we also have to handle embedded quotes.
	     */
	    const char *dqp = strchr(str, '"');
	    const char *sqp = strchr(str, '\'');

	    if (dqp && sqp) {
		/* Bad news */
		slaxTrace("bad news");
		*bp++ = '"';
	    } else if (dqp) {
		/* double quoted string to be surrounded by single quotes */
		*bp++ = '\'';
		squote = 1;
	    } else
		*bp++ = '"';

	    slen = strlen(str);
	    memcpy(bp, str, slen);
	    bp += slen;
	    if (squote) {
		/* double quoted string to be surrounded by single quotes */
		*bp++ = '\'';
		squote = 0;
	    } else
		*bp++ = '"';

	} else if ((flags & SSF_BRACES) && ssp->ss_ttype == T_QUOTED) {
	    for (cp = str; *cp; cp++) {
		*bp++ = *cp;
		if (*cp == '{' || *cp == '}') {
		    /*
		     * XSLT uses double open and close braces to escape
		     * them inside attributes to allow for attribute
		     * value templates (AVTs).  SLAX does AVTs with
		     * normal expression syntax, so if we see a brace,
		     * we want it to stay a brace.  We make this happen
		     * by doubling the character.
		     */
		    *bp++ = *cp;
		    len += 1;
		}
	    }

	} else {
	    memcpy(bp, str, slen);
	    bp += slen;
	}

	/* We want axis::elt with no spaces */
	if (ssp->ss_ttype == T_AXIS_NAME || ssp->ss_ttype == L_DCOLON)
	    len -= 1;
	else *bp++ = ' ';
    }

    if (len <= 0)
	return 0;

    if (len < bufsiz) {
	buf[--len] = '\0';
	return len;
    } else {
	buf[--bufsiz] = '\0';
	return bufsiz;
    }
}

/*
 * Fuse a variable number of strings together, returning the results.
 */
slax_string_t *
slaxStringFuse (slax_data_t *sdp UNUSED, int ttype, slax_string_t **sspp)
{
    slax_string_t *results, *start = *sspp;
    int len;

    if (start && start->ss_next == NULL && start->ss_ttype != T_QUOTED) {
	/*
	 * If we're only talking about one string and it doesn't
	 * need quoting, return it directly.  We need to clear
	 * out sspp to that the item isn't freed when the stack
	 * is cleared.
	 */
	*sspp = NULL;
	return start;
    }

    if (start && start->ss_next == NULL && start->ss_ttype == T_QUOTED)
	ttype = T_QUOTED;

    len = slaxStringLength(start, SSF_QUOTES);
    if (len == 0)
	return NULL;

    results = xmlMalloc(sizeof(*results) + len);
    if (results == NULL)
	return NULL;

    slaxStringCopy(results->ss_token, len, start, SSF_QUOTES);
    results->ss_ttype = ttype;
    results->ss_next = results->ss_concat = NULL;
    results->ss_flags = (ttype == T_QUOTED) ? slaxStringQFlags(results) : 0;

    if (slaxDebug)
	slaxTrace("slaxStringFuse: '%s'", results->ss_token);
    return results;
}

/**
 * Build a string from the string segment hung off "value" and return it
 *
 * @param value start of linked list of string segments
 * @param flags indicate how string data is marshalled
 * @return newly allocated character string
 */
char *
slaxStringAsChar (slax_string_t *value, unsigned flags)
{
    int len;
    char *buf;

    len = slaxStringLength(value, flags);
    if (len == 0)
	return NULL;

    buf = xmlMalloc(len);
    if (buf == NULL)
	return NULL;

    slaxStringCopy(buf, len, value, flags);

    if (slaxDebug)
	slaxTrace("slaxStringAsChar: '%s'", buf);
    return buf;
}

/**
 * Return a set of xpath values as a concat() invocation.  Strings are
 * linked in two dimensions.  The "ss_next" field links tokens inside
 * an XPath expression, and the "ss_concat" field links XPath expressions
 * inside SLAX concatenation operations.
 * For example: var $x = a1/a2/a3 _ b1/b2/b3;
 * The "a1"'s ss_next would be "/", but its ss_concat link would be "b1".
 *
 * @param value start of linked list of string segments
 * @param flags indicate how string data is marshalled
 * @return newly allocated character string
 */
char *
slaxStringAsConcat (slax_string_t *value, unsigned flags)
{
    static const char s_prepend[] = "concat(";
    static const char s_middle[] = ", ";
    static const char s_append[] = ")";
    char *buf, *bp;
    int len = 0, blen, slen;
    slax_string_t *ssp;

    /*
     * If there's no concatenation, just return the XPath expression
     */
    if (value->ss_concat == NULL)
	return slaxStringAsChar(value, flags);

    /*
     * First we work out the size of the buffer
     */
    for (ssp = value; ssp; ssp = ssp->ss_concat) {
	if (len != 0)
	    len += sizeof(s_middle) - 1;
	len += slaxStringLength(ssp, flags);
    }

    len += sizeof(s_prepend) + sizeof(s_append) - 1;

    /* Allocate the buffer */
    buf = bp = xmlMalloc(len);
    blen = len;			/* Save buffer length */
    if (buf == NULL) {
	slaxTrace("slaxStringAsConcat:: out of memory");
	return NULL;
    }

    /* Fill it in: build the xpath concat invocation */
    slen = sizeof(s_prepend) - 1;
    memcpy(bp, s_prepend, slen);
    bp += slen;

    for (ssp = value; ssp; ssp = ssp->ss_concat) {
	if (ssp != value) {
	    slen = sizeof(s_middle) - 1;
	    memcpy(bp, s_middle, slen);
	    bp += slen;
	}

	len = slaxStringCopy(bp, blen, ssp, flags);
	blen -= len;
	bp += len;
    }

    slen = sizeof(s_append) - 1;
    memcpy(bp, s_append, slen);
    bp += slen;

    *bp = 0;
    return buf;
}

/**
 * Return a set of xpath values as an attribute value template.  The SLAX
 * concatenation operations used the "{a1}{b2}" braces templating scheme
 * for concatenation.
 * For example: <a b = one _ "-" _ two>;  would be <a b="{one}-{two}"/>.
 * Attribute value templates are used for non-xsl elements.
 *
 * @param value start of linked list of string segments
 * @param flags indicate how string data is marshalled
 * @return newly allocated character string
 */
char *
slaxStringAsValueTemplate (slax_string_t *value, unsigned flags)
{
    char *buf, *bp;
    int len = 0, blen;
    unsigned xflags;
    slax_string_t *ssp;

    flags |= SSF_BRACES;	/* Just in case the caller forgot */

    /*
     * If there's no concatenation, just return the XPath expression
     */
    if (slaxStringIsSimple(value, T_QUOTED))
	return slaxStringAsChar(value, flags);

    /*
     * First we work out the size of the buffer
     */
    for (ssp = value; ssp; ssp = ssp->ss_concat) {
	xflags = flags;
	if (ssp->ss_ttype != T_QUOTED) {
	    len += 2; /* Room for braces */
	    xflags |= SSF_QUOTES;
	}

	len += slaxStringLength(ssp, xflags);
    }

    /* Allocate the buffer */
    buf = bp = xmlMalloc(len);
    blen = len;			/* Save buffer length */
    if (buf == NULL) {
	slaxTrace("slaxStringAsConcat:: out of memory");
	return NULL;
    }

    /* Fill it in: build the attribute value template */
    for (ssp = value; ssp; ssp = ssp->ss_concat) {
	xflags = flags;
	if (ssp->ss_ttype != T_QUOTED) {
	    *bp++ = '{';
	    blen -= 1;
	    xflags |= SSF_QUOTES;
	}

	len = slaxStringCopy(bp, blen, ssp, xflags);
	blen -= len;
	bp += len;

	if (ssp->ss_ttype != T_QUOTED) {
	    *bp++ = '}';
	    blen -= 1;
	}
    }

    *bp = 0;
    return buf;
}

/*
 * Free a set of string segments
 */
static void
slaxStringFreeConcat (slax_string_t *ssp)
{
    slax_string_t *concat;

    for ( ; ssp; ssp = concat) {
	concat = ssp->ss_concat;
	ssp->ss_concat = NULL;
	slaxStringFree(ssp);
    }
}

/**
 * Free a set of string segments.  Both ss_concat and ss_next links are
 * followed and all contents are freed.
 * @param ssp string links to be freed.
 */
void
slaxStringFree (slax_string_t *ssp)
{
    slax_string_t *next, *concat;

    for ( ; ssp; ssp = next) {
	next = ssp->ss_next;
	concat = ssp->ss_concat;
	ssp->ss_next = ssp->ss_concat = NULL;

	if (concat)
	    slaxStringFreeConcat(concat);

	ssp->ss_ttype = 0;
	ssp->ss_flags = 0;
	xmlFree(ssp);
    }
}

static int
slaxStringAddTailHelper (slax_string_t ***tailp,
			 const char *buf, size_t bufsiz, int ttype)
{
    slax_string_t *ssp;

    /*
     * Allocate and fill in a slax_string_t; we don't need a +1
     * for bufsiz since the ss_token already has it.
     */
    ssp = xmlMalloc(sizeof(*ssp) + bufsiz);
    if (ssp == NULL)
	return TRUE;

    memcpy(ssp->ss_token, buf, bufsiz);
    ssp->ss_token[bufsiz] = '\0';
    ssp->ss_next = ssp->ss_concat = NULL;
    ssp->ss_ttype = ttype;
    ssp->ss_flags = slaxStringQFlags(ssp);

    **tailp = ssp;
    *tailp = &ssp->ss_next;

    return FALSE;
}

/**
 * Add a new slax string segment to the linked list
 * @param tailp pointer to a variable that points to the end of the linked list
 * @param first pointer to first string in linked list
 * @param buf the string to add
 * @param bufsiz length of the string to add
 * @param ttype token type
 * @return TRUE if xmlMalloc fails, FALSE otherwise
 */
int
slaxStringAddTail (slax_string_t ***tailp, slax_string_t *first,
		   const char *buf, size_t bufsiz, int ttype)
{
    if (tailp == NULL || buf == NULL)
	return FALSE;		/* Benign no-op */

    /*
     * SLAX uses "_" as the concatenation operator, so if we're not
     * the first chunk of string, we need to insert an underscore.
     */
    if (first != NULL) {
	if (slaxStringAddTailHelper(tailp, "_", 1, L_UNDERSCORE))
	    return TRUE;
    }

    return slaxStringAddTailHelper(tailp, buf, bufsiz, ttype);
}
