/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isdigit() */
#include <errno.h>
#include <math.h> /* fabs() */
#include <stdio.h> /* rename() */
#include <stdlib.h> /* strtoul(), strtod(), realloc(), qsort(), mkstemp() */
#include <string.h>

#ifdef WIN32 /* tr_mkstemp() */
 #include <fcntl.h>
 #define _S_IREAD 256
 #define _S_IWRITE 128
#endif

#include <locale.h> /* setlocale() */
#include <unistd.h> /* write(), unlink() */

#include <event2/buffer.h>

#include "ConvertUTF.h"

#include "transmission.h"
#include "bencode.h"
#include "fdlimit.h" /* tr_close_file() */
#include "json.h"
#include "list.h"
#include "platform.h" /* TR_PATH_MAX */
#include "ptrarray.h"
#include "utils.h" /* tr_new(), tr_free() */

#ifndef ENODATA
 #define ENODATA EIO
#endif

/**
***
**/

static bool
isContainer( const tr_benc * val )
{
    return tr_bencIsList( val ) || tr_bencIsDict( val );
}

static bool
isSomething( const tr_benc * val )
{
    return isContainer( val ) || tr_bencIsInt( val )
                              || tr_bencIsString( val )
                              || tr_bencIsReal( val )
                              || tr_bencIsBool( val );
}

static void
tr_bencInit( tr_benc * val, char type )
{
    memset( val, 0, sizeof( *val ) );
    val->type = type;
}

/***
****  tr_bencParse()
****  tr_bencLoad()
***/

/**
 * The initial i and trailing e are beginning and ending delimiters.
 * You can have negative numbers such as i-3e. You cannot prefix the
 * number with a zero such as i04e. However, i0e is valid.
 * Example: i3e represents the integer "3"
 * NOTE: The maximum number of bit of this integer is unspecified,
 * but to handle it as a signed 64bit integer is mandatory to handle
 * "large files" aka .torrent for more that 4Gbyte
 */
int
tr_bencParseInt( const uint8_t *  buf,
                 const uint8_t *  bufend,
                 const uint8_t ** setme_end,
                 int64_t *        setme_val )
{
    char *       endptr;
    const void * begin;
    const void * end;
    int64_t      val;

    if( buf >= bufend )
        return EILSEQ;
    if( *buf != 'i' )
        return EILSEQ;

    begin = buf + 1;
    end = memchr( begin, 'e', ( bufend - buf ) - 1 );
    if( end == NULL )
        return EILSEQ;

    errno = 0;
    val = evutil_strtoll( begin, &endptr, 10 );
    if( errno || ( endptr != end ) ) /* incomplete parse */
        return EILSEQ;
    if( val && *(const char*)begin == '0' ) /* no leading zeroes! */
        return EILSEQ;

    *setme_end = (const uint8_t*)end + 1;
    *setme_val = val;
    return 0;
}

/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam"
 */
int
tr_bencParseStr( const uint8_t *  buf,
                 const uint8_t *  bufend,
                 const uint8_t ** setme_end,
                 const uint8_t ** setme_str,
                 size_t *         setme_strlen )
{
    size_t       len;
    const void * end;
    char *       endptr;

    if( buf >= bufend )
        return EILSEQ;

    if( !isdigit( *buf  ) )
        return EILSEQ;

    end = memchr( buf, ':', bufend - buf );
    if( end == NULL )
        return EILSEQ;

    errno = 0;
    len = strtoul( (const char*)buf, &endptr, 10 );
    if( errno || endptr != end )
        return EILSEQ;

    if( (const uint8_t*)end + 1 + len > bufend )
        return EILSEQ;

    *setme_end = (const uint8_t*)end + 1 + len;
    *setme_str = (const uint8_t*)end + 1;
    *setme_strlen = len;
    return 0;
}

/* set to 1 to help expose bugs with tr_bencListAdd and tr_bencDictAdd */
#define LIST_SIZE 4 /* number of items to increment list/dict buffer by */

static int
makeroom( tr_benc * val,
          size_t    count )
{
    assert( TR_TYPE_LIST == val->type || TR_TYPE_DICT == val->type );

    if( val->val.l.count + count > val->val.l.alloc )
    {
        /* We need a bigger boat */
        const int len = val->val.l.alloc + count +
                        ( count % LIST_SIZE ? LIST_SIZE -
                          ( count % LIST_SIZE ) : 0 );
        void * tmp = realloc( val->val.l.vals, len * sizeof( tr_benc ) );
        if( !tmp )
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = tmp;
    }

    return 0;
}

