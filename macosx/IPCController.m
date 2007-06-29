/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "bencode.h"
#include "ipcparse.h"
#include "transmission.h"

#import "IPCController.h"
#import "Torrent.h"

static void
getaddr( struct sockaddr_un * );
static NSArray *
bencarray( benc_val_t * val, int type );
static void
msg_lookup   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
static void
msg_info     ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_infoall  ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_action   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_actionall( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_addold   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_addnew   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_getbool  ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_getint   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_getstr   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_setbool  ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_setint   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_setstr   ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_empty    ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
void
msg_sup      ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );
static void
msg_default  ( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg );

@interface IPCClient : NSObject
{
    NSFileHandle    * _handle;
    struct ipc_info * _ipc;
    IPCController   * _controller;
    NSMutableData   * _buf;
}

- (id)              initClient: (IPCController *) controller
                         funcs: (struct ipc_funcs *) funcs
                        handle: (NSFileHandle *) handle;
- (IPCController *) controller;
- (struct ipc_info *)      ipc;
- (void)               gotdata: (NSNotification *) notification;
- (BOOL)              sendresp: (uint8_t *) buf
                          size: (size_t) size;
- (BOOL)         sendrespEmpty: (enum ipc_msg) msgid
                           tag: (int64_t) tag;
- (BOOL)           sendrespInt: (enum ipc_msg) msgid
                           tag: (int64_t) tag
                           val: (int64_t) val;
- (BOOL)           sendrespStr: (enum ipc_msg) msgid
                           tag: (int64_t) tag
                           val: (NSString *) val;
- (void)          sendrespInfo: (enum ipc_msg) respid
                           tag: (int64_t) tag
                      torrents: (NSArray *) tors
                         types: (int) types;

@end

@interface IPCController (Private)

- (void)  newclient: (NSNotification *) notification;
- (void) killclient: (IPCClient *) client;

@end

@implementation IPCController

- (id) init
{
    struct sockaddr_un sun;

    self = [super init];
    if( nil == self )
        return nil;

    getaddr( &sun );
    unlink( sun.sun_path );
    _sock    = [[NSSocketPort alloc]
                   initWithProtocolFamily: PF_UNIX
                               socketType: SOCK_STREAM
                                 protocol: 0
                                  address: [NSData dataWithBytes: &sun
                                                          length: sizeof(sun)]];
    _listen  = [[NSFileHandle alloc]
                   initWithFileDescriptor: [_sock socket]
                           closeOnDealloc: YES];
    _funcs   = ipc_initmsgs();
    _clients = [[NSMutableArray alloc] init];

    /* XXX is this error checking bogus? */
    if( nil  == _sock    ||
        nil  == _listen  ||
        NULL == _funcs   ||
        nil  == _clients ||
        0 > ipc_addmsg( _funcs, IPC_MSG_ADDMANYFILES, msg_addold    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_ADDONEFILE,   msg_addnew    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_AUTOMAP,      msg_setbool   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_AUTOSTART,    msg_setbool   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_DIR,          msg_setstr    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_DOWNLIMIT,    msg_setint    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETAUTOMAP,   msg_getbool   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETAUTOSTART, msg_getbool   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETDIR,       msg_getstr    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETDOWNLIMIT, msg_getint    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETINFO,      msg_info      ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETINFOALL,   msg_infoall   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETPEX,       msg_getbool   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETPORT,      msg_getint    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETSTAT,      msg_info      ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETSTATALL,   msg_infoall   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_GETUPLIMIT,   msg_getint    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_LOOKUP,       msg_lookup    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_NOOP,         msg_empty     ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_PEX,          msg_setbool   ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_PORT,         msg_setint    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_QUIT,         msg_empty     ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_REMOVE,       msg_action    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_REMOVEALL,    msg_actionall ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_START,        msg_action    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_STARTALL,     msg_actionall ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_STOP,         msg_action    ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_STOPALL,      msg_actionall ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_SUP,          msg_sup       ) ||
        0 > ipc_addmsg( _funcs, IPC_MSG_UPLIMIT,      msg_setint    ) )
    {
        [self release];
        return nil;
    }
    ipc_setdefmsg( _funcs, msg_default );

    [[NSNotificationCenter defaultCenter]
        addObserver: self
           selector: @selector(newclient:)
               name: NSFileHandleConnectionAcceptedNotification
             object: _listen];
    [_listen acceptConnectionInBackgroundAndNotify];

    return self;
}

- (void) dealloc
{
    struct sockaddr_un sun;

    [[NSNotificationCenter defaultCenter] removeObserver: self];
    [_listen  release];
    [_sock    release];
    [_clients release];
    ipc_freemsgs( _funcs );
    getaddr( &sun );
    unlink( sun.sun_path );
    [super dealloc];
}

