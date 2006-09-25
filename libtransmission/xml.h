/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_XML_H
#define TR_XML_H 1

const char *
tr_xmlFindTag( const char * begin, const char * end, const char * tag );

const char *
tr_xmlTagName( const char * begin, const char * end, int * len );

const char *
tr_xmlTagContents( const char * begin, const char * end );

#define tr_xmlFindTagContents( bb, ee, tt ) \
    ( tr_xmlTagContents( tr_xmlFindTag( (bb), (ee), (tt) ), (ee) ) )

int
tr_xmlVerifyContents( const char * begin, const char * end, const char * data,
                      int ignorecase );

#define tr_xmlFindTagVerifyContents( bb, ee, tt, dd, ic ) \
    ( tr_xmlVerifyContents( tr_xmlFindTagContents( (bb), (ee), (tt) ), \
                            (ee), (dd), (ic) ) )

const char *
tr_xmlSkipTag( const char * begin, const char * end );

char *
tr_xmlDupContents( const char * begin, const char * end );

#define tr_xmlDupTagContents( bb, ee, tt ) \
  ( tr_xmlDupContents( tr_xmlFindTagContents( (bb), (ee), (tt) ), (ee) ) )

#endif
