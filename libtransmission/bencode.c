/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#include <assert.h>
#include <ctype.h> /* isdigit, isprint */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <event.h>

#include "transmission.h"
#include "bencode.h"
#include "ptrarray.h"
#include "utils.h"

/**
***
**/

static int
tr_bencIsInt( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_INT;
}

static int
tr_bencIsList( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_LIST;
}

static int
tr_bencIsDict( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_DICT;
}

static int
isContainer( const benc_val_t * val )
{
    return val!=NULL && ( val->type & ( TYPE_DICT | TYPE_LIST ) );
}

benc_val_t*
tr_bencListGetNthChild( benc_val_t * val, int i )
{
    benc_val_t * ret = NULL;
    if( tr_bencIsList( val ) && ( i >= 0 ) && ( i < val->val.l.count ) )
        ret = val->val.l.vals + i;
    return ret;
}


/**
***
**/

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
tr_bencParseInt( const uint8_t  * buf,
                 const uint8_t  * bufend,
                 const uint8_t ** setme_end, 
                 int64_t        * setme_val )
{
    int err = TR_OK;
    char * endptr;
    const void * begin;
    const void * end;
    int64_t val;

    if( buf >= bufend )
        return TR_ERROR;
    if( *buf != 'i' )
        return TR_ERROR;

    begin = buf + 1;
    end = memchr( begin, 'e', (bufend-buf)-1 );
    if( end == NULL )
        return TR_ERROR;

    errno = 0;
    val = strtoll( begin, &endptr, 10 );
    if( errno || ( endptr != end ) ) /* incomplete parse */
        err = TR_ERROR;
    else if( val && *(const char*)begin=='0' ) /* no leading zeroes! */
        err = TR_ERROR;
    else {
        *setme_end = end + 1;
        *setme_val = val;
    }

    return err;
}


/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam" 
 */
int
tr_bencParseStr( const uint8_t  * buf,
                 const uint8_t  * bufend,
                 const uint8_t ** setme_end,
                 uint8_t       ** setme_str,
                 size_t         * setme_strlen )
{
    size_t len;
    const void * end;
    char * endptr;

    if( buf >= bufend )
        return TR_ERROR;

    if( !isdigit( *buf  ) )
        return TR_ERROR;

    end = memchr( buf, ':', bufend-buf );
    if( end == NULL )
        return TR_ERROR;

    errno = 0;
    len = strtoul( (const char*)buf, &endptr, 10 );
    if( errno || endptr!=end )
        return TR_ERROR;

    if( (const uint8_t*)end + 1 + len > bufend )
        return TR_ERROR;

    *setme_end = end + 1 + len;
    *setme_str = (uint8_t*) tr_strndup( end + 1, len );
    *setme_strlen = len;
    return TR_OK;
}

/* setting to 1 to help expose bugs with tr_bencListAdd and tr_bencDictAdd */
#define LIST_SIZE 8 /* number of items to increment list/dict buffer by */

static int
makeroom( benc_val_t * val, int count )
{
    assert( TYPE_LIST == val->type || TYPE_DICT == val->type );

    if( val->val.l.count + count > val->val.l.alloc )
    {
        /* We need a bigger boat */
        const int len = val->val.l.alloc + count +
            ( count % LIST_SIZE ? LIST_SIZE - ( count % LIST_SIZE ) : 0 );
        void * new = realloc( val->val.l.vals, len * sizeof( benc_val_t ) );
        if( NULL == new )
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = new;
    }

    return 0;
}

static benc_val_t*
getNode( benc_val_t * top, tr_ptrArray * parentStack, int type )
{
    benc_val_t * parent;

    assert( top != NULL );
    assert( parentStack != NULL );

    if( tr_ptrArrayEmpty( parentStack ) )
        return top;

    parent = tr_ptrArrayBack( parentStack );
    assert( parent != NULL );

    /* dictionary keys must be strings */
    if( ( parent->type == TYPE_DICT )
        && ( type != TYPE_STR )
        && ( ! ( parent->val.l.count % 2 ) ) )
        return NULL;

    makeroom( parent, 1 );
    return parent->val.l.vals + parent->val.l.count++;
}

/**
 * this function's awkward stack-based implementation
 * is to prevent maliciously-crafed bencode data from
 * smashing our stack through deep recursion. (#667)
 */