static tr_benc*
getNode( tr_benc *     top,
         tr_ptrArray * parentStack,
         int           type )
{
    tr_benc * parent;

    assert( top );
    assert( parentStack );

    if( tr_ptrArrayEmpty( parentStack ) )
        return top;

    parent = tr_ptrArrayBack( parentStack );
    assert( parent );

    /* dictionary keys must be strings */
    if( ( parent->type == TR_TYPE_DICT )
      && ( type != TR_TYPE_STR )
      && ( !( parent->val.l.count % 2 ) ) )
        return NULL;

    makeroom( parent, 1 );
    return parent->val.l.vals + parent->val.l.count++;
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
static int
tr_bencParseImpl( const void *     buf_in,
                  const void *     bufend_in,
                  tr_benc *        top,
                  tr_ptrArray *    parentStack,
                  const uint8_t ** setme_end )
{
    int             err;
    const uint8_t * buf = buf_in;
    const uint8_t * bufend = bufend_in;

    tr_bencInit( top, 0 );

    while( buf != bufend )
    {
        if( buf > bufend ) /* no more text to parse... */
            return 1;

        if( *buf == 'i' ) /* int */
        {
            int64_t         val;
            const uint8_t * end;
            tr_benc *       node;

            if( ( err = tr_bencParseInt( buf, bufend, &end, &val ) ) )
                return err;

            node = getNode( top, parentStack, TR_TYPE_INT );
            if( !node )
                return EILSEQ;

            tr_bencInitInt( node, val );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( *buf == 'l' ) /* list */
        {
            tr_benc * node = getNode( top, parentStack, TR_TYPE_LIST );
            if( !node )
                return EILSEQ;
            tr_bencInit( node, TR_TYPE_LIST );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf == 'd' ) /* dict */
        {
            tr_benc * node = getNode( top, parentStack, TR_TYPE_DICT );
            if( !node )
                return EILSEQ;
            tr_bencInit( node, TR_TYPE_DICT );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf == 'e' ) /* end of list or dict */
        {
            tr_benc * node;
            ++buf;
            if( tr_ptrArrayEmpty( parentStack ) )
                return EILSEQ;

            node = tr_ptrArrayBack( parentStack );
            if( tr_bencIsDict( node ) && ( node->val.l.count % 2 ) )
            {
                /* odd # of children in dict */
                tr_bencFree( &node->val.l.vals[--node->val.l.count] );
                return EILSEQ;
            }

            tr_ptrArrayPop( parentStack );
            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( isdigit( *buf ) ) /* string? */
        {
            const uint8_t * end;
            const uint8_t * str;
            size_t          str_len;
            tr_benc *       node;

            if( ( err = tr_bencParseStr( buf, bufend, &end, &str, &str_len ) ) )
                return err;

            node = getNode( top, parentStack, TR_TYPE_STR );
            if( !node )
                return EILSEQ;

            tr_bencInitStr( node, str, str_len );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else /* invalid bencoded text... march past it */
        {
            ++buf;
        }
    }

    err = !isSomething( top ) || !tr_ptrArrayEmpty( parentStack );

    if( !err && setme_end )
        *setme_end = buf;

    return err;
}

int
tr_bencParse( const void *     buf,
              const void *     end,
              tr_benc *        top,
              const uint8_t ** setme_end )
{
    int           err;
    tr_ptrArray   parentStack = TR_PTR_ARRAY_INIT;

    top->type = 0; /* set to `uninitialized' */
    err = tr_bencParseImpl( buf, end, top, &parentStack, setme_end );
    if( err )
        tr_bencFree( top );

    tr_ptrArrayDestruct( &parentStack, NULL );
    return err;
}

int
tr_bencLoad( const void * buf_in,
             size_t       buflen,
             tr_benc *    setme_benc,
             char **      setme_end )
{
    const uint8_t * buf = buf_in;
    const uint8_t * end;
    const int       ret = tr_bencParse( buf, buf + buflen, setme_benc, &end );

    if( !ret && setme_end )
        *setme_end = (char*) end;
    return ret;
}

/***
****
***/

/* returns true if the benc's string was malloced.
 * this occurs when the string is too long for our string buffer */
static inline int
stringIsAlloced( const tr_benc * val )
{
    return val->val.s.len >= sizeof( val->val.s.str.buf );
}

/* returns a const pointer to the benc's string */
static inline const char*
getStr( const tr_benc* val )
{
    return stringIsAlloced(val) ? val->val.s.str.ptr : val->val.s.str.buf;
}

static int
dictIndexOf( const tr_benc * val, const char * key )
{
    if( tr_bencIsDict( val ) )
    {
        size_t       i;
        const size_t len = strlen( key );

        for( i = 0; ( i + 1 ) < val->val.l.count; i += 2 )
        {
            const tr_benc * child = val->val.l.vals + i;
            if( ( child->type == TR_TYPE_STR )
              && ( child->val.s.len == len )
              && !memcmp( getStr(child), key, len ) )
                return i;
        }
    }

    return -1;
}

tr_benc *
tr_bencDictFind( tr_benc * val, const char * key )
{
    const int i = dictIndexOf( val, key );

    return i < 0 ? NULL : &val->val.l.vals[i + 1];
}

static bool
tr_bencDictFindType( tr_benc * dict, const char * key, int type, tr_benc ** setme )
{
    return tr_bencIsType( *setme = tr_bencDictFind( dict, key ), type );
}

size_t
tr_bencListSize( const tr_benc * list )
{
    return tr_bencIsList( list ) ? list->val.l.count : 0;
}

tr_benc*
tr_bencListChild( tr_benc * val,
                  size_t    i )
{
    tr_benc * ret = NULL;

    if( tr_bencIsList( val ) && ( i < val->val.l.count ) )
        ret = val->val.l.vals + i;
    return ret;
}

int
tr_bencListRemove( tr_benc * list, size_t i )
{
    if( tr_bencIsList( list ) && ( i < list->val.l.count ) )
    {
        tr_bencFree( &list->val.l.vals[i] );
        tr_removeElementFromArray( list->val.l.vals, i, sizeof( tr_benc ), list->val.l.count-- );
        return 1;
    }

    return 0;
}

static void
tr_benc_warning( const char * err )
{
    fprintf( stderr, "warning: %s\n", err );
}

bool
tr_bencGetInt( const tr_benc * val,
               int64_t *       setme )
{
    bool success = false;

    if( !success && (( success = tr_bencIsInt( val ))))
        if( setme )
            *setme = val->val.i;

    if( !success && (( success = tr_bencIsBool( val )))) {
        tr_benc_warning( "reading bool as an int" );
        if( setme )
            *setme = val->val.b ? 1 : 0;
    }

    return success;
}

bool
tr_bencGetStr( const tr_benc * val, const char ** setme )
{
    const bool success = tr_bencIsString( val );

    if( success )
        *setme = getStr( val );

    return success;
}

bool
tr_bencGetRaw( const tr_benc * val, const uint8_t ** setme_raw, size_t * setme_len )
{
    const bool success = tr_bencIsString( val );

    if( success ) {
        *setme_raw = (uint8_t*) getStr(val);
        *setme_len = val->val.s.len;
    }

    return success;
}

bool
tr_bencGetBool( const tr_benc * val, bool * setme )
{
    const char * str;
    bool success = false;

    if(( success = tr_bencIsBool( val )))
        *setme = val->val.b;

    if( !success && tr_bencIsInt( val ) )
        if(( success = ( val->val.i==0 || val->val.i==1 ) ))
            *setme = val->val.i!=0;

    if( !success && tr_bencGetStr( val, &str ) )
        if(( success = ( !strcmp(str,"true") || !strcmp(str,"false"))))
            *setme = !strcmp(str,"true");

    return success;
}

bool
tr_bencGetReal( const tr_benc * val, double * setme )
{
    bool success = false;

    if( !success && (( success = tr_bencIsReal( val ))))
        *setme = val->val.d;

    if( !success && (( success = tr_bencIsInt( val ))))
        *setme = val->val.i;

    if( !success && tr_bencIsString(val) )
    {
        char * endptr;
        char locale[128];
        double d;

        /* the json spec requires a '.' decimal point regardless of locale */
        tr_strlcpy( locale, setlocale( LC_NUMERIC, NULL ), sizeof( locale ) );
        setlocale( LC_NUMERIC, "POSIX" );
        d  = strtod( getStr(val), &endptr );
        setlocale( LC_NUMERIC, locale );

        if(( success = ( getStr(val) != endptr ) && !*endptr ))
            *setme = d;
    }


    return success;
}

bool
tr_bencDictFindInt( tr_benc * dict, const char * key, int64_t * setme )
{
    return tr_bencGetInt( tr_bencDictFind( dict, key ), setme );
}

bool
tr_bencDictFindBool( tr_benc * dict, const char * key, bool * setme )
{
    return tr_bencGetBool( tr_bencDictFind( dict, key ), setme );
}

bool
tr_bencDictFindReal( tr_benc * dict, const char * key, double * setme )
{
    return tr_bencGetReal( tr_bencDictFind( dict, key ), setme );
}

bool
tr_bencDictFindStr( tr_benc *  dict, const char *  key, const char ** setme )
{
    return tr_bencGetStr( tr_bencDictFind( dict, key ), setme );
}

bool
tr_bencDictFindList( tr_benc * dict, const char * key, tr_benc ** setme )
{
    return tr_bencDictFindType( dict, key, TR_TYPE_LIST, setme );
}

bool
tr_bencDictFindDict( tr_benc * dict, const char * key, tr_benc ** setme )
{
    return tr_bencDictFindType( dict, key, TR_TYPE_DICT, setme );
}

bool
tr_bencDictFindRaw( tr_benc * dict, const char * key, const uint8_t  ** setme_raw, size_t * setme_len )
{
    return tr_bencGetRaw( tr_bencDictFind( dict, key ), setme_raw, setme_len );
}

/***
****
***/

void
tr_bencInitRaw( tr_benc * val, const void * src, size_t byteCount )
{
    char * setme;
    tr_bencInit( val, TR_TYPE_STR );

    /* There's no way in benc notation to distinguish between
     * zero-terminated strings and raw byte arrays.
     * Because of this, tr_bencMergeDicts() and tr_bencListCopy()
     * don't know whether or not a TR_TYPE_STR node needs a '\0'.
     * Append one, een to the raw arrays, just to be safe. */

    if( byteCount < sizeof( val->val.s.str.buf ) )
        setme = val->val.s.str.buf;
    else
        setme = val->val.s.str.ptr = tr_new( char, byteCount + 1 );

    memcpy( setme, src, byteCount );
    setme[byteCount] = '\0';
    val->val.s.len = byteCount;
}

void
tr_bencInitStr( tr_benc * val, const void * str, int len )
{
    if( str == NULL )
        len = 0;
    else if( len < 0 )
        len = strlen( str );

    tr_bencInitRaw( val, str, len );
}

void
tr_bencInitBool( tr_benc * b, int value )
{
    tr_bencInit( b, TR_TYPE_BOOL );
    b->val.b = value != 0;
}

void
tr_bencInitReal( tr_benc * b, double value )
{
    tr_bencInit( b, TR_TYPE_REAL );
    b->val.d = value;
}

void
tr_bencInitInt( tr_benc * b, int64_t value )
{
    tr_bencInit( b, TR_TYPE_INT );
    b->val.i = value;
}

int
tr_bencInitList( tr_benc * b, size_t reserveCount )
{
    tr_bencInit( b, TR_TYPE_LIST );
    return tr_bencListReserve( b, reserveCount );
}

int
tr_bencListReserve( tr_benc * b, size_t count )
{
    assert( tr_bencIsList( b ) );
    return makeroom( b, count );
}

int
tr_bencInitDict( tr_benc * b, size_t reserveCount )
{
    tr_bencInit( b, TR_TYPE_DICT );
    return tr_bencDictReserve( b, reserveCount );
}

int
tr_bencDictReserve( tr_benc * b, size_t reserveCount )
{
    assert( tr_bencIsDict( b ) );
    return makeroom( b, reserveCount * 2 );
}

tr_benc *
tr_bencListAdd( tr_benc * list )
{
    tr_benc * item;

    assert( tr_bencIsList( list ) );

    if( list->val.l.count == list->val.l.alloc )
        tr_bencListReserve( list, LIST_SIZE );

    assert( list->val.l.count < list->val.l.alloc );

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    tr_bencInit( item, TR_TYPE_INT );

    return item;
}

tr_benc *
tr_bencListAddInt( tr_benc * list, int64_t val )
{
    tr_benc * node = tr_bencListAdd( list );

    tr_bencInitInt( node, val );
    return node;
}

tr_benc *
tr_bencListAddReal( tr_benc * list, double val )
{
    tr_benc * node = tr_bencListAdd( list );
    tr_bencInitReal( node, val );
    return node;
}

tr_benc *
tr_bencListAddBool( tr_benc * list, bool val )
{
    tr_benc * node = tr_bencListAdd( list );
    tr_bencInitBool( node, val );
    return node;
}

tr_benc *
tr_bencListAddStr( tr_benc * list, const char * val )
{
    tr_benc * node = tr_bencListAdd( list );
    tr_bencInitStr( node, val, -1 );
    return node;
}

tr_benc *
tr_bencListAddRaw( tr_benc * list, const uint8_t * val, size_t len )
{
    tr_benc * node = tr_bencListAdd( list );
    tr_bencInitRaw( node, val, len );
    return node;
}

tr_benc*
tr_bencListAddList( tr_benc * list,
                    size_t    reserveCount )
{
    tr_benc * child = tr_bencListAdd( list );

    tr_bencInitList( child, reserveCount );
    return child;
}

tr_benc*
tr_bencListAddDict( tr_benc * list,
                    size_t    reserveCount )
{
    tr_benc * child = tr_bencListAdd( list );

    tr_bencInitDict( child, reserveCount );
    return child;
}

tr_benc *
tr_bencDictAdd( tr_benc *    dict,
                const char * key )
{
    tr_benc * keyval, * itemval;

    assert( tr_bencIsDict( dict ) );
    if( dict->val.l.count + 2 > dict->val.l.alloc )
        makeroom( dict, 2 );
    assert( dict->val.l.count + 2 <= dict->val.l.alloc );

    keyval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInitStr( keyval, key, -1 );

    itemval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInit( itemval, TR_TYPE_INT );

    return itemval;
}

static tr_benc*
dictFindOrAdd( tr_benc * dict, const char * key, int type )
{
    tr_benc * child;

    /* see if it already exists, and if so, try to reuse it */
    if(( child = tr_bencDictFind( dict, key ))) {
        if( !tr_bencIsType( child, type ) ) {
            tr_bencDictRemove( dict, key );
            child = NULL;
        }
    }

    /* if it doesn't exist, create it */
    if( child == NULL )
        child = tr_bencDictAdd( dict, key );

    return child;
}

tr_benc*
tr_bencDictAddInt( tr_benc *    dict,
                   const char * key,
                   int64_t      val )
{
    tr_benc * child = dictFindOrAdd( dict, key, TR_TYPE_INT );
    tr_bencInitInt( child, val );
    return child;
}

tr_benc*
tr_bencDictAddBool( tr_benc * dict, const char * key, bool val )
{
    tr_benc * child = dictFindOrAdd( dict, key, TR_TYPE_BOOL );
    tr_bencInitBool( child, val );
    return child;
}

tr_benc*
tr_bencDictAddReal( tr_benc * dict, const char * key, double val )
{
    tr_benc * child = dictFindOrAdd( dict, key, TR_TYPE_REAL );
    tr_bencInitReal( child, val );
    return child;
}

tr_benc*
tr_bencDictAddStr( tr_benc * dict, const char * key, const char * val )
{
    tr_benc * child;

    /* see if it already exists, and if so, try to reuse it */
    if(( child = tr_bencDictFind( dict, key ))) {
        if( tr_bencIsString( child ) ) {
            if( stringIsAlloced( child ) )
                tr_free( child->val.s.str.ptr );
        } else {
            tr_bencDictRemove( dict, key );
            child = NULL;
        }
    }

    /* if it doesn't exist, create it */
    if( child == NULL )
        child = tr_bencDictAdd( dict, key );

    /* set it */
    tr_bencInitStr( child, val, -1 );

    return child;
}

tr_benc*
tr_bencDictAddRaw( tr_benc *    dict,
                   const char * key,
                   const void * src,
                   size_t       len )
{
    tr_benc * child;

    /* see if it already exists, and if so, try to reuse it */
    if(( child = tr_bencDictFind( dict, key ))) {
        if( tr_bencIsString( child ) ) {
            if( stringIsAlloced( child ) )
                tr_free( child->val.s.str.ptr );
        } else {
            tr_bencDictRemove( dict, key );
            child = NULL;
        }
    }

    /* if it doesn't exist, create it */
    if( child == NULL )
        child = tr_bencDictAdd( dict, key );

    /* set it */
    tr_bencInitRaw( child, src, len );

    return child;
}

tr_benc*
tr_bencDictAddList( tr_benc *    dict,
                    const char * key,
                    size_t       reserveCount )
{
    tr_benc * child = tr_bencDictAdd( dict, key );

    tr_bencInitList( child, reserveCount );
    return child;
}

tr_benc*
tr_bencDictAddDict( tr_benc *    dict,
                    const char * key,
                    size_t       reserveCount )
{
    tr_benc * child = tr_bencDictAdd( dict, key );

    tr_bencInitDict( child, reserveCount );
    return child;
}

int
tr_bencDictRemove( tr_benc *    dict,
                   const char * key )
{
    int i = dictIndexOf( dict, key );

    if( i >= 0 )
    {
        const int n = dict->val.l.count;
        tr_bencFree( &dict->val.l.vals[i] );
        tr_bencFree( &dict->val.l.vals[i + 1] );
        if( i + 2 < n )
        {
            dict->val.l.vals[i]   = dict->val.l.vals[n - 2];
            dict->val.l.vals[i + 1] = dict->val.l.vals[n - 1];
        }
        dict->val.l.count -= 2;
    }
    return i >= 0; /* return true if found */
}

/***
****  BENC WALKING
***/

struct KeyIndex
{
    const char *  key;
    int           index;
};

static int
compareKeyIndex( const void * va,
                 const void * vb )
{
    const struct KeyIndex * a = va;
    const struct KeyIndex * b = vb;

    return strcmp( a->key, b->key );
}

struct SaveNode
{
    const tr_benc *  val;
    int              valIsVisited;
    int              childCount;
    int              childIndex;
    int *            children;
};

static void
nodeInitDict( struct SaveNode * node, const tr_benc * val, bool sort_dicts )
{
    const int n = val->val.l.count;
    const int nKeys = n / 2;

    assert( tr_bencIsDict( val ) );

    node->val = val;
    node->children = tr_new0( int, n );

    if( sort_dicts )
    {
        int i, j;
        struct KeyIndex * indices = tr_new( struct KeyIndex, nKeys );
        for( i=j=0; i<n; i+=2, ++j )
        {
            indices[j].key = getStr(&val->val.l.vals[i]);
            indices[j].index = i;
        }
        qsort( indices, j, sizeof( struct KeyIndex ), compareKeyIndex );
        for( i = 0; i < j; ++i )
        {
            const int index = indices[i].index;
            node->children[node->childCount++] = index;
            node->children[node->childCount++] = index + 1;
        }

        tr_free( indices );
    }
    else
    {
        int i;

        for( i=0; i<n; ++i )
            node->children[node->childCount++] = i;
    }

    assert( node->childCount == n );
}

static void
nodeInitList( struct SaveNode * node, const tr_benc * val )
{
    int               i, n;

    assert( tr_bencIsList( val ) );

    n = val->val.l.count;
    node->val = val;
    node->childCount = n;
    node->children = tr_new0( int, n );
    for( i = 0; i < n; ++i ) /* a list's children don't need to be reordered */
        node->children[i] = i;
}

static void
nodeInitLeaf( struct SaveNode * node, const tr_benc * val )
{
    assert( !isContainer( val ) );

    node->val = val;
}

static void
nodeInit( struct SaveNode * node, const tr_benc * val, bool sort_dicts )
{
    static const struct SaveNode INIT_NODE = { NULL, 0, 0, 0, NULL };
    *node = INIT_NODE;

         if( tr_bencIsList( val ) ) nodeInitList( node, val );
    else if( tr_bencIsDict( val ) ) nodeInitDict( node, val, sort_dicts );
    else                            nodeInitLeaf( node, val );
}

typedef void ( *BencWalkFunc )( const tr_benc * val, void * user_data );

struct WalkFuncs
{
    BencWalkFunc    intFunc;
    BencWalkFunc    boolFunc;
    BencWalkFunc    realFunc;
    BencWalkFunc    stringFunc;
    BencWalkFunc    dictBeginFunc;
    BencWalkFunc    listBeginFunc;
    BencWalkFunc    containerEndFunc;
};

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
static void
bencWalk( const tr_benc          * top,
          const struct WalkFuncs * walkFuncs,
          void                   * user_data,
          bool                     sort_dicts )
{
    int stackSize = 0;
    int stackAlloc = 64;
    struct SaveNode * stack = tr_new( struct SaveNode, stackAlloc );

    nodeInit( &stack[stackSize++], top, sort_dicts );

    while( stackSize > 0 )
    {
        struct SaveNode * node = &stack[stackSize-1];
        const tr_benc *   val;

        if( !node->valIsVisited )
        {
            val = node->val;
            node->valIsVisited = true;
        }
        else if( node->childIndex < node->childCount )
        {
            const int index = node->children[node->childIndex++];
            val = node->val->val.l.vals +  index;
        }
        else /* done with this node */
        {
            if( isContainer( node->val ) )
                walkFuncs->containerEndFunc( node->val, user_data );
            --stackSize;
            tr_free( node->children );
            continue;
        }

        if( val ) switch( val->type )
            {
                case TR_TYPE_INT:
                    walkFuncs->intFunc( val, user_data );
                    break;

                case TR_TYPE_BOOL:
                    walkFuncs->boolFunc( val, user_data );
                    break;

                case TR_TYPE_REAL:
                    walkFuncs->realFunc( val, user_data );
                    break;

                case TR_TYPE_STR:
                    walkFuncs->stringFunc( val, user_data );
                    break;

                case TR_TYPE_LIST:
                    if( val == node->val )
                        walkFuncs->listBeginFunc( val, user_data );
                    else {
                        if( stackAlloc == stackSize ) {
                            stackAlloc *= 2;
                            stack = tr_renew( struct SaveNode, stack, stackAlloc );
                        }
                        nodeInit( &stack[stackSize++], val, sort_dicts );
                    }
                    break;

                case TR_TYPE_DICT:
                    if( val == node->val )
                        walkFuncs->dictBeginFunc( val, user_data );
                    else {
                        if( stackAlloc == stackSize ) {
                            stackAlloc *= 2;
                            stack = tr_renew( struct SaveNode, stack, stackAlloc );
                        }
                        nodeInit( &stack[stackSize++], val, sort_dicts );
                    }
                    break;

                default:
                    /* did caller give us an uninitialized val? */
                    tr_err( "%s", _( "Invalid metadata" ) );
                    break;
            }
    }

    tr_free( stack );
}

/****
*****
****/

static void
saveIntFunc( const tr_benc * val, void * evbuf )
{
    evbuffer_add_printf( evbuf, "i%" PRId64 "e", val->val.i );
}

static void
saveBoolFunc( const tr_benc * val, void * evbuf )
{
    if( val->val.b )
        evbuffer_add( evbuf, "i1e", 3 );
    else
        evbuffer_add( evbuf, "i0e", 3 );
}

static void
saveRealFunc( const tr_benc * val, void * evbuf )
{
    char buf[128];
    char locale[128];
    size_t len;

    /* always use a '.' decimal point s.t. locale-hopping doesn't bite us */
    tr_strlcpy( locale, setlocale( LC_NUMERIC, NULL ), sizeof( locale ) );
    setlocale( LC_NUMERIC, "POSIX" );
    tr_snprintf( buf, sizeof( buf ), "%f", val->val.d );
    setlocale( LC_NUMERIC, locale );

    len = strlen( buf );
    evbuffer_add_printf( evbuf, "%lu:", (unsigned long)len );
    evbuffer_add( evbuf, buf, len );
}

static void
saveStringFunc( const tr_benc * val, void * evbuf )
{
    evbuffer_add_printf( evbuf, "%lu:", (unsigned long)val->val.s.len );
    evbuffer_add( evbuf, getStr(val), val->val.s.len );
}

static void
saveDictBeginFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add( evbuf, "d", 1 );
}

static void
saveListBeginFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add( evbuf, "l", 1 );
}

static void
saveContainerEndFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add( evbuf, "e", 1 );
}

static const struct WalkFuncs saveFuncs = { saveIntFunc,
                                            saveBoolFunc,
                                            saveRealFunc,
                                            saveStringFunc,
                                            saveDictBeginFunc,
                                            saveListBeginFunc,
                                            saveContainerEndFunc };

/***
****
***/

static void
freeDummyFunc( const tr_benc * val UNUSED, void * buf UNUSED  )
{}

static void
freeStringFunc( const tr_benc * val, void * unused UNUSED )
{
    if( stringIsAlloced( val ) )
        tr_free( val->val.s.str.ptr );
}

static void
freeContainerEndFunc( const tr_benc * val, void * unused UNUSED )
{
    tr_free( val->val.l.vals );
}

static const struct WalkFuncs freeWalkFuncs = { freeDummyFunc,
                                                freeDummyFunc,
                                                freeDummyFunc,
                                                freeStringFunc,
                                                freeDummyFunc,
                                                freeDummyFunc,
                                                freeContainerEndFunc };

void
tr_bencFree( tr_benc * val )
{
    if( isSomething( val ) )
        bencWalk( val, &freeWalkFuncs, NULL, false );
}

/***
****
***/

/** @brief Implementation helper class for tr_bencToBuffer(TR_FMT_JSON) */
struct ParentState
{
    int    bencType;
    int    childIndex;
    int    childCount;
};

/** @brief Implementation helper class for tr_bencToBuffer(TR_FMT_JSON) */
struct jsonWalk
{
    bool doIndent;
    tr_list * parents;
    struct evbuffer *  out;
};

static void
jsonIndent( struct jsonWalk * data )
{
    if( data->doIndent )
    {
        char buf[1024];
        const int width = tr_list_size( data->parents ) * 4;

        buf[0] = '\n';
        memset( buf+1, ' ', width );
        evbuffer_add( data->out, buf, 1+width );
    }
}

static void
jsonChildFunc( struct jsonWalk * data )
{
    if( data->parents )
    {
        struct ParentState * parentState = data->parents->data;

        switch( parentState->bencType )
        {
            case TR_TYPE_DICT:
            {
                const int i = parentState->childIndex++;
                if( !( i % 2 ) )
                    evbuffer_add( data->out, ": ", data->doIndent ? 2 : 1 );
                else {
                    const bool isLast = parentState->childIndex == parentState->childCount;
                    if( !isLast ) {
                        evbuffer_add( data->out, ", ", data->doIndent ? 2 : 1 );
                        jsonIndent( data );
                    }
                }
                break;
            }

            case TR_TYPE_LIST:
            {
                const bool isLast = ++parentState->childIndex == parentState->childCount;
                if( !isLast ) {
                    evbuffer_add( data->out, ", ", data->doIndent ? 2 : 1 );
                    jsonIndent( data );
                }
                break;
            }

            default:
                break;
        }
    }
}

static void
jsonPushParent( struct jsonWalk * data,
                const tr_benc *   benc )
{
    struct ParentState * parentState = tr_new( struct ParentState, 1 );

    parentState->bencType = benc->type;
    parentState->childIndex = 0;
    parentState->childCount = benc->val.l.count;
    tr_list_prepend( &data->parents, parentState );
}

static void
jsonPopParent( struct jsonWalk * data )
{
    tr_free( tr_list_pop_front( &data->parents ) );
}

static void
jsonIntFunc( const tr_benc * val,
             void *          vdata )
{
    struct jsonWalk * data = vdata;

    evbuffer_add_printf( data->out, "%" PRId64, val->val.i );
    jsonChildFunc( data );
}

static void
jsonBoolFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;

    if( val->val.b )
        evbuffer_add( data->out, "true", 4 );
    else
        evbuffer_add( data->out, "false", 5 );

    jsonChildFunc( data );
}

