/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isdigit, isprint, isspace */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <locale.h>

#include <event.h> /* evbuffer */

#include "ConvertUTF.h"

#include "transmission.h"
#include "bencode.h"
#include "json.h"
#include "list.h"
#include "ptrarray.h"
#include "utils.h" /* tr_new(), tr_free() */

/**
***
**/

tr_bool
tr_bencIsType( const tr_benc * val, int type )
{
    return ( val ) && ( val->type == type );
}

static tr_bool
isContainer( const tr_benc * val )
{
    return tr_bencIsList( val ) || tr_bencIsDict( val );
}

static tr_bool
isSomething( const tr_benc * val )
{
    return isContainer( val ) || tr_bencIsInt( val ) || tr_bencIsString( val );
}

static void
tr_bencInit( tr_benc * val,
             int       type )
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
    int          err = 0;
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
        err = EILSEQ;
    else if( val && *(const char*)begin == '0' ) /* no leading zeroes! */
        err = EILSEQ;
    else
    {
        *setme_end = (const uint8_t*)end + 1;
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
#define LIST_SIZE 8 /* number of items to increment list/dict buffer by */

static int
makeroom( tr_benc * val,
          size_t    count )
{
    assert( TYPE_LIST == val->type || TYPE_DICT == val->type );

    if( val->val.l.count + count > val->val.l.alloc )
    {
        /* We need a bigger boat */
        const int len = val->val.l.alloc + count +
                        ( count % LIST_SIZE ? LIST_SIZE -
                          ( count % LIST_SIZE ) : 0 );
        void *    new = realloc( val->val.l.vals, len * sizeof( tr_benc ) );
        if( NULL == new )
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = new;
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
    if( ( parent->type == TYPE_DICT )
      && ( type != TYPE_STR )
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
            int             err;
            tr_benc *       node;

            if( ( err = tr_bencParseInt( buf, bufend, &end, &val ) ) )
                return err;

            node = getNode( top, parentStack, TYPE_INT );
            if( !node )
                return EILSEQ;

            tr_bencInitInt( node, val );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( *buf == 'l' ) /* list */
        {
            tr_benc * node = getNode( top, parentStack, TYPE_LIST );
            if( !node )
                return EILSEQ;
            tr_bencInit( node, TYPE_LIST );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf == 'd' ) /* dict */
        {
            tr_benc * node = getNode( top, parentStack, TYPE_DICT );
            if( !node )
                return EILSEQ;
            tr_bencInit( node, TYPE_DICT );
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
            int             err;
            tr_benc *       node;

            if( ( err = tr_bencParseStr( buf, bufend, &end, &str, &str_len ) ) )
                return err;

            node = getNode( top, parentStack, TYPE_STR );
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

static int
dictIndexOf( const tr_benc * val,
             const char *    key )
{
    if( tr_bencIsDict( val ) )
    {
        size_t       i;
        const size_t len = strlen( key );

        for( i = 0; ( i + 1 ) < val->val.l.count; i += 2 )
        {
            const tr_benc * child = val->val.l.vals + i;

            if( ( child->type == TYPE_STR )
              && ( child->val.s.i == len )
              && !memcmp( child->val.s.s, key, len ) )
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

static tr_benc*
tr_bencDictFindType( tr_benc *    val,
                     const char * key,
                     int          type )
{
    tr_benc * ret = tr_bencDictFind( val, key );

    return ( ret && ( ret->type == type ) ) ? ret : NULL;
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

tr_bool
tr_bencGetInt( const tr_benc * val,
               int64_t *       setme )
{
    const int success = tr_bencIsInt( val );

    if( success && setme )
        *setme = val->val.i;

    return success;
}

tr_bool
tr_bencGetStr( const tr_benc * val,
               const char **   setme )
{
    const int success = tr_bencIsString( val );

    if( success )
        *setme = val->val.s.s;
    return success;
}

tr_bool
tr_bencDictFindInt( tr_benc * dict, const char * key, int64_t * setme )
{
    tr_bool found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_INT );

    if( child )
        found = tr_bencGetInt( child, setme );

    return found;
}

tr_bool
tr_bencDictFindDouble( tr_benc * dict, const char * key, double * setme )
{
    const char * str;
    const tr_bool success = tr_bencDictFindStr( dict, key, &str );

    if( success && setme )
        *setme = strtod( str, NULL );

    return success;
}

tr_bool
tr_bencDictFindList( tr_benc * dict, const char * key, tr_benc ** setme )
{
    tr_bool found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_LIST );

    if( child )
    {
        if( setme != NULL )
            *setme = child;
        found = TRUE;
    }

    return found;
}

tr_bool
tr_bencDictFindDict( tr_benc * dict, const char * key, tr_benc ** setme )
{
    tr_bool found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_DICT );

    if( child )
    {
        if( setme != NULL )
            *setme = child;
        found = TRUE;
    }

    return found;
}

tr_bool
tr_bencDictFindStr( tr_benc *  dict, const char *  key, const char ** setme )
{
    tr_bool found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_STR );

    if( child )
    {
        if( setme )
            *setme = child->val.s.s;
        found = TRUE;
    }

    return found;
}

tr_bool
tr_bencDictFindRaw( tr_benc         * dict,
                    const char      * key,
                    const uint8_t  ** setme_raw,
                    size_t          * setme_len )
{
    tr_bool found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_STR );

    if( child )
    {
        *setme_raw = (uint8_t*) child->val.s.s;
        *setme_len = child->val.s.i;
        found = TRUE;
    }

    return found;
}

/***
****
***/

void
tr_bencInitRaw( tr_benc *    val,
                const void * src,
                size_t       byteCount )
{
    tr_bencInit( val, TYPE_STR );
    val->val.s.i = byteCount;
    val->val.s.s = tr_memdup( src, byteCount );
}

void
tr_bencInitStr( tr_benc *    val,
                const void * str,
                int          len )
{
    tr_bencInit( val, TYPE_STR );

    val->val.s.s = tr_strndup( str, len );

    if( val->val.s.s == NULL )
        val->val.s.i = 0;
    else if( len < 0 )
        val->val.s.i = strlen( val->val.s.s );
    else
        val->val.s.i = len;
}

void
tr_bencInitInt( tr_benc * val,
                int64_t   num )
{
    tr_bencInit( val, TYPE_INT );
    val->val.i = num;
}

int
tr_bencInitList( tr_benc * val,
                 size_t    reserveCount )
{
    tr_bencInit( val, TYPE_LIST );
    return tr_bencListReserve( val, reserveCount );
}

int
tr_bencListReserve( tr_benc * val,
                    size_t    count )
{
    assert( tr_bencIsList( val ) );
    return makeroom( val, count );
}

int
tr_bencInitDict( tr_benc * val,
                 size_t    reserveCount )
{
    tr_bencInit( val, TYPE_DICT );
    return tr_bencDictReserve( val, reserveCount );
}

int
tr_bencDictReserve( tr_benc * val,
                    size_t    reserveCount )
{
    assert( tr_bencIsDict( val ) );
    return makeroom( val, reserveCount * 2 );
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
    tr_bencInit( item, TYPE_INT );

    return item;
}

tr_benc *
tr_bencListAddInt( tr_benc * list,
                   int64_t   val )
{
    tr_benc * node = tr_bencListAdd( list );

    tr_bencInitInt( node, val );
    return node;
}

tr_benc *
tr_bencListAddStr( tr_benc *    list,
                   const char * val )
{
    tr_benc * node = tr_bencListAdd( list );

    tr_bencInitStr( node, val, -1 );
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
    tr_bencInit( itemval, TYPE_INT );

    return itemval;
}

tr_benc*
tr_bencDictAddInt( tr_benc *    dict,
                   const char * key,
                   int64_t      val )
{
    tr_benc * child;

    /* see if it already exists, and if so, try to reuse it */
    if(( child = tr_bencDictFind( dict, key ))) {
        if( !tr_bencIsInt( child ) ) {
            tr_bencDictRemove( dict, key );
            child = NULL;
        }
    }

    /* if it doesn't exist, create it */
    if( child == NULL )
        child = tr_bencDictAdd( dict, key );

    /* set it */
    tr_bencInitInt( child, val );

    return child;
}

tr_benc*
tr_bencDictAddStr( tr_benc * dict, const char * key, const char * val )
{
    tr_benc * child;

    /* see if it already exists, and if so, try to reuse it */
    if(( child = tr_bencDictFind( dict, key ))) {
        if( tr_bencIsString( child ) )
            tr_free( child->val.s.s );
        else {
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
tr_bencDictAddDouble( tr_benc *    dict,
                      const char * key,
                      double       d )
{
    char buf[128];
    char * locale;

    /* the json spec requires a '.' decimal point regardless of locale */
    locale = tr_strdup( setlocale ( LC_NUMERIC, NULL ) );
    setlocale( LC_NUMERIC, "POSIX" );
    tr_snprintf( buf, sizeof( buf ), "%f", d );
    setlocale( LC_NUMERIC, locale );
    tr_free( locale );

    return tr_bencDictAddStr( dict, key, buf );
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

tr_benc*
tr_bencDictAddRaw( tr_benc *    dict,
                   const char * key,
                   const void * src,
                   size_t       len )
{
    tr_benc * child = tr_bencDictAdd( dict, key );

    tr_bencInitRaw( child, src, len );
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

static struct SaveNode*
nodeNewDict( const tr_benc * val )
{
    int               i, j;
    int               nKeys;
    struct SaveNode * node;
    struct KeyIndex * indices;

    assert( tr_bencIsDict( val ) );

    nKeys = val->val.l.count / 2;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->children = tr_new0( int, nKeys * 2 );

    /* ugh, a dictionary's children have to be sorted by key... */
    indices = tr_new( struct KeyIndex, nKeys );
    for( i = j = 0; i < ( nKeys * 2 ); i += 2, ++j )
    {
        indices[j].key = val->val.l.vals[i].val.s.s;
        indices[j].index = i;
    }
    qsort( indices, j, sizeof( struct KeyIndex ), compareKeyIndex );
    for( i = 0; i < j; ++i )
    {
        const int index = indices[i].index;
        node->children[node->childCount++] = index;
        node->children[node->childCount++] = index + 1;
    }

    assert( node->childCount == nKeys * 2 );
    tr_free( indices );
    return node;
}

static struct SaveNode*
nodeNewList( const tr_benc * val )
{
    int               i, n;
    struct SaveNode * node;

    assert( tr_bencIsList( val ) );

    n = val->val.l.count;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->childCount = n;
    node->children = tr_new0( int, n );
    for( i = 0; i < n; ++i ) /* a list's children don't need to be reordered */
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

    if( tr_bencIsList( val ) )
        node = nodeNewList( val );
    else if( tr_bencIsDict( val ) )
        node = nodeNewDict( val );
    else
        node = nodeNewLeaf( val );

    return node;
}

typedef void ( *BencWalkFunc )( const tr_benc * val, void * user_data );

struct WalkFuncs
{
    BencWalkFunc    intFunc;
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
bencWalk( const tr_benc *    top,
          struct WalkFuncs * walkFuncs,
          void *             user_data )
{
    tr_ptrArray stack = TR_PTR_ARRAY_INIT;

    tr_ptrArrayAppend( &stack, nodeNew( top ) );

    while( !tr_ptrArrayEmpty( &stack ) )
    {
        struct SaveNode * node = tr_ptrArrayBack( &stack );
        const tr_benc *   val;

        if( !node->valIsVisited )
        {
            val = node->val;
            node->valIsVisited = TRUE;
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
            tr_ptrArrayPop( &stack );
            tr_free( node->children );
            tr_free( node );
            continue;
        }

        if( val ) switch( val->type )
            {
                case TYPE_INT:
                    walkFuncs->intFunc( val, user_data );
                    break;

                case TYPE_STR:
                    walkFuncs->stringFunc( val, user_data );
                    break;

                case TYPE_LIST:
                    if( val != node->val )
                        tr_ptrArrayAppend( &stack, nodeNew( val ) );
                    else
                        walkFuncs->listBeginFunc( val, user_data );
                    break;

                case TYPE_DICT:
                    if( val != node->val )
                        tr_ptrArrayAppend( &stack, nodeNew( val ) );
                    else
                        walkFuncs->dictBeginFunc( val, user_data );
                    break;

                default:
                    /* did caller give us an uninitialized val? */
                    tr_err( _( "Invalid metadata" ) );
                    break;
            }
    }

    tr_ptrArrayDestruct( &stack, NULL );
}

/****
*****
****/

static void
saveIntFunc( const tr_benc * val,
             void *          evbuf )
{
    evbuffer_add_printf( evbuf, "i%" PRId64 "e", val->val.i );
}

static void
saveStringFunc( const tr_benc * val,
                void *          vevbuf )
{
    struct evbuffer * evbuf = vevbuf;

    evbuffer_add_printf( evbuf, "%lu:", (unsigned long)val->val.s.i );
    evbuffer_add( evbuf, val->val.s.s, val->val.s.i );
}

static void
saveDictBeginFunc( const tr_benc * val UNUSED,
                   void *              evbuf )
{
    evbuffer_add_printf( evbuf, "d" );
}

static void
saveListBeginFunc( const tr_benc * val UNUSED,
                   void *              evbuf )
{
    evbuffer_add_printf( evbuf, "l" );
}

static void
saveContainerEndFunc( const tr_benc * val UNUSED,
                      void *              evbuf )
{
    evbuffer_add_printf( evbuf, "e" );
}

char*
tr_bencSave( const tr_benc * top,
             int *           len )
{
    char *            ret;
    struct WalkFuncs  walkFuncs;
    struct evbuffer * out = evbuffer_new( );

    walkFuncs.intFunc = saveIntFunc;
    walkFuncs.stringFunc = saveStringFunc;
    walkFuncs.dictBeginFunc = saveDictBeginFunc;
    walkFuncs.listBeginFunc = saveListBeginFunc;
    walkFuncs.containerEndFunc = saveContainerEndFunc;
    bencWalk( top, &walkFuncs, out );

    if( len )
        *len = EVBUFFER_LENGTH( out );
    ret = tr_strndup( EVBUFFER_DATA( out ), EVBUFFER_LENGTH( out ) );
    evbuffer_free( out );
    return ret;
}

/***
****
***/

static void
freeDummyFunc( const tr_benc * val UNUSED,
               void * buf          UNUSED  )
{}

static void
freeStringFunc( const tr_benc * val,
                void *          freeme )
{
    tr_ptrArrayAppend( freeme, val->val.s.s );
}

static void
freeContainerBeginFunc( const tr_benc * val,
                        void *          freeme )
{
    tr_ptrArrayAppend( freeme, val->val.l.vals );
}

void
tr_bencFree( tr_benc * val )
{
    if( val && val->type )
    {
        tr_ptrArray a = TR_PTR_ARRAY_INIT;
        struct WalkFuncs walkFuncs;

        walkFuncs.intFunc = freeDummyFunc;
        walkFuncs.stringFunc = freeStringFunc;
        walkFuncs.dictBeginFunc = freeContainerBeginFunc;
        walkFuncs.listBeginFunc = freeContainerBeginFunc;
        walkFuncs.containerEndFunc = freeDummyFunc;
        bencWalk( val, &walkFuncs, &a );

        tr_ptrArrayDestruct( &a, tr_free );
    }
}

/***
****
***/

struct ParentState
{
    int    bencType;
    int    childIndex;
    int    childCount;
};

struct jsonWalk
{
    tr_list *          parents;
    struct evbuffer *  out;
};

static void
jsonIndent( struct jsonWalk * data )
{
    const int width = tr_list_size( data->parents ) * 4;

    evbuffer_add_printf( data->out, "\n%*.*s", width, width, " " );
}

static void
jsonChildFunc( struct jsonWalk * data )
{
    if( data->parents )
    {
        struct ParentState * parentState = data->parents->data;

        switch( parentState->bencType )
        {
            case TYPE_DICT:
            {
                const int i = parentState->childIndex++;
                if( !( i % 2 ) )
                    evbuffer_add_printf( data->out, ": " );
                else
                {
                    evbuffer_add_printf( data->out, ", " );
                    jsonIndent( data );
                }
                break;
            }

            case TYPE_LIST:
            {
                ++parentState->childIndex;
                evbuffer_add_printf( data->out, ", " );
                jsonIndent( data );
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
jsonStringFunc( const tr_benc * val,
                void *          vdata )
{
    struct jsonWalk *    data = vdata;
    const unsigned char *it, *end;

    evbuffer_add_printf( data->out, "\"" );
    for( it = (const unsigned char*)val->val.s.s, end = it + val->val.s.i;
         it != end; ++it )
    {
        switch( *it )
        {
            case '/':
                evbuffer_add_printf( data->out, "\\/" ); break;

            case '\b':
                evbuffer_add_printf( data->out, "\\b" ); break;

            case '\f':
                evbuffer_add_printf( data->out, "\\f" ); break;

            case '\n':
                evbuffer_add_printf( data->out, "\\n" ); break;

            case '\r':
                evbuffer_add_printf( data->out, "\\r" ); break;

            case '\t':
                evbuffer_add_printf( data->out, "\\t" ); break;

            case '"':
                evbuffer_add_printf( data->out, "\\\"" ); break;

            case '\\':
                evbuffer_add_printf( data->out, "\\\\" ); break;

            default:
                if( isascii( *it ) )
                {
                    /*fprintf( stderr, "[%c]\n", *it );*/
                    evbuffer_add_printf( data->out, "%c", *it );
                }
                else
                {
                    const UTF8 * tmp = it;
                    UTF32        buf = 0;
                    UTF32 *      u32 = &buf;
                    ConversionResult result = ConvertUTF8toUTF32( &tmp, end, &u32, &buf + 1, 0 );
                    if( ( result != conversionOK ) && ( tmp == it ) )
                        ++it; /* it's beyond help; skip it */
                    else {
                        evbuffer_add_printf( data->out, "\\u%04x", buf );
                        it = tmp - 1;
                    }
                    /*fprintf( stderr, "[\\u%04x]\n", buf );*/
                }
        }
    }
    evbuffer_add_printf( data->out, "\"" );
    jsonChildFunc( data );
}

static void
jsonDictBeginFunc( const tr_benc * val,
                   void *          vdata )
{
    struct jsonWalk * data = vdata;

    jsonPushParent( data, val );
    evbuffer_add_printf( data->out, "{" );
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
    evbuffer_add_printf( data->out, "[" );
    if( nChildren )
        jsonIndent( data );
}

static void
jsonContainerEndFunc( const tr_benc * val,
                      void *          vdata )
{
    size_t            i;
    struct jsonWalk * data = vdata;
    char *            str;
    int               emptyContainer = FALSE;

    /* trim out the trailing comma, if any */
    str = (char*) EVBUFFER_DATA( data->out );
    for( i = EVBUFFER_LENGTH( data->out ) - 1; i > 0; --i )
    {
        if( isspace( str[i] ) ) continue;
        if( str[i] == ',' )
            EVBUFFER_LENGTH( data->out ) = i;
        if( str[i] == '{' || str[i] == '[' )
            emptyContainer = TRUE;
        break;
    }

    jsonPopParent( data );
    if( !emptyContainer )
        jsonIndent( data );
    if( tr_bencIsDict( val ) )
        evbuffer_add_printf( data->out, "}" );
    else /* list */
        evbuffer_add_printf( data->out, "]" );
    jsonChildFunc( data );
}

char*
tr_bencSaveAsJSON( const tr_benc * top,
                   int *           len )
{
    char *           ret;
    struct WalkFuncs walkFuncs;
    struct jsonWalk  data;

    data.out = evbuffer_new( );
    data.parents = NULL;

    walkFuncs.intFunc = jsonIntFunc;
    walkFuncs.stringFunc = jsonStringFunc;
    walkFuncs.dictBeginFunc = jsonDictBeginFunc;
    walkFuncs.listBeginFunc = jsonListBeginFunc;
    walkFuncs.containerEndFunc = jsonContainerEndFunc;

    bencWalk( top, &walkFuncs, &data );

    if( EVBUFFER_LENGTH( data.out ) )
        evbuffer_add_printf( data.out, "\n" );
    if( len )
        *len = EVBUFFER_LENGTH( data.out );
    ret = tr_strndup( EVBUFFER_DATA( data.out ), EVBUFFER_LENGTH( data.out ) );
    evbuffer_free( data.out );
    return ret;
}

/***
****
***/

static size_t
tr_bencDictSize( const tr_benc * dict )
{
    size_t count = 0;

    if( tr_bencIsDict( dict ) )
        count = dict->val.l.count / 2;

    return count;
}

static tr_bool
tr_bencDictChild( const tr_benc * dict, size_t n, const char ** key, const tr_benc ** val )
{
    tr_bool success = 0;

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
        const tr_benc * val;

        if( tr_bencDictChild( source, i, &key, &val ) )
        {
            int64_t i64;
            const char * str;
            tr_benc * t;

            if( tr_bencGetInt( val, &i64 ) )
            {
                tr_bencDictRemove( target, key );
                tr_bencDictAddInt( target, key, i64 );
            }
            else if( tr_bencGetStr( val, &str ) )
            {
                tr_bencDictRemove( target, key );
                tr_bencDictAddStr( target, key, str );
            }
            else if( tr_bencIsDict( val ) && tr_bencDictFindDict( target, key, &t ) )
            {
                tr_bencMergeDicts( t, val );
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

static int
saveFile( const char * filename,
          const char * content,
          size_t       len )
{
    int    err = 0;
    FILE * out = NULL;

    out = fopen( filename, "wb+" );

    if( !out )
    {
        err = errno;
        tr_err( _( "Couldn't open \"%1$s\": %2$s" ),
                filename, tr_strerror( errno ) );
    }
    else if( fwrite( content, sizeof( char ), len, out ) != (size_t)len )
    {
        err = errno;
        tr_err( _( "Couldn't save file \"%1$s\": %2$s" ),
               filename, tr_strerror( errno ) );
    }

    if( !err )
        tr_dbg( "tr_bencSaveFile saved \"%s\"", filename );
    if( out )
        fclose( out );
    return err;
}

int
tr_bencSaveFile( const char *    filename,
                 const tr_benc * b )
{
    int       len;
    char *    content = tr_bencSave( b, &len );
    const int err = saveFile( filename, content, len );

    tr_free( content );
    return err;
}

int
tr_bencSaveJSONFile( const char *    filename,
                     const tr_benc * b )
{
    int       len;
    char *    content = tr_bencSaveAsJSON( b, &len );
    const int err = saveFile( filename, content, len );

    tr_free( content );
    return err;
}

/***
****
***/

int
tr_bencLoadFile( const char * filename,
                 tr_benc *    b )
{
    int       err;
    size_t    contentLen;
    uint8_t * content;

    content = tr_loadFile( filename, &contentLen );
    if( !content )
        err = errno;
    else
        err = tr_bencLoad( content, contentLen, b, NULL );

    tr_free( content );
    return err;
}

int
tr_bencLoadJSONFile( const char * filename,
                     tr_benc *    b )
{
    int        err;
    size_t     contentLen;
    uint8_t  * content;

    content = tr_loadFile( filename, &contentLen );
    if( !content )
        err = errno;
    else
        err = tr_jsonParse( content, contentLen, b, NULL );

    tr_free( content );
    return err;
}

