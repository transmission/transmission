// This file Copyright © 2009-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <libtransmission/transmission.h>

@class Torrent;

@interface TrackerNode : NSObject

@property(nonatomic, weak, readonly) Torrent* torrent;

- (instancetype)initWithTrackerView:(tr_tracker_view const*)stat torrent:(Torrent*)torrent;

- (BOOL)isEqual:(id)object;

@property(nonatomic, readonly) NSString* host;
@property(nonatomic, readonly) NSString* fullAnnounceAddress;

@property(nonatomic, readonly) NSInteger tier;

@property(nonatomic, readonly) NSUInteger identifier;

/// -1 if unknown
@property(nonatomic, readonly) NSInteger totalSeeders;
/// -1 if unknown
@property(nonatomic, readonly) NSInteger totalLeechers;
/// -1 if unknown
@property(nonatomic, readonly) NSInteger totalDownloaded;

@property(nonatomic, readonly) NSString* lastAnnounceStatusString;
@property(nonatomic, readonly) NSString* nextAnnounceStatusString;
@property(nonatomic, readonly) NSString* lastScrapeStatusString;

@end