static void
jsonRealFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;
    char locale[128];

    if( fabs( val->val.d - (int)val->val.d ) < 0.00001 )
        evbuffer_add_printf( data->out, "%d", (int)val->val.d );
    else {
        /* json requires a '.' decimal point regardless of locale */
        tr_strlcpy( locale, setlocale( LC_NUMERIC, NULL ), sizeof( locale ) );
        setlocale( LC_NUMERIC, "POSIX" );
        evbuffer_add_printf( data->out, "%.4f", tr_truncd( val->val.d, 4 ) );
        setlocale( LC_NUMERIC, locale );
    }

    jsonChildFunc( data );
}

static void
jsonStringFunc( const tr_benc * val, void * vdata )
{
    char * out;
    char * outwalk;
    char * outend;
    struct evbuffer_iovec vec[1];
    struct jsonWalk * data = vdata;
    const unsigned char * it = (const unsigned char *) getStr(val);
    const unsigned char * end = it + val->val.s.len;
    const int safeguard = 512; /* arbitrary margin for escapes and unicode */

    evbuffer_reserve_space( data->out, val->val.s.len+safeguard, vec, 1 );
    out = vec[0].iov_base;
    outend = out + vec[0].iov_len;

    outwalk = out;
    *outwalk++ = '"';

    for( ; it!=end; ++it )
    {
        switch( *it )
        {
            case '\b': *outwalk++ = '\\'; *outwalk++ = 'b'; break;
            case '\f': *outwalk++ = '\\'; *outwalk++ = 'f'; break;
            case '\n': *outwalk++ = '\\'; *outwalk++ = 'n'; break;
            case '\r': *outwalk++ = '\\'; *outwalk++ = 'r'; break;
            case '\t': *outwalk++ = '\\'; *outwalk++ = 't'; break;
            case '"' : *outwalk++ = '\\'; *outwalk++ = '"'; break;
            case '\\': *outwalk++ = '\\'; *outwalk++ = '\\'; break;

            default:
                if( isascii( *it ) )
                    *outwalk++ = *it;
                else {
                    const UTF8 * tmp = it;
                    UTF32        buf[1] = { 0 };
                    UTF32 *      u32 = buf;
                    ConversionResult result = ConvertUTF8toUTF32( &tmp, end, &u32, buf + 1, 0 );
                    if((( result==conversionOK ) || (result==targetExhausted)) && (tmp!=it)) {
                        outwalk += tr_snprintf( outwalk, outend-outwalk, "\\u%04x", (unsigned int)buf[0] );
                        it = tmp - 1;
                    }
                }
        }
    }

    *outwalk++ = '"';
    vec[0].iov_len = outwalk - out;
    evbuffer_commit_space( data->out, vec, 1 );

    jsonChildFunc( data );
}