- (id) delegate
{
    return _delegate;
}

- (void) setDelegate: (id) newdelegate
{
    _delegate = newdelegate;
}

@end

@implementation IPCController (Private)

- (void) newclient: (NSNotification *) notification
{
    NSDictionary * info;
    NSFileHandle * handle;
    NSNumber     * error;
    IPCClient    * client;

    info   = [notification userInfo];
    handle = [info objectForKey: NSFileHandleNotificationFileHandleItem];
    error  = [info objectForKey: @"NSFileHandleError"];

    if( nil != error )
    {
        NSLog( @"Failed to accept IPC socket connection: %@", error );
        return;
    }

    [_listen acceptConnectionInBackgroundAndNotify];

    if( nil == handle )
        return;

    client = [[IPCClient alloc]
                 initClient: self
                      funcs: _funcs
                     handle: handle];
    if( nil == client )
        return;

    [_clients addObject:client];
    [client release];
}

- (void) killclient: (IPCClient *) client
{
    [_clients removeObject: client];
}

@end

@implementation IPCClient

- (id) initClient: (IPCController *) controller
            funcs: (struct ipc_funcs *) funcs
           handle: (NSFileHandle *) handle
{
    uint8_t * buf;
    size_t    size;

    self = [super init];
    if( nil == self )
        return nil;

    _handle     = [handle retain];
    _ipc        = ipc_newcon( funcs );
    _controller = controller;
    _buf        = [[NSMutableData alloc] init];

    buf = ipc_mkvers( &size, "Transmission MacOS X " VERSION_STRING );
    if( NULL == _ipc || nil == _buf || NULL == buf ||
        ![self sendresp: buf size: size] )
    {
        [self release];
        return nil;
    }

    [[NSNotificationCenter defaultCenter]
        addObserver: self
           selector: @selector(gotdata:)
               name: NSFileHandleReadCompletionNotification
             object: _handle];
    [_handle readInBackgroundAndNotify];

    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    [_handle release];
    [_buf    release];
    ipc_freecon( _ipc );
    [super   dealloc];
}

- (IPCController *) controller
{
    return _controller;
}

- (struct ipc_info *) ipc
{
    return _ipc;
}

- (void) gotdata: (NSNotification *) notification
{
    NSDictionary * info;
    NSData       * data;
    NSNumber     * error;
    ssize_t        res;

    info  = [notification userInfo];
    data  = [info objectForKey: NSFileHandleNotificationDataItem];
    error = [info objectForKey: @"NSFileHandleError"];

    if( nil != error )
    {
        NSLog( @"Failed to read from IPC socket connection: %@", error );
        [_controller killclient: self];
        return;
    }

    if( nil == data || 0 == [data length] )
    {
        [_controller killclient: self];
        return;
    }

    [_handle readInBackgroundAndNotify];
    [_buf appendData: data];

    if( IPC_MIN_MSG_LEN > [_buf length] )
        return;

    res = ipc_parse( _ipc, [_buf mutableBytes], [_buf length], self );

    if( 0 > res )
    {
        switch( errno )
        {
            case EPERM:
                NSLog( @"IPC client has unsupported protocol version" );
                break;
            case EINVAL:
                NSLog( @"IPC protocol parse error" );
                break;
            default:
                NSLog( @"IPC parsing failed" );
                break;
        }
        [_controller killclient: self];
        return;
    }
    else if( 0 < res )
    {
        assert( res <= [_buf length]);
        if( res < [_buf length])
        {
            memmove( [_buf mutableBytes], [_buf bytes] + res,
                     [_buf length] - res );
        }
        [_buf setLength: ([_buf length] - res)];
    }
}

- (BOOL) sendresp: (uint8_t *) buf
             size: (size_t) size
{
    @try
    {
        [_handle writeData: [NSData dataWithBytesNoCopy: buf
                                                 length: size
                                           freeWhenDone: YES]];
        return YES;
    }
    @catch( NSException * ex )
    {
        NSLog( @"Failed to write to IPC socket connection" );
        return NO;
    }
}

- (BOOL) sendrespEmpty: (enum ipc_msg) msgid
                   tag: (int64_t) tag
{
    uint8_t * buf;
    size_t    size;

    buf = ipc_mkempty( _ipc, &size, msgid, tag );
    if( NULL == buf )
        return FALSE;

    return [self sendresp: buf
                     size: size];
}

