// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <libtransmission/transmission.h>

@class RpcController;
@class Torrent;

@protocol RpcControllerDelegate<NSObject>

- (Torrent*)rpcController:(RpcController*)controller torrentForId:(tr_torrent_id_t)torrent_id;
- (void)rpcController:(RpcController*)controller addTorrent:(Torrent*)torrent;
- (void)rpcController:(RpcController*)controller removeTorrent:(Torrent*)torrent deleteData:(BOOL)deleteData;
- (void)rpcControllerDidStartOrStopTorrent:(RpcController*)controller;
- (void)rpcControllerDidChangeTorrent:(RpcController*)controller torrent:(Torrent*)torrent;
- (void)rpcControllerDidMoveTorrent:(RpcController*)controller torrent:(Torrent*)torrent;
- (void)rpcControllerDidUpdateQueue:(RpcController*)controller;
- (void)rpcControllerDidChangeSession:(RpcController*)controller;
- (void)rpcControllerDidRequestSessionClose:(RpcController*)controller;

@end

@interface RpcController : NSObject

- (instancetype)initWithLib:(tr_session*)lib delegate:(id<RpcControllerDelegate>)delegate;

@end