static void
jsonDictBeginFunc( const tr_benc * val,
                   void *          vdata )
{
    struct jsonWalk * data = vdata;

    jsonPushParent( data, val );
    evbuffer_add( data->out, "{", 1 );
    if( val->val.l.count )
        jsonIndent( data );
}

static void
jsonListBeginFunc( const tr_benc * val,
                   void *          vdata )
{
    const size_t      nChildren = tr_bencListSize( val );
    struct jsonWalk * data = vdata;

    jsonPushParent( data, val );
    evbuffer_add( data->out, "[", 1 );
    if( nChildren )
        jsonIndent( data );
}

static void
jsonContainerEndFunc( const tr_benc * val,
                      void *          vdata )
{
    struct jsonWalk * data = vdata;
    int               emptyContainer = false;

    jsonPopParent( data );
    if( !emptyContainer )
        jsonIndent( data );
    if( tr_bencIsDict( val ) )
        evbuffer_add( data->out, "}", 1 );
    else /* list */
        evbuffer_add( data->out, "]", 1 );
    jsonChildFunc( data );
}

static const struct WalkFuncs jsonWalkFuncs = { jsonIntFunc,
                                                jsonBoolFunc,
                                                jsonRealFunc,
                                                jsonStringFunc,
                                                jsonDictBeginFunc,
                                                jsonListBeginFunc,
                                                jsonContainerEndFunc };

