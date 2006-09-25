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

#include "transmission.h"

/* http://www.w3.org/TR/2004/REC-xml-20040204/ */

#define WS( cc ) \
    ( ' ' == (cc) || '\t' == (cc) || '\n' == (cc) || '\r' == (cc) )
#define TAGBEGIN                '<'
#define TAGEND                  '>'
#define TAGCLOSE                '/'
#define NAMESPACESEP            ':'
#define SQUOTEC                 '\''
#define DQUOTEC                 '"'
#define SQUOTES                 "'"
#define DQUOTES                 "\""
#define COMMENTBEGIN            "<!--"
#define COMMENTEND              "-->"
#define PROCINSTBEGIN           "<?"
#define PROCINSTEND             "?>"
#define CDATABEGIN              "<![CDATA["
#define CDATAEND                "]]>"
#define BANGBEGIN               "<!"
#define BANGEND                 ">"
#define CHECKNULL( bb, ee, rr ) \
    { if( NULL == (bb) || (ee) <= (bb) ) return (rr); }
#define justskip( bb, ee, ap, ot, ct ) \
    ( skipthingy( (bb), (ee), (ap), (ot), (ct), NULL, NULL ) )

static char *
catrange( char * str, const char * begin, const char * end );
static int
skipall( const char * begin, const char * end, const char ** afterpos );
static int
nexttag( const char * begin, const char * end, const char ** tagpos );
static int
overtag( const char * begin, const char * end, const char ** overpos );
static int
tagname( const char * begin, const char * end,
         const char ** tagstart, const char ** namestart, int * namelen );
static int
skipthingy( const char * begin, const char * end, const char ** afterpos,
            const char * openthingy, const char * closethingy,
            const char ** databegin, const char ** dataend );

/* XXX check document charset, in http headers and/or <?xml> tag */

const char *
tr_xmlFindTag( const char * begin, const char * end, const char * tag )
{
    const char * name;
    int len;

    CHECKNULL( begin, end, NULL );

    while( tagname( begin, end, &begin, &name, &len ) )
    {
        assert( NULL != begin && NULL != name && 0 < len );
        if( 0 == tr_strncasecmp( tag, name, len ) )
        {
            return begin;
        }
        begin = tr_xmlSkipTag( begin, end );
    }

    return NULL;
}

const char *
tr_xmlTagName( const char * begin, const char * end, int * len )
{
    CHECKNULL( begin, end, NULL );

    if( tagname( begin, end, NULL, &begin, len ) )
    {
        return begin;
    }

    return NULL;
}

const char *
tr_xmlTagContents( const char * begin, const char * end )
{
    CHECKNULL( begin, end, NULL );

    if( nexttag( begin, end, &begin ) && overtag( begin, end, &begin ) )
    {
        begin = NULL;
    }

    return begin;
}

int
tr_xmlVerifyContents( const char * begin, const char * end, const char * data,
                      int ignorecase )
{
    int len;

    CHECKNULL( begin, end, 1 );
    len = strlen( data );

    while( end > begin && WS( *begin ) )
    {
        begin++;
    }
    if( end - begin > len )
    {
        if( ignorecase )
        {
            return ( 0 != tr_strncasecmp( begin, data, len ) );
        }
        else
        {
            return ( 0 != memcmp( begin, data, len ) );
        }
    }

    return 1;
}

const char *
tr_xmlSkipTag( const char * begin, const char * end )
{
    CHECKNULL( begin, end, NULL );

    if( nexttag( begin, end, &begin ) )
    {
        if( overtag( begin, end, &begin ) )
        {
            return begin;
        }
        while( NULL != begin )
        {
            if( nexttag( begin, end, &begin ) )
            {
                begin = tr_xmlSkipTag( begin, end );
            }
            else
            {
                overtag( begin, end, &begin );
                return begin;
            }
        }
    }

    return NULL;
}

char *
tr_xmlDupContents( const char * begin, const char * end )
{
    const char * ii, * cbegin, * cend;
    char       * ret;
    int          len;

    CHECKNULL( begin, end, NULL );

    ret = NULL;
    len = strlen( CDATABEGIN );

    while( end > begin )
    {
        ii = memchr( begin, TAGBEGIN, end - begin );
        if( NULL == ii )
        {
            free( ret );
            return NULL;
        }
        /* XXX expand entity references and such here */
        ret = catrange( ret, begin, ii );
        if( NULL == ret )
        {
            return NULL;
        }
        if( !skipthingy( ii, end, &begin, CDATABEGIN, CDATAEND,
                         &cbegin, &cend ) )
        {
            ret = catrange( ret, cbegin, cend );
            if( NULL == ret )
            {
                return NULL;
            }
        }
        else if( skipall( ii, end, &begin ) )
        {
            if( end > ii + 1 && TAGCLOSE == ii[1] )
            {
                return ret;
            }
            begin = tr_xmlSkipTag( ii, end );
        }
    }

    free( ret );

    return NULL;
}

