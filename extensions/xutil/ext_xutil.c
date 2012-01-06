/*
 * $Id$
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.  See ../Copyright for additional information.
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>

#include <sys/queue.h>

#include <libxml/xpathInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>

#include "config.h"

#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxio.h>
#include <libslax/xmlsoft.h>
#include <libslax/slaxutil.h>

#define XML_FULL_NS "http://xml.libslax.org/xml"

static void
ext_xutil_string_to_xml (xmlXPathParserContext *ctxt, int nargs)
{
    xsltTransformContextPtr tctxt;
    xmlXPathObjectPtr ret = NULL;
    xmlDocPtr xmlp = NULL;
    xmlDocPtr container = NULL;
    xmlNodePtr childp;
    xmlChar *strstack[nargs];	/* Stack for strings */
    int ndx;
    int bufsiz;
    char *buf;

    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL)
	goto bail;

    bzero(strstack, sizeof(strstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	strstack[ndx] = xmlXPathPopString(ctxt);
    }

    for (bufsiz = 0, ndx = 0; ndx < nargs; ndx++)
	if (strstack[ndx])
	    bufsiz += xmlStrlen(strstack[ndx]);

    buf = alloca(bufsiz + 1);
    buf[bufsiz] = '\0';
    for (bufsiz = 0, ndx = 0; ndx < nargs; ndx++) {
	if (strstack[ndx]) {
	    int len = xmlStrlen(strstack[ndx]);
	    memcpy(buf + bufsiz, strstack[ndx], len);
	    bufsiz += len;
	}
    }

    /* buf now has the complete string */
    xmlp = xmlReadMemory(buf, bufsiz, "raw_data", NULL, XML_PARSE_NOENT);
    if (xmlp == NULL)
	goto bail;

    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto bail;

    /* Fake an RVT to hold the output of the template */
    container = xsltCreateRVT(tctxt);
    if (container == NULL)
	goto bail;

    xsltRegisterLocalRVT(tctxt, container);

    childp = xmlDocGetRootElement(xmlp);
    if (childp) {
	xmlNodePtr newp = xmlDocCopyNode(childp, container, 1);
	if (newp) {
	    xmlAddChild((xmlNodePtr) container, newp);
	    xmlXPathNodeSetAdd(ret->nodesetval, newp);
	}
    }

bail:
    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));

    if (xmlp)
	xmlFreeDoc(xmlp);

    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	if (strstack[ndx])
	    xmlFree(strstack[ndx]);
    }
}

static int
ext_xutil_rawwrite_callback (void *opaque, const char *buf, int len)
{
    slax_data_list_t *listp = opaque;

    if (listp == NULL)
	return 0;

    slaxDataListAddLen(listp, buf, len);

    return len;
}

static void
ext_xutil_xml_to_string (xmlXPathParserContext *ctxt, int nargs)
{
    xmlSaveCtxtPtr handle;
    slax_data_list_t list;
    slax_data_node_t *dnp;
    xmlXPathObjectPtr xop;
    xmlXPathObjectPtr objstack[nargs];	/* Stack for objects */
    int ndx;
    char *buf;
    int bufsiz;
    int hit = 0;

    slaxDataListInit(&list);

    bzero(objstack, sizeof(objstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	objstack[ndx] = valuePop(ctxt);
	if (objstack[ndx])
	    hit += 1;
    }

    /* If the args are empty, let's get out now */
    if (hit == 0) {
	xmlXPathReturnEmptyString(ctxt);
	return;
    }

    /* We make a custom IO handler to save content into a list of strings */
    handle = xmlSaveToIO(ext_xutil_rawwrite_callback, NULL, &list, NULL,
                 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    for (ndx = 0; ndx < nargs; ndx++) {
	xop = objstack[ndx];
	if (xop == NULL)
	    continue;

	if (xop->nodesetval) {
	    xmlNodeSetPtr tab = xop->nodesetval;
	    int i;
	    for (i = 0; i < tab->nodeNr; i++) {
		xmlNodePtr node = tab->nodeTab[i];

		xmlSaveTree(handle, node);
		xmlSaveFlush(handle);
	    }
	}
    }

    xmlSaveClose(handle);	/* This frees is also */

    /* Now we turn the saved data from a linked list into a single string */
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	bufsiz += dnp->dn_len;
    }

    /* If the objects are empty, let's get out now */
    if (bufsiz == 0) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    buf = xmlMalloc(bufsiz + 1);
    if (buf == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }
	
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	memcpy(buf + bufsiz, dnp->dn_data, dnp->dn_len);
	bufsiz += dnp->dn_len;
    }

    valuePush(ctxt, xmlXPathWrapCString(buf));

 bail:
    slaxDataListClean(&list);

    for (ndx = 0; ndx < nargs; ndx++)
	xmlXPathFreeObject(objstack[ndx]);
}