/***
****
***/

static void
tr_bencListCopy( tr_benc * target, const tr_benc * src )
{
    int i = 0;
    const tr_benc * val;

    while(( val = tr_bencListChild( (tr_benc*)src, i++ )))
    {
       if( tr_bencIsBool( val ) )
       {
           bool boolVal = 0;
           tr_bencGetBool( val, &boolVal );
           tr_bencListAddBool( target, boolVal );
       }
       else if( tr_bencIsReal( val ) )
       {
           double realVal = 0;
           tr_bencGetReal( val, &realVal );
           tr_bencListAddReal( target, realVal );
       }
       else if( tr_bencIsInt( val ) )
       {
           int64_t intVal = 0;
           tr_bencGetInt( val, &intVal );
           tr_bencListAddInt( target, intVal );
       }
       else if( tr_bencIsString( val ) )
       {
           tr_bencListAddRaw( target, (const uint8_t*)getStr( val ), val->val.s.len );
       }
       else if( tr_bencIsDict( val ) )
       {
           tr_bencMergeDicts( tr_bencListAddDict( target, 0 ), val );
       }
       else if ( tr_bencIsList( val ) )
       {
           tr_bencListCopy( tr_bencListAddList( target, 0 ), val );
       }
       else
       {
           tr_err( "tr_bencListCopy skipping item" );
       }
   }
}