- (BOOL) sendrespInt: (enum ipc_msg) msgid
                 tag: (int64_t) tag
                 val: (int64_t) val
{
    uint8_t * buf;
    size_t    size;

    buf = ipc_mkint( _ipc, &size, msgid, tag, val );
    if( NULL == buf )
        return FALSE;

    return [self sendresp: buf
                     size: size];
}

- (BOOL) sendrespStr: (enum ipc_msg) msgid
                 tag: (int64_t) tag
                 val: (NSString *) val
{
    uint8_t       * buf;
    size_t          size;
    NSData        * data;
    NSMutableData * sucky;

    if( [val canBeConvertedToEncoding: NSUTF8StringEncoding] )
        buf = ipc_mkstr( _ipc, &size, msgid, tag,
                         [val cStringUsingEncoding: NSUTF8StringEncoding] );
    else
    {
        data = [val dataUsingEncoding: NSUTF8StringEncoding
                 allowLossyConversion: YES];
        /* XXX this sucks, I should add a length argument to ipc_mkstr() */
        sucky = [NSMutableData dataWithData: data];
        [sucky appendBytes: "" length: 1];
        buf = ipc_mkstr( _ipc, &size, msgid, tag, [sucky bytes] );
    }
    if( NULL == buf )
        return FALSE;

    return [self sendresp: buf
                     size: size];
}

- (void) sendrespInfo: (enum ipc_msg) respid
                  tag: (int64_t) tag
             torrents: (NSArray *) tors
                types: (int) types
{
    benc_val_t     packet, * pkinf;
    NSEnumerator * enumerator;
    Torrent      * tor;
    uint8_t      * buf;
    size_t         size;
    int            res;

    pkinf = ipc_initval( _ipc, respid, tag, &packet, TYPE_LIST );
    if( NULL == pkinf )
        goto fail;
    if( tr_bencListReserve( pkinf, [tors count] ) )
    {
        tr_bencFree( &packet );
        goto fail;
    }

    enumerator = [tors objectEnumerator];
    while( nil != ( tor = [enumerator nextObject] ) )
    {
        if( IPC_MSG_INFO == respid )
            res = ipc_addinfo( pkinf, [tor torrentID], [tor torrentInfo], types );
        else
            res = ipc_addstat( pkinf, [tor torrentID], [tor torrentStat], types );
        if( 0 > res )
        {
            tr_bencFree( &packet );
            goto fail;
        }
    }

    buf = ipc_mkval( &packet, &size );
    tr_bencFree( &packet );
    if( NULL == buf )
        goto fail;
    [self sendresp: buf size: size ];
    return;

  fail:
    NSLog( @"Failed to create IPC reply packet" );
    [_controller killclient: self];
}


@end

void
getaddr( struct sockaddr_un * sun )
{
    bzero( sun, sizeof *sun );
    sun->sun_family = AF_LOCAL;
    strlcpy( sun->sun_path, tr_getPrefsDirectory(), sizeof sun->sun_path );
    strlcat( sun->sun_path, "/socket", sizeof sun->sun_path );
}

NSArray *
bencarray( benc_val_t * val, int type )
{
    int              ii;
    NSMutableArray * ret;
    benc_val_t     * item;

    assert( TYPE_STR == type || TYPE_INT == type );

    if( NULL == val || TYPE_LIST != val->type )
        return nil;

    ret = [NSMutableArray arrayWithCapacity: val->val.l.count];
    for( ii = 0; ii < val->val.l.count; ii++ )
    {
        item = &val->val.l.vals[ii];
        if( type != item->type )
            return nil;
        if( TYPE_STR == type )
        {
            [ret addObject: [NSString stringWithCString: item->val.s.s
                                               encoding: NSUTF8StringEncoding]];
        }
        else
        {
            [ret addObject: [NSNumber numberWithLongLong: (long long) item->val.i]];
        }
    }

    return ret;
}

