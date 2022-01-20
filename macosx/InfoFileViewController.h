// This file Copyright © 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>
#import <Quartz/Quartz.h>

#import "InfoViewController.h"

@class FileOutlineController;

@interface InfoFileViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    IBOutlet FileOutlineController* fFileController;

    IBOutlet NSSearchField* fFileFilterField;
    IBOutlet NSButton* fCheckAllButton;
    IBOutlet NSButton* fUncheckAllButton;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

- (void)saveViewSize;

- (IBAction)setFileFilterText:(id)sender;
- (IBAction)checkAll:(id)sender;
- (IBAction)uncheckAll:(id)sender;

@property(nonatomic, readonly) NSArray* quickLookURLs;
@property(nonatomic, readonly) BOOL canQuickLook;
- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item;

@end
