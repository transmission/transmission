// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

@interface InfoWindowController : NSWindowController

@property(nonatomic, readonly) NSArray<NSURL*>* quickLookURLs;
@property(nonatomic, readonly) BOOL canQuickLook;

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents;
- (void)updateInfoStats;
- (void)updateOptions;

- (IBAction)setTab:(id)sender;

- (void)setNextTab;
- (void)setPreviousTab;

- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item;

@end