int
tr_bencParse( const void     * buf_in,
              const void     * bufend_in,
              benc_val_t     * top,
              const uint8_t ** setme_end )
{
    int err;
    const uint8_t * buf = buf_in;
    const uint8_t * bufend = bufend_in;
    tr_ptrArray * parentStack = tr_ptrArrayNew( );

    while( buf != bufend )
    {
        if( buf > bufend ) /* no more text to parse... */
            return 1;

        if( *buf=='i' ) /* int */
        {
            int64_t val;
            const uint8_t * end;
            int err;
            benc_val_t * node;

            if(( err = tr_bencParseInt( buf, bufend, &end, &val )))
                return err;

            node = getNode( top, parentStack, TYPE_INT );
            if( !node )
                return TR_ERROR;

            tr_bencInitInt( node, val );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( *buf=='l' ) /* list */
        {
            benc_val_t * node = getNode( top, parentStack, TYPE_LIST );
            if( !node )
                return TR_ERROR;
            tr_bencInit( node, TYPE_LIST );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf=='d' ) /* dict */
        {
            benc_val_t * node = getNode( top, parentStack, TYPE_DICT );
            if( !node )
                return TR_ERROR;
            tr_bencInit( node, TYPE_DICT );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf=='e' ) /* end of list or dict */
        {
            ++buf;
            if( tr_ptrArrayEmpty( parentStack ) )
                return TR_ERROR;
            tr_ptrArrayPop( parentStack );
            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( isdigit(*buf) ) /* string? */
        {
            const uint8_t * end;
            uint8_t * str;
            size_t str_len;
            int err;
            benc_val_t * node;

            if(( err = tr_bencParseStr( buf, bufend, &end, &str, &str_len )))
                return err;

            node = getNode( top, parentStack, TYPE_STR );
            if( !node )
                return TR_ERROR;

            tr_bencInitStr( node, str, str_len, 0 );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else /* invalid bencoded text... march past it */
        {
            ++buf;
        }
    }

    err = tr_ptrArrayEmpty( parentStack ) ? 0 : 1;

    if( !err && ( setme_end != NULL ) )
        *setme_end = buf;

    tr_ptrArrayFree( parentStack, NULL );
    return err;
}

int
tr_bencLoad( const void  * buf_in,
             int           buflen,
             benc_val_t  * setme_benc,
             char       ** setme_end )
{
    const uint8_t * buf = buf_in;
    const uint8_t * end;
    const int ret = tr_bencParse( buf, buf+buflen, setme_benc, &end );
    if( !ret && setme_end )
        *setme_end = (char*) end;
    return ret;
}

benc_val_t *
tr_bencDictFind( benc_val_t * val, const char * key )
{
    int len, ii;

    if( val->type != TYPE_DICT )
    {
        return NULL;
    }

    len = strlen( key );
    
    for( ii = 0; ii + 1 < val->val.l.count; ii += 2 )
    {
        if( TYPE_STR  != val->val.l.vals[ii].type ||
            len       != val->val.l.vals[ii].val.s.i ||
            0 != memcmp( val->val.l.vals[ii].val.s.s, key, len ) )
        {
            continue;
        }
        return &val->val.l.vals[ii+1];
    }

    return NULL;
}

benc_val_t*
tr_bencDictFindType( benc_val_t * val, const char * key, int type )
{
    benc_val_t * ret = tr_bencDictFind( val, key );
    return ret && ret->type == type ? ret : NULL;
}

int64_t
tr_bencGetInt ( const benc_val_t * val )
{
    assert( tr_bencIsInt( val ) );
    return val->val.i;
}

benc_val_t *
tr_bencDictFindFirst( benc_val_t * val, ... )
{
    const char * key;
    benc_val_t * ret;
    va_list      ap;

    ret = NULL;
    va_start( ap, val );
    while( ( key = va_arg( ap, const char * ) ) )
    {
        ret = tr_bencDictFind( val, key );
        if( NULL != ret )
        {
            break;
        }
    }
    va_end( ap );

    return ret;
}

char *
tr_bencStealStr( benc_val_t * val )
{
    assert( TYPE_STR == val->type );
    val->val.s.nofree = 1;
    return val->val.s.s;
}

void
_tr_bencInitStr( benc_val_t * val, char * str, int len, int nofree )
{
    tr_bencInit( val, TYPE_STR );
    val->val.s.s      = str;
    val->val.s.nofree = nofree;
    if( 0 >= len )
    {
        len = ( NULL == str ? 0 : strlen( str ) );
    }
    val->val.s.i = len;
}

int
tr_bencInitStrDup( benc_val_t * val, const char * str )
{
    char * newStr = tr_strdup( str );
    if( newStr == NULL )
        return 1;

    _tr_bencInitStr( val, newStr, 0, 0 );
    return 0;
}

void
tr_bencInitInt( benc_val_t * val, int64_t num )
{
    tr_bencInit( val, TYPE_INT );
    val->val.i = num;
}

int
tr_bencListReserve( benc_val_t * val, int count )
{
    assert( TYPE_LIST == val->type );

    return makeroom( val, count );
}

int
tr_bencDictReserve( benc_val_t * val, int count )
{
    assert( TYPE_DICT == val->type );

    return makeroom( val, count * 2 );
}

benc_val_t *
tr_bencListAdd( benc_val_t * list )
{
    benc_val_t * item;

    assert( tr_bencIsList( list ) );
    assert( list->val.l.count < list->val.l.alloc );

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    tr_bencInit( item, TYPE_INT );

    return item;
}

benc_val_t *
tr_bencDictAdd( benc_val_t * dict, const char * key )
{
    benc_val_t * keyval, * itemval;

    assert( tr_bencIsDict( dict ) );
    assert( dict->val.l.count + 2 <= dict->val.l.alloc );

    keyval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInitStr( keyval, (char*)key, -1, 1 );

    itemval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInit( itemval, TYPE_INT );

    return itemval;
}


/***
****  BENC WALKING
***/

struct KeyIndex
{
    const char * key;
    int index;
};

static int
compareKeyIndex( const void * va, const void * vb )
{
    const struct KeyIndex * a = va;
    const struct KeyIndex * b = vb;
    return strcmp( a->key, b->key );
}

struct SaveNode
{
    const benc_val_t * val;
    int valIsSaved;
    int childCount;
    int childIndex;
    int * children;
};

static struct SaveNode*
nodeNewDict( const benc_val_t * val )
{
    int i, j, n;
    struct SaveNode * node;
    struct KeyIndex * indices;

    assert( tr_bencIsDict( val ) );

    n = val->val.l.count;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->children = tr_new0( int, n );

    /* ugh, a dictionary's children have to be sorted by key... */
    indices = tr_new( struct KeyIndex, n );
    for( i=j=0; i<n; i+=2, ++j ) {
        indices[j].key = val->val.l.vals[i].val.s.s;
        indices[j].index = i;
    }
    qsort( indices, j, sizeof(struct KeyIndex), compareKeyIndex );
    for( i=0; i<j; ++i ) {
        const int index = indices[i].index;
        node->children[ node->childCount++ ] = index;
        node->children[ node->childCount++ ] = index + 1;
    }

    assert( node->childCount == n );
    tr_free( indices );
    return node;
}

static struct SaveNode*
nodeNewList( const benc_val_t * val )
{
    int i, n;
    struct SaveNode * node;

    assert( tr_bencIsList( val ) );

    n = val->val.l.count;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->childCount = n;
    node->children = tr_new0( int, n );
    for( i=0; i<n; ++i ) /* a list's children don't need to be reordered */
        node->children[i] = i;

    return node;
}

static struct SaveNode*
nodeNewLeaf( const benc_val_t * val )
{
    struct SaveNode * node;

    assert( !isContainer( val ) );

    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    return node;
}

static struct SaveNode*
nodeNew( const benc_val_t * val )
{
    struct SaveNode * node;

    if( val->type == TYPE_LIST )
        node = nodeNewList( val );
    else if( val->type == TYPE_DICT )
        node = nodeNewDict( val );
    else
        node = nodeNewLeaf( val );

    return node;
}

typedef void (*BencWalkFunc)( const benc_val_t * val, void * user_data );

struct WalkFuncs
{
    BencWalkFunc intFunc;
    BencWalkFunc stringFunc;
    BencWalkFunc dictBeginFunc;
    BencWalkFunc listBeginFunc;
    BencWalkFunc containerEndFunc;
};

/**
 * this function's awkward stack-based implementation
 * is to prevent maliciously-crafed bencode data from
 * smashing our stack through deep recursion. (#667)
 */
static void
bencWalk( const benc_val_t   * top,
          struct WalkFuncs   * walkFuncs,
          void               * user_data )
{
    tr_ptrArray * stack = tr_ptrArrayNew( );
    tr_ptrArrayAppend( stack, nodeNew( top ) );

    while( !tr_ptrArrayEmpty( stack ) )
    {
        struct SaveNode * node = tr_ptrArrayBack( stack );
        const benc_val_t * val;

        if( !node->valIsSaved )
        {
            val = node->val;
            node->valIsSaved = TRUE;
        }
        else if( node->childIndex < node->childCount )
        {
            const int index = node->children[ node->childIndex++ ];
            val = node->val->val.l.vals +  index;
        }
        else /* done with this node */
        {
            if( isContainer( node->val ) )
                walkFuncs->containerEndFunc( node->val, user_data );
            tr_ptrArrayPop( stack );
            tr_free( node->children );
            tr_free( node );
            continue;
        }

        switch( val->type )
        {
            case TYPE_INT:
                walkFuncs->intFunc( val, user_data );
                break;

            case TYPE_STR:
                walkFuncs->stringFunc( val, user_data );
                break;

            case TYPE_LIST:
                if( val != node->val )
                    tr_ptrArrayAppend( stack, nodeNew( val ) );
                else
                    walkFuncs->listBeginFunc( val, user_data );
                break;

            case TYPE_DICT:
                if( val != node->val )
                    tr_ptrArrayAppend( stack, nodeNew( val ) );
                else
                    walkFuncs->dictBeginFunc( val, user_data );
                break;

            default:
                /* did caller give us an uninitialized val? */
                tr_err( "Invalid benc type %d", val->type );
                break;
        }
    }

    tr_ptrArrayFree( stack, NULL );
}

/****
*****
****/

static void
saveIntFunc( const benc_val_t * val, void * evbuf )
{
    evbuffer_add_printf( evbuf, "i%"PRId64"e", tr_bencGetInt(val) );
}
static void
saveStringFunc( const benc_val_t * val, void * vevbuf )
{
    struct evbuffer * evbuf = vevbuf;
    evbuffer_add_printf( evbuf, "%i:", val->val.s.i );
    evbuffer_add( evbuf, val->val.s.s, val->val.s.i );
}
static void
saveDictBeginFunc( const benc_val_t * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "d" );
}
static void
saveListBeginFunc( const benc_val_t * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "l" );
}
static void
saveContainerEndFunc( const benc_val_t * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "e" );
}
char*
tr_bencSave( const benc_val_t * top, int * len )
{
    char * ret;
    struct WalkFuncs walkFuncs;
    struct evbuffer * out = evbuffer_new( );

    walkFuncs.intFunc = saveIntFunc;
    walkFuncs.stringFunc = saveStringFunc;
    walkFuncs.dictBeginFunc = saveDictBeginFunc;
    walkFuncs.listBeginFunc = saveListBeginFunc;
    walkFuncs.containerEndFunc = saveContainerEndFunc;
    bencWalk( top, &walkFuncs, out );
    
    if( len != NULL )
        *len = EVBUFFER_LENGTH( out );
    ret = tr_strndup( (char*) EVBUFFER_DATA( out ), EVBUFFER_LENGTH( out ) );
    evbuffer_free( out );
    return ret;
}

