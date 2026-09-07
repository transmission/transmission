// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <optional>

#include <libtransmission/string-utils.h>
#include <libtransmission/transmission.h>

#import "RpcController.h"

#import "GroupsController.h"
#import "Torrent.h"

@interface RpcController ()

@property(nonatomic, readonly) tr_session* fLib;
@property(nonatomic, weak) id<RpcControllerDelegate> delegate;

@end

@implementation RpcController

- (instancetype)initWithLib:(tr_session*)lib delegate:(id<RpcControllerDelegate>)delegate
{
    if ((self = [super init]))
    {
        _fLib = lib;
        _delegate = delegate;

        tr_sessionSetRPCCallback(
            _fLib,
            [controller = self](tr_rpc_callback_type const type, std::optional<tr_torrent_id_t> const torrent_id)
            {
                [controller rpcCallback:type forTorrentId:torrent_id];
                return TR_RPC_NOREMOVE; // We'll do the remove manually.
            });
    }

    return self;
}

- (void)rpcCallback:(tr_rpc_callback_type)type forTorrentId:(std::optional<tr_torrent_id_t>)torrent_id
{
    @autoreleasepool
    {
        dispatch_async(dispatch_get_main_queue(), ^{
            Torrent* torrent = nil;
            if (torrent_id.has_value() && (type != TR_RPC_TORRENT_ADDED && type != TR_RPC_SESSION_CHANGED && type != TR_RPC_SESSION_CLOSE))
            {
                torrent = [self.delegate rpcController:self torrentForId:*torrent_id];

                if (!torrent)
                {
                    NSLog(@"No torrent found matching the given torrent id from the RPC callback!");
                    return;
                }
            }

            switch (type)
            {
            case TR_RPC_TORRENT_ADDED:
                if (torrent_id.has_value())
                {
                    if (auto* const torrentStruct = tr_torrentFindFromId(self.fLib, *torrent_id); torrentStruct != nullptr)
                    {
                        [self addTorrentWithStruct:torrentStruct];
                    }
                }
                break;

            case TR_RPC_TORRENT_STARTED:
            case TR_RPC_TORRENT_STOPPED:
                [self startedStoppedTorrent:torrent];
                break;

            case TR_RPC_TORRENT_REMOVING:
                [self.delegate rpcController:self removeTorrent:torrent deleteData:NO];
                break;

            case TR_RPC_TORRENT_TRASHING:
                [self.delegate rpcController:self removeTorrent:torrent deleteData:YES];
                break;

            case TR_RPC_TORRENT_CHANGED:
                [self changedTorrent:torrent];
                break;

            case TR_RPC_TORRENT_MOVED:
                [self movedTorrent:torrent];
                break;

            case TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED:
                [self.delegate rpcControllerDidUpdateQueue:self];
                break;

            case TR_RPC_SESSION_CHANGED:
                [self.delegate rpcControllerDidChangeSession:self];
                break;

            case TR_RPC_SESSION_CLOSE:
                [self.delegate rpcControllerDidRequestSessionClose:self];
                break;

            default:
                NSAssert1(NO, @"Unknown RPC command received: %d", type);
            }
        });
    }
}

- (void)addTorrentWithStruct:(struct tr_torrent*)torrentStruct
{
    NSString* location = tr_strv_to_utf8_nsstring(tr_torrentGetDownloadDir(torrentStruct));

    Torrent* torrent = [[Torrent alloc] initWithTorrentStruct:torrentStruct location:location lib:self.fLib];

    // Change the location if the group calls for it (this has to wait until after the torrent is created).
    if ([GroupsController.groups usesCustomDownloadLocationForIndex:torrent.groupValue])
    {
        location = [GroupsController.groups customDownloadLocationForIndex:torrent.groupValue];
        [torrent changeDownloadFolderBeforeUsing:location determinationType:TorrentDeterminationAutomatic];
    }

    [torrent update];
    [self.delegate rpcController:self addTorrent:torrent];
}

- (void)startedStoppedTorrent:(Torrent*)torrent
{
    [torrent update];

    [self.delegate rpcControllerDidStartOrStopTorrent:self];
}

- (void)changedTorrent:(Torrent*)torrent
{
    [torrent update];

    [self.delegate rpcControllerDidChangeTorrent:self torrent:torrent];
}

- (void)movedTorrent:(Torrent*)torrent
{
    [torrent update];
    [torrent updateTimeMachineExclude];

    [self.delegate rpcControllerDidMoveTorrent:self torrent:torrent];
}

@end
