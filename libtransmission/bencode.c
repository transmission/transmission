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

#include <event.h> /* evbuffer */

#include "transmission.h"
#include "bencode.h"
#include "list.h"
#include "ptrarray.h"
#include "utils.h" /* tr_new(), tr_free() */

/**
***
**/

static int
isType( const tr_benc * val, int type )
{
    return ( ( val != NULL ) && ( val->type == type ) );
}

#define isInt(v)    ( isType( ( v ), TYPE_INT ) )
#define isString(v) ( isType( ( v ), TYPE_STR ) )
#define isList(v)   ( isType( ( v ), TYPE_LIST ) )
#define isDict(v)   ( isType( ( v ), TYPE_DICT ) )

static int
isContainer( const tr_benc * val )
{
    return isList(val) || isDict(val);
}
static int
isSomething( const tr_benc * val )
{
    return isContainer(val) || isInt(val) || isString(val);
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
makeroom( tr_benc * val, int count )
{
    assert( TYPE_LIST == val->type || TYPE_DICT == val->type );

    if( val->val.l.count + count > val->val.l.alloc )
    {
        /* We need a bigger boat */
        const int len = val->val.l.alloc + count +
            ( count % LIST_SIZE ? LIST_SIZE - ( count % LIST_SIZE ) : 0 );
        void * new = realloc( val->val.l.vals, len * sizeof( tr_benc ) );
        if( NULL == new )
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = new;
    }

    return 0;
}

static tr_benc*
getNode( tr_benc * top, tr_ptrArray * parentStack, int type )
{
    tr_benc * parent;

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
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
int
tr_bencParse( const void     * buf_in,
              const void     * bufend_in,
              tr_benc     * top,
              const uint8_t ** setme_end )
{
    int err;
    const uint8_t * buf = buf_in;
    const uint8_t * bufend = bufend_in;
    tr_ptrArray * parentStack = tr_ptrArrayNew( );

    tr_bencInit( top, 0 );

    while( buf != bufend )
    {
        if( buf > bufend ) /* no more text to parse... */
            return 1;

        if( *buf=='i' ) /* int */
        {
            int64_t val;
            const uint8_t * end;
            int err;
            tr_benc * node;

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
            tr_benc * node = getNode( top, parentStack, TYPE_LIST );
            if( !node )
                return TR_ERROR;
            tr_bencInit( node, TYPE_LIST );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf=='d' ) /* dict */
        {
            tr_benc * node = getNode( top, parentStack, TYPE_DICT );
            if( !node )
                return TR_ERROR;
            tr_bencInit( node, TYPE_DICT );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf=='e' ) /* end of list or dict */
        {
            tr_benc * node;
            ++buf;
            if( tr_ptrArrayEmpty( parentStack ) )
                return TR_ERROR;

            node = tr_ptrArrayBack( parentStack );
            if( isDict( node ) && ( node->val.l.count % 2 ) )
                return TR_ERROR; /* odd # of children in dict */

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
            tr_benc * node;

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

    err = !isSomething( top ) || !tr_ptrArrayEmpty( parentStack );

    if( !err && ( setme_end != NULL ) )
        *setme_end = buf;

    tr_ptrArrayFree( parentStack, NULL );
    return err;
}

int
tr_bencLoad( const void  * buf_in,
             int           buflen,
             tr_benc  * setme_benc,
             char       ** setme_end )
{
    const uint8_t * buf = buf_in;
    const uint8_t * end;
    const int ret = tr_bencParse( buf, buf+buflen, setme_benc, &end );
    if( !ret && setme_end )
        *setme_end = (char*) end;
    return ret;
}

/***
****
***/

tr_benc *
tr_bencDictFind( tr_benc * val, const char * key )
{
    int len, ii;

    if( !isDict( val ) )
        return NULL;

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

tr_benc*
tr_bencDictFindType( tr_benc * val, const char * key, int type )
{
    tr_benc * ret = tr_bencDictFind( val, key );
    return ret && ret->type == type ? ret : NULL;
}

tr_benc *
tr_bencDictFindFirst( tr_benc * val, ... )
{
    const char * key;
    tr_benc * ret;
    va_list      ap;

    ret = NULL;
    va_start( ap, val );
    while(( key = va_arg( ap, const char * )))
        if(( ret = tr_bencDictFind( val, key )))
            break;
    va_end( ap );

    return ret;
}

tr_benc*
tr_bencListGetNthChild( tr_benc * val, int i )
{
    tr_benc * ret = NULL;
    if( isList( val ) && ( i >= 0 ) && ( i < val->val.l.count ) )
        ret = val->val.l.vals + i;
    return ret;
}

int64_t
tr_bencGetInt ( const tr_benc * val )
{
    assert( isInt( val ) );
    return val->val.i;
}

char *
tr_bencStealStr( tr_benc * val )
{
    assert( isString( val ) );
    val->val.s.nofree = 1;
    return val->val.s.s;
}

/***
****
***/

void
_tr_bencInitStr( tr_benc * val, char * str, int len, int nofree )
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
tr_bencInitStrDup( tr_benc * val, const char * str )
{
    char * newStr = tr_strdup( str );
    if( newStr == NULL )
        return 1;

    _tr_bencInitStr( val, newStr, 0, 0 );
    return 0;
}

void
tr_bencInitInt( tr_benc * val, int64_t num )
{
    tr_bencInit( val, TYPE_INT );
    val->val.i = num;
}

int
tr_bencListReserve( tr_benc * val, int count )
{
    assert( isList( val ) );

    return makeroom( val, count );
}

int
tr_bencDictReserve( tr_benc * val, int count )
{
    assert( isDict( val ) );

    return makeroom( val, count * 2 );
}

tr_benc *
tr_bencListAdd( tr_benc * list )
{
    tr_benc * item;

    assert( isList( list ) );
    assert( list->val.l.count < list->val.l.alloc );

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    tr_bencInit( item, TYPE_INT );

    return item;
}

tr_benc *
tr_bencDictAdd( tr_benc * dict, const char * key )
{
    tr_benc * keyval, * itemval;

    assert( isDict( dict ) );
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
    const tr_benc * val;
    int valIsVisited;
    int childCount;
    int childIndex;
    int * children;
};

static struct SaveNode*
nodeNewDict( const tr_benc * val )
{
    int i, j;
    int nKeys;
    struct SaveNode * node;
    struct KeyIndex * indices;

    assert( isDict( val ) );

    nKeys = val->val.l.count / 2;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->children = tr_new0( int, nKeys * 2 );

    /* ugh, a dictionary's children have to be sorted by key... */
    indices = tr_new( struct KeyIndex, nKeys );
    for( i=j=0; i<(nKeys*2); i+=2, ++j ) {
        indices[j].key = val->val.l.vals[i].val.s.s;
        indices[j].index = i;
    }
    qsort( indices, j, sizeof(struct KeyIndex), compareKeyIndex );
    for( i=0; i<j; ++i ) {
        const int index = indices[i].index;
        node->children[ node->childCount++ ] = index;
        node->children[ node->childCount++ ] = index + 1;
    }

    assert( node->childCount == nKeys * 2 );
    tr_free( indices );
    return node;
}

static struct SaveNode*
nodeNewList( const tr_benc * val )
{
    int i, n;
    struct SaveNode * node;

    assert( isList( val ) );

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
nodeNewLeaf( const tr_benc * val )
{
    struct SaveNode * node;

    assert( !isContainer( val ) );

    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    return node;
}

static struct SaveNode*
nodeNew( const tr_benc * val )
{
    struct SaveNode * node;

    if( isList( val ) )
        node = nodeNewList( val );
    else if( isDict( val ) )
        node = nodeNewDict( val );
    else
        node = nodeNewLeaf( val );

    return node;
}

typedef void (*BencWalkFunc)( const tr_benc * val, void * user_data );

struct WalkFuncs
{
    BencWalkFunc intFunc;
    BencWalkFunc stringFunc;
    BencWalkFunc dictBeginFunc;
    BencWalkFunc listBeginFunc;
    BencWalkFunc containerEndFunc;
};

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
static void
bencWalk( const tr_benc   * top,
          struct WalkFuncs   * walkFuncs,
          void               * user_data )
{
    tr_ptrArray * stack = tr_ptrArrayNew( );
    tr_ptrArrayAppend( stack, nodeNew( top ) );

    while( !tr_ptrArrayEmpty( stack ) )
    {
        struct SaveNode * node = tr_ptrArrayBack( stack );
        const tr_benc * val;

        if( !node->valIsVisited )
        {
            val = node->val;
            node->valIsVisited = TRUE;
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
                tr_err( _( "Invalid benc type %d" ), val->type );
                break;
        }
    }

    tr_ptrArrayFree( stack, NULL );
}

/****
*****
****/

static void
saveIntFunc( const tr_benc * val, void * evbuf )
{
    evbuffer_add_printf( evbuf, "i%"PRId64"e", tr_bencGetInt(val) );
}
static void
saveStringFunc( const tr_benc * val, void * vevbuf )
{
    struct evbuffer * evbuf = vevbuf;
    evbuffer_add_printf( evbuf, "%i:", val->val.s.i );
    evbuffer_add( evbuf, val->val.s.s, val->val.s.i );
}
static void
saveDictBeginFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "d" );
}
static void
saveListBeginFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "l" );
}
static void
saveContainerEndFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "e" );
}
char*
tr_bencSave( const tr_benc * top, int * len )
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
freeDummyFunc( const tr_benc * val UNUSED, void * buf UNUSED  )
{
}
static void
freeStringFunc( const tr_benc * val, void * freeme )
{
    if( !val->val.s.nofree )
        tr_ptrArrayAppend( freeme, val->val.s.s );
}
static void
freeContainerBeginFunc( const tr_benc * val, void * freeme )
{
    tr_ptrArrayAppend( freeme, val->val.l.vals );
}
void
tr_bencFree( tr_benc * val )
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
printIntFunc( const tr_benc * val, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "int:  %"PRId64"\n", tr_bencGetInt(val) );
}
static void
printStringFunc( const tr_benc * val, void * vdata )
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
printListBeginFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "list\n" );
    ++data->depth;
}
static void
printDictBeginFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "dict\n" );
    ++data->depth;
}
static void
printContainerEndFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    --data->depth;
}
void
tr_bencPrint( const tr_benc * val )
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

