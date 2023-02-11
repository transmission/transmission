// This file Copyright © 2010-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#import "InfoViewController.h"

@interface InfoFileViewController : NSViewController<InfoViewController>

@property(nonatomic, readonly) NSArray<NSURL*>* quickLookURLs;
@property(nonatomic, readonly) BOOL canQuickLook;

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents;
- (void)updateInfo;

- (void)saveViewSize;

- (IBAction)setFileFilterText:(id)sender;
- (IBAction)checkAll:(id)sender;
- (IBAction)uncheckAll:(id)sender;

- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item;

@end
