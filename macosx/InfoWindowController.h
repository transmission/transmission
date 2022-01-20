// This file Copyright © 2006-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>
#import <Quartz/Quartz.h>

@protocol InfoViewController;
@class InfoGeneralViewController;
@class InfoActivityViewController;
@class InfoTrackersViewController;
@class InfoPeersViewController;
@class InfoFileViewController;
@class InfoOptionsViewController;

@interface InfoWindowController : NSWindowController
{
    NSArray* fTorrents;

    CGFloat fMinWindowWidth;

    NSViewController<InfoViewController>* fViewController;
    NSInteger fCurrentTabTag;
    IBOutlet NSSegmentedControl* fTabs;

    InfoGeneralViewController* fGeneralViewController;
    InfoActivityViewController* fActivityViewController;
    InfoTrackersViewController* fTrackersViewController;
    InfoPeersViewController* fPeersViewController;
    InfoFileViewController* fFileViewController;
    InfoOptionsViewController* fOptionsViewController;

    IBOutlet NSImageView* fImageView;
    IBOutlet NSTextField* fNameField;
    IBOutlet NSTextField* fBasicInfoField;
    IBOutlet NSTextField* fNoneSelectedField;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfoStats;
- (void)updateOptions;

- (void)setTab:(id)sender;

- (void)setNextTab;
- (void)setPreviousTab;

@property(nonatomic, readonly) NSArray* quickLookURLs;
@property(nonatomic, readonly) BOOL canQuickLook;
- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item;

@end