static size_t
tr_bencDictSize( const tr_benc * dict )
{
    size_t count = 0;

    if( tr_bencIsDict( dict ) )
        count = dict->val.l.count / 2;

    return count;
}

bool
tr_bencDictChild( tr_benc * dict, size_t n, const char ** key, tr_benc ** val )
{
    bool success = 0;

    assert( tr_bencIsDict( dict ) );

    if( tr_bencIsDict( dict ) && (n*2)+1 <= dict->val.l.count )
    {
        tr_benc * k = dict->val.l.vals + (n*2);
        tr_benc * v = dict->val.l.vals + (n*2) + 1;
        if(( success = tr_bencGetStr( k, key ) && isSomething( v )))
            *val = v;
    }

    return success;
}

void
tr_bencMergeDicts( tr_benc * target, const tr_benc * source )
{
    size_t i;
    const size_t sourceCount = tr_bencDictSize( source );

    assert( tr_bencIsDict( target ) );
    assert( tr_bencIsDict( source ) );

    for( i=0; i<sourceCount; ++i )
    {
        const char * key;
        tr_benc * val;
        tr_benc * t;

        if( tr_bencDictChild( (tr_benc*)source, i, &key, &val ) )
        {
            if( tr_bencIsBool( val ) )
            {
                bool boolVal;
                tr_bencGetBool( val, &boolVal );
                tr_bencDictAddBool( target, key, boolVal );
            }
            else if( tr_bencIsReal( val ) )
            {
                double realVal = 0;
                tr_bencGetReal( val, &realVal );
                tr_bencDictAddReal( target, key, realVal );
            }
            else if( tr_bencIsInt( val ) )
            {
                int64_t intVal = 0;
                tr_bencGetInt( val, &intVal );
                tr_bencDictAddInt( target, key, intVal );
            }
            else if( tr_bencIsString( val ) )
            {
                tr_bencDictAddRaw( target, key, getStr( val ), val->val.s.len );
            }
            else if( tr_bencIsDict( val ) && tr_bencDictFindDict( target, key, &t ) )
            {
                tr_bencMergeDicts( t, val );
            }
            else if( tr_bencIsList( val ) )
            {
                if( tr_bencDictFind( target, key ) == NULL )
                {
                    tr_bencListCopy( tr_bencDictAddList( target, key, tr_bencListSize( val ) ), val );
                }
            }
            else
            {
                tr_dbg( "tr_bencMergeDicts skipping \"%s\"", key );
            }
        }
    }
}