static void
ext_xutil_json_to_string (xmlXPathParserContext *ctxt, int nargs)
{
    xsltTransformContextPtr tctxt;
    xmlXPathObjectPtr ret = NULL;
    xmlDocPtr xmlp = NULL;
    xmlDocPtr container = NULL;
    xmlNodePtr childp;
    xmlChar *strstack[nargs];	/* Stack for strings */
    int ndx;
    int bufsiz;
    char *buf;

    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL)
	goto bail;

    bzero(strstack, sizeof(strstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	strstack[ndx] = xmlXPathPopString(ctxt);
    }

    for (bufsiz = 0, ndx = 0; ndx < nargs; ndx++)
	if (strstack[ndx])
	    bufsiz += xmlStrlen(strstack[ndx]);

    buf = alloca(bufsiz + 1);
    buf[bufsiz] = '\0';
    for (bufsiz = 0, ndx = 0; ndx < nargs; ndx++) {
	if (strstack[ndx]) {
	    int len = xmlStrlen(strstack[ndx]);
	    memcpy(buf + bufsiz, strstack[ndx], len);
	    bufsiz += len;
	}
    }

    /* buf now has the complete string */
    xmlp = xmlReadMemory(buf, bufsiz, "raw_data", NULL, XML_PARSE_NOENT);
    if (xmlp == NULL)
	goto bail;

    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto bail;

    /* Fake an RVT to hold the output of the template */
    container = xsltCreateRVT(tctxt);
    if (container == NULL)
	goto bail;

    xsltRegisterLocalRVT(tctxt, container);

    childp = xmlDocGetRootElement(xmlp);
    if (childp) {
	xmlNodePtr newp = xmlDocCopyNode(childp, container, 1);
	if (newp) {
	    xmlAddChild((xmlNodePtr) container, newp);
	    xmlXPathNodeSetAdd(ret->nodesetval, newp);
	}
    }

bail:
    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));

    if (xmlp)
	xmlFreeDoc(xmlp);

    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	if (strstack[ndx])
	    xmlFree(strstack[ndx]);
    }
}

static void
ext_xutil_string_to_json (xmlXPathParserContext *ctxt, int nargs)
{
    xmlSaveCtxtPtr handle;
    slax_data_list_t list;
    slax_data_node_t *dnp;
    xmlXPathObjectPtr xop;
    xmlXPathObjectPtr objstack[nargs];	/* Stack for objects */
    int ndx;
    char *buf;
    int bufsiz;
    int hit = 0;

    slaxDataListInit(&list);

    bzero(objstack, sizeof(objstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	objstack[ndx] = valuePop(ctxt);
	if (objstack[ndx])
	    hit += 1;
    }

    /* If the args are empty, let's get out now */
    if (hit == 0) {
	xmlXPathReturnEmptyString(ctxt);
	return;
    }

    /* We make a custom IO handler to save content into a list of strings */
    handle = xmlSaveToIO(ext_xutil_rawwrite_callback, NULL, &list, NULL,
                 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    for (ndx = 0; ndx < nargs; ndx++) {
	xop = objstack[ndx];
	if (xop == NULL)
	    continue;

	if (xop->nodesetval) {
	    xmlNodeSetPtr tab = xop->nodesetval;
	    int i;
	    for (i = 0; i < tab->nodeNr; i++) {
		xmlNodePtr node = tab->nodeTab[i];

		xmlSaveTree(handle, node);
		xmlSaveFlush(handle);
	    }
	}
    }

    xmlSaveClose(handle);	/* This frees is also */

    /* Now we turn the saved data from a linked list into a single string */
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	bufsiz += dnp->dn_len;
    }

    /* If the objects are empty, let's get out now */
    if (bufsiz == 0) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    buf = xmlMalloc(bufsiz + 1);
    if (buf == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }
	
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	memcpy(buf + bufsiz, dnp->dn_data, dnp->dn_len);
	bufsiz += dnp->dn_len;
    }

    valuePush(ctxt, xmlXPathWrapCString(buf));

 bail:
    slaxDataListClean(&list);

    for (ndx = 0; ndx < nargs; ndx++)
	xmlXPathFreeObject(objstack[ndx]);
}

slax_function_table_t slaxXutilTable[] = {
    { "json-to-string", ext_xutil_json_to_string },
    { "string-to-json", ext_xutil_string_to_json },
    { "string-to-xml", ext_xutil_string_to_xml },
    { "xml-to-string", ext_xutil_xml_to_string },
    { NULL, NULL }
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxXutilTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
