/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "config.h"

#if 0
/*
 * The readline header files contain function prototypes that
 * won't compile with our warning level.  We take the "lesser
 * of two evils" approach and fake a prototype here instead
 * of turning down our warning levels.
 */
#include <readline/readline.h>
#include <readline/history.h>
#else /* 0 */
extern char *readline (const char *);
extern void add_history (const char *);
#endif /* 0 */

/* Callback functions for input and output */
static slaxInputCallback_t slaxInputCallback;
static slaxOutputCallback_t slaxOutputCallback;
static xmlOutputWriteCallback slaxWriteCallback;

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
char *
slaxInput (const char *prompt, unsigned flags)
{
    char *res;

    /* slaxLog("slaxInput: -> [%s]", prompt); */
    res = slaxInputCallback ? slaxInputCallback(prompt, flags) : NULL;
    /* slaxLog("slaxInput: <- [%s]", res ?: "null"); */

    return res;
}

/**
 * Use the callback to output a string
 * @fmt printf-style format string
 */
void
slaxOutput (const char *fmt, ...)
{
    if (slaxOutputCallback) {
	char buf[BUFSIZ];
	va_list vap;

	va_start(vap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vap);
	va_end(vap);

	/* slaxLog("slaxOutput: [%s]", buf); */
	slaxOutputCallback("%s\n", buf);
    }
}

/**
 * Output a node
 */
void
slaxOutputNode (xmlNodePtr node)
{
    xmlSaveCtxtPtr handle;

    handle = xmlSaveToIO(slaxWriteCallback, NULL, NULL, NULL,
		 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle) {
        xmlSaveTree(handle, node);
        xmlSaveFlush(handle);
	slaxWriteCallback(NULL, "\n", 1);
	xmlSaveClose(handle);
    }
}

/**
 * Print the given nodeset. First we print the nodeset in a temp file.
 * Then read that file and send the the line to mgd one by one.
 */
void
slaxOutputNodeset (xmlNodeSetPtr nodeset)
{
    int i;

    for (i = 0; i < nodeset->nodeNr; i++)
	slaxOutputNode(nodeset->nodeTab[i]);
}

/*
 * Register debugger
 */
void
slaxIoRegister (slaxInputCallback_t input_callback,
		slaxOutputCallback_t output_callback,
		xmlOutputWriteCallback raw_write)
{
    slaxInputCallback = input_callback;
    slaxOutputCallback = output_callback;
    slaxWriteCallback = raw_write;
}

static char *
slaxIoStdioInputCallback (const char *prompt, unsigned flags UNUSED)
{
#ifdef HAVE_READLINE
    char *cp, *res;

    /*
     * readline() will return a malloc'd buffer but we need to
     * swap it for memory that's acquired via xmlMalloc().
     */
    cp = readline(prompt);
    if (cp == NULL)
	return NULL;

    /* Add the command to the shell history (if it's not blank) */
    if ((flags & SIF_HISTORY) && *cp)
	add_history(cp);

    res = (char *) xmlStrdup((xmlChar *) cp);
    free(cp);
    return res;

    
#else /* HAVE_READLINE */
    char buf[BUFSIZ];
    int len;

    fputs(prompt, stderr);
    fflush(stderr);

    buf[0] = '\0';
    if (fgets(buf, sizeof(buf), stdin) == NULL)
	return NULL;

    len = strlen(buf);
    if (len > 1 && buf[len - 1] == '\n')
	buf[len - 1] = '\0';

    return (char *) xmlStrdup((xmlChar *) buf);
#endif /* HAVE_READLINE */
}

static void
slaxIoStdioOutputCallback (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    vfprintf(stderr, fmt, vap);
    fflush(stderr);
}

static int
slaxIoStdioRawwriteCallback (void *opaque UNUSED, const char *buf, int len)
{
    return write(fileno(stderr), buf, len);
}

void
slaxIoUseStdio (void)
{
    slaxIoRegister(slaxIoStdioInputCallback, slaxIoStdioOutputCallback,
		   slaxIoStdioRawwriteCallback);
}

static void
slaxProcTrace (void *vfp, xmlNodePtr nodep, const char *fmt, ...)
{
    FILE *fp = vfp;
    va_list vap;

    va_start(vap, fmt);

#if !defined(NO_TRACE_CLOCK)
    {
	struct timeval cur_time;
	char *time_buffer;
	
	gettimeofday(&cur_time, NULL);
	time_buffer = ctime(&cur_time.tv_sec);

	fprintf(fp, "%.15s: ", time_buffer + 4);  /* "Mmm dd hh:mm:ss" */
    }
#endif

    if (nodep) {
	xmlSaveCtxt *handle;

	fprintf(fp, "XML Content (%d)\n", nodep->type);
	fflush(fp);
	handle = xmlSaveToFd(fileno(fp), NULL,
			     XML_SAVE_FORMAT | XML_SAVE_NO_DECL);
	if (handle) {
	    xmlSaveTree(handle, nodep);
	    xmlSaveFlush(handle);
	    xmlSaveClose(handle);
	}

    } else {
	vfprintf(fp, fmt, vap);
    }

    fprintf(fp, "\n");
    fflush(fp);
    va_end(vap);
}

void
slaxTraceToFile (FILE *fp)
{
    slaxTraceEnable(slaxProcTrace, fp);
}

/**
 * Simple trace function that tosses messages to stderr if slaxDebug
 * has been set to non-zero.
 *
 * @param fmt format string plus variadic arguments
 */
void
slaxLog (const char *fmt, ...)
{
    va_list vap;

    if (!slaxDebug)
	return;

    va_start(vap, fmt);

    vfprintf(stderr, fmt, vap);
    fprintf(stderr, "\n");
    fflush(stderr);

    va_end(vap);
}

/**
 * Dump a formatted version of the XSL tree to a file
 *
 * @param fd file descriptor open for output
 * @param docp document pointer
 * @param partial Should we emit partial (snippet) output?
 */
void
slaxDumpToFd (int fd, xmlDocPtr docp, int partial)
{
    xmlSaveCtxtPtr handle;

    handle = xmlSaveToFd(fd, "UTF-8", XML_SAVE_FORMAT);

    if (!partial)
	xmlSaveDoc(handle, docp);
    else {
	xmlNodePtr nodep = xmlDocGetRootElement(docp);
	if (nodep)
	    nodep = nodep->children;

	for ( ; nodep; nodep = nodep->next) {
	    if (nodep->type == XML_ELEMENT_NODE) {
		xmlSaveTree(handle, nodep);
		xmlSaveFlush(handle);
		write(fd, "\n", 1);
	    }
	}
    }

    xmlSaveClose(handle);
}

/*
 * Dump a formatted version of the XSL tree to stdout
 */
void
slaxDump (xmlDocPtr docp)
{
    slaxDumpToFd(1, docp, 0);
}