/***
****
***/

struct evbuffer *
tr_bencToBuf( const tr_benc * top, tr_fmt_mode mode )
{
    struct evbuffer * buf = evbuffer_new( );

    evbuffer_expand( buf, 4096 ); /* alloc a little memory to start off with */

    switch( mode )
    {
        case TR_FMT_BENC:
            bencWalk( top, &saveFuncs, buf, true );
            break;

        case TR_FMT_JSON:
        case TR_FMT_JSON_LEAN: {
            struct jsonWalk data;
            data.doIndent = mode==TR_FMT_JSON;
            data.out = buf;
            data.parents = NULL;
            bencWalk( top, &jsonWalkFuncs, &data, true );
            if( evbuffer_get_length( buf ) )
                evbuffer_add_printf( buf, "\n" );
            break;
        }
    }

    return buf;
}

char*
tr_bencToStr( const tr_benc * top, tr_fmt_mode mode, int * len )
{
    struct evbuffer * buf = tr_bencToBuf( top, mode );
    const size_t n = evbuffer_get_length( buf );
    char * ret = evbuffer_free_to_str( buf );
    if( len != NULL )
        *len = (int) n;
    return ret;
}

/* portability wrapper for mkstemp(). */
static int
tr_mkstemp( char * template )
{
#ifdef WIN32
    const int flags = O_RDWR | O_BINARY | O_CREAT | O_EXCL | _O_SHORT_LIVED;
    const mode_t mode = _S_IREAD | _S_IWRITE;
    mktemp( template );
    return open( template, flags, mode );
#else
    return mkstemp( template );
#endif
}