/***
****
***/

static void
freeDummyFunc( const benc_val_t * val UNUSED, void * buf UNUSED  )
{
}
static void
freeStringFunc( const benc_val_t * val, void * freeme )
{
    if( !val->val.s.nofree )
        tr_ptrArrayAppend( freeme, val->val.s.s );
}
static void
freeContainerBeginFunc( const benc_val_t * val, void * freeme )
{
    tr_ptrArrayAppend( freeme, val->val.l.vals );
}
void
tr_bencFree( benc_val_t * val )
{
    if( val != NULL )
    {
        tr_ptrArray * freeme = tr_ptrArrayNew( );
        struct WalkFuncs walkFuncs;

        walkFuncs.intFunc = freeDummyFunc;
        walkFuncs.stringFunc = freeStringFunc;
        walkFuncs.dictBeginFunc = freeContainerBeginFunc;
        walkFuncs.listBeginFunc = freeContainerBeginFunc;
        walkFuncs.containerEndFunc = freeDummyFunc;
        bencWalk( val, &walkFuncs, freeme );

        tr_ptrArrayFree( freeme, tr_free );
    }
}

/***
****
***/

struct WalkPrint
{
    int depth;
    FILE * out;
};
static void
printLeadingSpaces( struct WalkPrint * data )
{
    const int width = data->depth * 2;
    fprintf( data->out, "%*.*s", width, width, " " );
}
static void
printIntFunc( const benc_val_t * val, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "int:  %"PRId64"\n", tr_bencGetInt(val) );
}
static void
printStringFunc( const benc_val_t * val, void * vdata )
{
    int ii;
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "string:  " );
    for( ii = 0; val->val.s.i > ii; ii++ )
    {
        if( '\\' == val->val.s.s[ii] ) {
            putc( '\\', data->out );
            putc( '\\', data->out );
        } else if( isprint( val->val.s.s[ii] ) ) {
            putc( val->val.s.s[ii], data->out );
        } else {
            fprintf( data->out, "\\x%02x", val->val.s.s[ii] );
        }
    }
    fprintf( data->out, "\n" );
}
static void
printListBeginFunc( const benc_val_t * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "list\n" );
    ++data->depth;
}
static void
printDictBeginFunc( const benc_val_t * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "dict\n" );
    ++data->depth;
}
static void
printContainerEndFunc( const benc_val_t * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    --data->depth;
}
void
tr_bencPrint( benc_val_t * val )
{
    struct WalkFuncs walkFuncs;
    struct WalkPrint walkPrint;

    walkFuncs.intFunc = printIntFunc;
    walkFuncs.stringFunc = printStringFunc;
    walkFuncs.dictBeginFunc = printDictBeginFunc;
    walkFuncs.listBeginFunc = printListBeginFunc;
    walkFuncs.containerEndFunc = printContainerEndFunc;

    walkPrint.out = stderr;
    walkPrint.depth = 0;
    bencWalk( val, &walkFuncs, &walkPrint );
}