static char *
catrange( char * str, const char * begin, const char * end )
{
    int    len;
    char * ret;

    if( NULL == str )
    {
        return tr_dupstr( begin, end - begin );
    }

    len = strlen( str );
    ret = realloc( str, len + end - begin + 1 );
    if( NULL == ret )
    {
        free( str );
        return NULL;
    }

    memcpy( ret + len, begin, end - begin );
    ret[len + end - begin] = '\0';

    return ret;
}

static int
skipall( const char * begin, const char * end, const char ** afterpos )
{
    return ( justskip( begin, end, afterpos, COMMENTBEGIN, COMMENTEND ) &&
             justskip( begin, end, afterpos, CDATABEGIN, CDATAEND ) &&
             justskip( begin, end, afterpos, PROCINSTBEGIN, PROCINSTEND ) &&
             justskip( begin, end, afterpos, BANGBEGIN, BANGEND ) );
}

/* returns true if a tag was found and it's a start or empty element tag */
static int
nexttag( const char * begin, const char * end, const char ** tagpos )
{
    CHECKNULL( begin, end, 0 );

    while( end > begin )
    {
        begin = memchr( begin, TAGBEGIN, end - begin );
        CHECKNULL( begin, end, 0 );
        if( justskip( begin, end, &begin, COMMENTBEGIN, COMMENTEND ) &&
            justskip( begin, end, &begin, CDATABEGIN, CDATAEND ) &&
            justskip( begin, end, &begin, PROCINSTBEGIN, PROCINSTEND ) &&
            justskip( begin, end, &begin, BANGBEGIN, BANGEND ) )
        {
            *tagpos = begin;
            begin++;
            if( end > begin )
            {
                return ( TAGCLOSE != *begin );
            }
            break;
        }
    }

    *tagpos = NULL;
    return 0;
}

/* returns true if the tag is an empty element such as <foo/> */
static int
overtag( const char * begin, const char * end, const char ** overpos )
{
    const char * ii;

    assert( NULL != begin && end > begin && TAGBEGIN == *begin );

    ii = begin + 1;
    while( end > ii )
    {
        switch( *ii )
        {
            case DQUOTEC:
                justskip( ii, end, &ii, DQUOTES, DQUOTES );
                break;
            case SQUOTEC:
                justskip( ii, end, &ii, SQUOTES, SQUOTES );
                break;
            case TAGEND:
                *overpos = ii + 1;
                for( ii--; begin < ii; ii-- )
                {
                    if( TAGCLOSE == *ii )
                    {
                        return 1;
                    }
                    if( !WS( *ii ) )
                    {
                        break;
                    }
                }
                return 0;
            default:
                ii++;
                break;
        }
    }

    *overpos = NULL;
    return 0;
}

static int
tagname( const char * begin, const char * end,
         const char ** tagstart, const char ** namestart, int * namelen )
{
    const char * name, * ii;

    CHECKNULL( begin, end, 0 );

    if( nexttag( begin, end, &begin ) )
    {
        assert( NULL != begin && TAGBEGIN == *begin );
        ii = begin + 1;
        while( end > ii && WS( *ii ) )
        {
            ii++;
        }
        name = ii;
        while( end > ii && TAGEND != *ii && !WS( *ii ) )
        {
            if( NAMESPACESEP == *ii )
            {
                name = ii + 1;
            }
            ii++;
        }
        if( end > ii && ii > name )
        {
            if( NULL != tagstart )
            {
                *tagstart = begin;
            }
            if( NULL != namestart )
            {
                *namestart = name;
            }
            if( NULL != namelen )
            {
                *namelen = ii - name;
            }
            return 1;
        }
    }

    return 0;
}

static int
skipthingy( const char * begin, const char * end, const char ** afterpos,
            const char * openthingy, const char * closethingy,
            const char ** databegin, const char ** dataend )
 {
    int len;

    CHECKNULL( begin, end, 1 );
    len = strlen( openthingy );
    if( 0 != memcmp( begin, openthingy, MIN( end - begin, len ) ) )
    {
        return 1;
    }

    if( NULL != afterpos )
    {
        *afterpos = NULL;
    }
    if( NULL != databegin )
    {
        *databegin = NULL;
    }
    if( NULL != dataend )
    {
        *dataend = NULL;
    }
    if( end - begin <= len )
    {
        return 0;
    }

    begin += len;
    if( NULL != databegin )
    {
        *databegin = begin;
    }

    len = strlen( closethingy );
    begin = tr_memmem( begin, end - begin, closethingy, len );
    if( NULL != dataend )
    {
        *dataend = begin;
    }
    if( NULL != afterpos && NULL != begin )
    {
        *afterpos = ( begin + len >= end ? NULL : begin + len );
    }

    return 0;
}