int
tr_bencToFile( const tr_benc * top, tr_fmt_mode mode, const char * filename )
{
    char * tmp;
    int fd;
    int err = 0;
    char buf[TR_PATH_MAX];

    /* follow symlinks to find the "real" file, to make sure the temporary
     * we build with tr_mkstemp() is created on the right partition */
    if( tr_realpath( filename, buf ) != NULL )
        filename = buf;

    /* if the file already exists, try to move it out of the way & keep it as a backup */
    tmp = tr_strdup_printf( "%s.tmp.XXXXXX", filename );
    fd = tr_mkstemp( tmp );
    tr_set_file_for_single_pass( fd );
    if( fd >= 0 )
    {
        int nleft;

        /* save the benc to a temporary file */
        {
            struct evbuffer * buf = tr_bencToBuf( top, mode );
            const char * walk = (const char *) evbuffer_pullup( buf, -1 );
            nleft = evbuffer_get_length( buf );

            while( nleft > 0 ) {
                const int n = write( fd, walk, nleft );
                if( n >= 0 ) {
                    nleft -= n;
                    walk += n;
                }
                else if( errno != EAGAIN ) {
                    err = errno;
                    break;
                }
            }

            evbuffer_free( buf );
        }

        if( nleft > 0 )
        {
            tr_err( _( "Couldn't save temporary file \"%1$s\": %2$s" ), tmp, tr_strerror( err ) );
            tr_close_file( fd );
            unlink( tmp );
        }
        else
        {
            //tr_fsync( fd );
            tr_close_file( fd );

#ifdef WIN32
            if( MoveFileEx( tmp, filename, MOVEFILE_REPLACE_EXISTING ) )
#else
            if( !rename( tmp, filename ) )
#endif
            {
                tr_inf( _( "Saved \"%s\"" ), filename );
            }
            else
            {
                err = errno;
                tr_err( _( "Couldn't save file \"%1$s\": %2$s" ), filename, tr_strerror( err ) );
                unlink( tmp );
            }
        }
    }
    else
    {
        err = errno;
        tr_err( _( "Couldn't save temporary file \"%1$s\": %2$s" ), tmp, tr_strerror( err ) );
    }

    tr_free( tmp );
    return err;
}

/***
****
***/

int
tr_bencLoadFile( tr_benc * setme, tr_fmt_mode mode, const char * filename )
{
    int err;
    size_t contentLen;
    uint8_t * content;

    content = tr_loadFile( filename, &contentLen );
    if( !content && errno )
        err = errno;
    else if( !content )
        err = ENODATA;
    else {
        if( mode == TR_FMT_BENC )
            err = tr_bencLoad( content, contentLen, setme, NULL );
        else
            err = tr_jsonParse( filename, content, contentLen, setme, NULL );
    }

    tr_free( content );
    return err;
}