/***
****
***/

struct ParentState
{
    int type;
    int index;
};
 
struct phpWalk
{
    tr_list * parents;
    struct evbuffer * out;
};

static void
phpChildFunc( struct phpWalk * data )
{
    if( data->parents )
    {
        struct ParentState * parentState = data->parents->data;

        if( parentState->type == TYPE_LIST )
            evbuffer_add_printf( data->out, "i:%d;", parentState->index++ );
    }
}

static void
phpPushParent( struct phpWalk * data, int type )
{
    struct ParentState * parentState = tr_new( struct ParentState, 1 );
    parentState->type = type;
    parentState->index = 0;
    tr_list_prepend( &data->parents, parentState );
}

static void
phpPopParent( struct phpWalk * data )
{
    tr_free( tr_list_pop_front( &data->parents ) );
}

static void
phpIntFunc( const tr_benc * val, void * vdata )
{
    struct phpWalk * data = vdata;
    phpChildFunc( data );
    evbuffer_add_printf( data->out, "i:%"PRId64";", tr_bencGetInt(val) );
}
static void
phpStringFunc( const tr_benc * val, void * vdata )
{
    struct phpWalk * data = vdata;
    phpChildFunc( data );
    evbuffer_add_printf( data->out, "s:%d:\"%s\";", val->val.s.i, val->val.s.s );
}
static void
phpDictBeginFunc( const tr_benc * val, void * vdata )
{
    struct phpWalk * data = vdata;
    phpChildFunc( data );
    phpPushParent( data, TYPE_DICT );
    evbuffer_add_printf( data->out, "a:%d:{", val->val.l.count/2 );
}
static void
phpListBeginFunc( const tr_benc * val, void * vdata )
{
    struct phpWalk * data = vdata;
    phpChildFunc( data );
    phpPushParent( data, TYPE_LIST );
    evbuffer_add_printf( data->out, "a:%d:{", val->val.l.count );
}
static void
phpContainerEndFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct phpWalk * data = vdata;
    phpPopParent( data );
    evbuffer_add_printf( data->out, "}" );
}
char*
tr_bencSaveAsSerializedPHP( const tr_benc * top, int * len )
{
    char * ret;
    struct WalkFuncs walkFuncs;
    struct phpWalk data;

    data.out = evbuffer_new( );
    data.parents = NULL;

    walkFuncs.intFunc = phpIntFunc;
    walkFuncs.stringFunc = phpStringFunc;
    walkFuncs.dictBeginFunc = phpDictBeginFunc;
    walkFuncs.listBeginFunc = phpListBeginFunc;
    walkFuncs.containerEndFunc = phpContainerEndFunc;

    bencWalk( top, &walkFuncs, &data );
    
    if( len != NULL )
        *len = EVBUFFER_LENGTH( data.out );
    ret = tr_strndup( (char*) EVBUFFER_DATA( data.out ), EVBUFFER_LENGTH( data.out ) );
    evbuffer_free( data.out );
    return ret;
}