void
msg_lookup( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient      * client = arg;
    NSArray        * hashes, * tors;
    benc_val_t       packet, * pkinf;
    NSEnumerator   * enumerator;
    Torrent        * tor;
    uint8_t        * buf;
    size_t           size;

    hashes = bencarray( val, TYPE_STR );
    if( NULL == hashes )
    {
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    tors  = [[[client controller] delegate] ipcGetTorrentsByHash: hashes];
    [client sendrespInfo: IPC_MSG_INFO
                     tag: tag
                torrents: tors
                   types: IPC_INF_HASH];
}

void
msg_info( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient  * client = arg;
    enum ipc_msg respid;
    benc_val_t * typesval;
    int          types;
    NSArray    * ids, * tors;

    if( NULL == val || TYPE_DICT != val->type )
        goto bad;

    typesval = tr_bencDictFind( val, "type" );
    if( NULL == typesval || TYPE_LIST != typesval->type ||
        nil == ( ids = bencarray( tr_bencDictFind( val, "id" ), TYPE_INT ) ) )
        goto bad;

    respid = ( IPC_MSG_GETINFO == msgid ? IPC_MSG_INFO : IPC_MSG_STAT );
    tors   = [[[client controller] delegate] ipcGetTorrentsByID: ids];
    types  = ipc_infotypes( respid, typesval );
    [client sendrespInfo: respid tag: tag torrents: tors types: types];
    return;

  bad:
    NSLog( @"Got bad IPC packet" );
    [client sendrespEmpty: IPC_MSG_BAD tag: tag];
}

void
msg_infoall( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient  * client = arg;
    enum ipc_msg respid;
    int          types;
    NSArray    * tors;

    if( NULL == val || TYPE_LIST != val->type )
    {
        NSLog( @"Got bad IPC packet" );
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    respid = ( IPC_MSG_GETINFOALL == msgid ? IPC_MSG_INFO : IPC_MSG_STAT );
    tors   = [[[client controller] delegate] ipcGetTorrentsByID: nil];
    types  = ipc_infotypes( respid, val );
    [client sendrespInfo: respid tag: tag torrents: tors types: types];
}

void
msg_action( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;
    BOOL        res;
    NSArray   * ids, * tors;
    id          delegate;

    ids = bencarray( val, TYPE_INT );
    if( nil == ids )
    {
        NSLog( @"Got bad IPC packet" );
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    delegate = [[client controller] delegate];
    tors     = [delegate ipcGetTorrentsByID: ids];
    switch( msgid )
    {
        case IPC_MSG_REMOVE:
            res = [delegate ipcRemoveTorrents: tors];
            break;
        case IPC_MSG_START:
            res = [delegate ipcStartTorrents: tors];
            break;
        case IPC_MSG_STOP:
            res = [delegate ipcStopTorrents: tors];
            break;
        default:
            assert( 0 );
            return;
    }

    if( res )
        [client sendrespEmpty: IPC_MSG_OK tag: tag];
    else
        [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
}

void
msg_actionall( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;
    BOOL        res;
    NSArray   * tors;
    id          delegate;

    delegate = [[client controller] delegate];
    tors     = [delegate ipcGetTorrentsByID: nil];
    switch( msgid )
    {
        case IPC_MSG_REMOVEALL:
            res = [delegate ipcRemoveTorrents: tors];
            break;
        case IPC_MSG_STARTALL:
            res = [delegate ipcStartTorrents: tors];
            break;
        case IPC_MSG_STOPALL:
            res = [delegate ipcStopTorrents: tors];
            break;
        default:
            assert( 0 );
            return;
    }

    if( res )
        [client sendrespEmpty: IPC_MSG_OK tag: tag];
    else
        [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
}

void
msg_addold( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;
    NSArray   * paths;
    BOOL        res;

    paths = bencarray( val, TYPE_STR );
    if( nil == paths )
    {
        NSLog( @"Got bad IPC packet" );
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    /* XXX should send back info message with torrent IDs */
    if( [[[client controller] delegate] ipcAddTorrents: paths] )
        [client sendrespEmpty: IPC_MSG_OK tag: tag];
    else
        [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
}

void
msg_addnew( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient  * client = arg;
    benc_val_t * fileval, * dataval, * dirval, * autoval;
    NSString   * file, * dir;
    NSData     * data;
    BOOL         autobool, res;

    if( NULL == val || TYPE_DICT != val->type )
        goto bad;

    fileval = tr_bencDictFind( val, "file" );
    dataval = tr_bencDictFind( val, "data" );
    dirval  = tr_bencDictFind( val, "directory" );
    autoval = tr_bencDictFind( val, "autostart" );
    if( ( ( NULL == fileval || TYPE_STR != fileval->type )   &&
          ( NULL == dataval || TYPE_STR != dataval->type ) ) ||
        ( ( NULL != fileval && TYPE_STR == fileval->type )   &&
          ( NULL != dataval && TYPE_STR == dataval->type ) ) ||
          ( NULL != dirval  && TYPE_STR != dirval->type  )   ||
          ( NULL != autoval && TYPE_INT != autoval->type ) )
        goto bad;

    dir = ( NULL == dirval ? nil :
            [NSString stringWithCString: dirval->val.s.s
                               encoding: NSUTF8StringEncoding] );
    autobool = ( NULL == autoval || 0 == autoval->val.i ? NO : YES );

    if( NULL != fileval )
    {
        file = [NSString stringWithCString: fileval->val.s.s
                                  encoding: NSUTF8StringEncoding];
        if( NULL == autoval )
            res = [[[client controller] delegate]
                      ipcAddTorrentFile: file
                              directory: dir];
        else
            res = [[[client controller] delegate]
                      ipcAddTorrentFileAutostart: file
                                       directory: dir
                                       autostart: autobool];
    }
    else
    {
        data = [NSData dataWithBytes: dataval->val.s.s
                              length: dataval->val.s.i];
        if( NULL == autoval )
            res = [[[client controller] delegate]
                      ipcAddTorrentData: data
                              directory: dir];
        else
            res = [[[client controller] delegate]
                      ipcAddTorrentDataAutostart: data
                                       directory: dir
                                       autostart: autobool];
    }

    /* XXX should send back info message with torrent ID */
    if( res )
        [client sendrespEmpty: IPC_MSG_OK tag: tag];
    else
        [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
    return;

  bad:
    NSLog( @"Got bad IPC packet" );
    [client sendrespEmpty: IPC_MSG_BAD tag: tag];
}

void
msg_getbool( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    switch( msgid )
    {
        case IPC_MSG_GETAUTOMAP:
        case IPC_MSG_GETAUTOSTART:
        case IPC_MSG_GETPEX:
            [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_getint( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    switch( msgid )
    {
        case IPC_MSG_GETDOWNLIMIT:
        case IPC_MSG_GETPORT:
        case IPC_MSG_GETUPLIMIT:
            [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_getstr( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    switch( msgid )
    {
        case IPC_MSG_GETDIR:
            [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_setbool( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    if( NULL == val || TYPE_INT != val->type )
    {
        NSLog( @"Got bad IPC packet" );
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    switch( msgid )
    {
        case IPC_MSG_AUTOMAP:
        case IPC_MSG_AUTOSTART:
        case IPC_MSG_PEX:
            [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_setint( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    if( NULL == val || TYPE_INT != val->type )
    {
        NSLog( @"Got bad IPC packet" );
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    switch( msgid )
    {
        case IPC_MSG_DOWNLIMIT:
        case IPC_MSG_PORT:
        case IPC_MSG_UPLIMIT:
            [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_setstr( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    if( NULL == val || TYPE_STR != val->type )
    {
        NSLog( @"Got bad IPC packet" );
        [client sendrespEmpty: IPC_MSG_BAD tag: tag];
        return;
    }

    switch( msgid )
    {
        case IPC_MSG_DIR:
            [client sendrespEmpty: IPC_MSG_FAIL tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_empty( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    switch( msgid )
    {
        case IPC_MSG_NOOP:
            [client sendrespEmpty: IPC_MSG_OK tag: tag];
            break;
        case IPC_MSG_QUIT:
            [[[client controller] delegate] ipcQuit];
            [client sendrespEmpty: IPC_MSG_OK tag: tag];
            break;
        default:
            assert( 0 );
            break;
    }
}

void
msg_sup( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient       * client = arg;
    benc_val_t        packet, * pkval, * name;
    struct ipc_info * ipc;
    int               ii;
    enum ipc_msg      found;
    uint8_t         * buf;
    size_t            size;

    if( NULL == val || TYPE_LIST != val->type )
        goto bad;

    ipc   = [client ipc];
    pkval = ipc_initval( ipc, IPC_MSG_SUP, tag, &packet, TYPE_LIST );
    if( NULL == pkval )
        goto fail;
    if( tr_bencListReserve( pkval, val->val.l.count ) )
    {
        tr_bencFree( &packet );
        goto fail;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        name = &val->val.l.vals[ii];
        if( NULL == name || TYPE_STR != name->type )
            goto bad;
        found = ipc_msgid( ipc, name->val.s.s );
        if( IPC__MSG_COUNT == found || !ipc_ishandled( ipc, found ) )
        {
            continue;
        }
        tr_bencInitStr( tr_bencListAdd( pkval ),
                        name->val.s.s, name->val.s.i, 1 );
    }

    buf = ipc_mkval( &packet, &size );
    tr_bencFree( &packet );
    if( NULL == buf )
        goto fail;
    [client sendresp: buf size: size ];
    return;

  bad:
    NSLog( @"Got bad IPC packet" );
    [client sendrespEmpty: IPC_MSG_BAD tag: tag];
    return;

  fail:
    NSLog( @"Failed to create IPC reply packet" );
    [[client controller] killclient: client];
}

void
msg_default( enum ipc_msg msgid, benc_val_t * val, int64_t tag, void * arg )
{
    IPCClient * client = arg;

    [client sendrespEmpty: IPC_MSG_NOTSUP tag: tag];
}
