/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2009 Transmission authors and contributors
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

#import <Cocoa/Cocoa.h>
#import <transmission.h>
#import <Quartz/Quartz.h>

@class Torrent;
@class TrackerTableView;
@class FileOutlineController;
@class PiecesView;

@interface InfoWindowController : NSWindowController <QLPreviewPanelDataSource, QLPreviewPanelDelegate>
{
    NSArray * fTorrents;
    NSMutableArray * fTrackers;
    
    IBOutlet NSView * fInfoView, * fActivityView, * fTrackerView, * fPeersView, * fFilesView, * fOptionsView;
    NSInteger fCurrentTabTag;
    IBOutlet NSMatrix * fTabMatrix;

    IBOutlet NSImageView * fImageView;
    IBOutlet NSTextField * fNameField, * fBasicInfoField;
    
    IBOutlet NSTextField * fTrackerField, * fPiecesField, * fHashField, * fSecureField,
                        * fDataLocationField,
                        * fDateAddedField, * fDateCompletedField, * fDateActivityField,
                        * fCreatorField, * fDateCreatedField,
                        * fStateField, * fProgressField,
                        * fHaveField, * fDownloadedTotalField, * fUploadedTotalField, * fFailedHashField,
                        * fRatioField, * fSwarmSpeedField;
    IBOutlet NSTextView * fCommentView;
    IBOutlet NSButton * fRevealDataButton;
    
    IBOutlet TrackerTableView * fTrackerTable;
    IBOutlet NSSegmentedControl * fTrackerAddRemoveControl;
    IBOutlet NSTextField * fAnnounceAddressField, * fAnnounceLastField, * fAnnounceResponseField, * fAnnounceNextField,
                            * fScrapeAddressField, * fScrapeLastField, * fScrapeResponseField, * fScrapeNextField;
    
    NSArray * fPeers, * fWebSeeds;
    IBOutlet NSTableView * fPeerTable, * fWebSeedTable;
    IBOutlet NSTextField * fConnectedPeersField, * fDownloadingFromField, * fUploadingToField, * fKnownField,
                            * fSeedersField, * fLeechersField, * fCompletedFromTrackerField;
    IBOutlet NSTextView * fErrorMessageView;
    IBOutlet PiecesView * fPiecesView;
    IBOutlet NSSegmentedControl * fPiecesControl;
    CGFloat fWebSeedTableHeight, fSpaceBetweenWebSeedAndPeer;
    NSViewAnimation * fWebSeedTableAnimation;
    
    IBOutlet FileOutlineController * fFileController;
    IBOutlet NSSearchField * fFileFilterField;
    
    IBOutlet NSPopUpButton * fPriorityPopUp, * fRatioPopUp;
    IBOutlet NSButton * fUploadLimitCheck, * fDownloadLimitCheck, * fGlobalLimitCheck;
    IBOutlet NSTextField * fUploadLimitField, * fDownloadLimitField, * fRatioLimitField,
                        * fUploadLimitLabel, * fDownloadLimitLabel, * fPeersConnectLabel,
                        * fPeersConnectField;
    
    NSCache * fTrackerIconCache;
    NSMutableSet * fTrackerIconLoaded;
    
    NSString * fInitialString;
    
    QLPreviewPanel * fPreviewPanel;
}

- (void) setInfoForTorrents: (NSArray *) torrents;
- (void) updateInfoStats;
- (void) updateOptions;

- (void) setTab: (id) sender;

- (void) setNextTab;
- (void) setPreviousTab;

- (void) addRemoveTracker: (id) sender;

- (void) setPiecesView: (id) sender;
- (void) setPiecesViewForAvailable: (BOOL) available;

- (void) revealDataFile: (id) sender;

- (void) setFileFilterText: (id) sender;

- (void) setUseSpeedLimit: (id) sender;
- (void) setSpeedLimit: (id) sender;
- (void) setUseGlobalSpeedLimit: (id) sender;

- (void) setRatioSetting: (id) sender;
- (void) setRatioLimit: (id) sender;

- (void) setPriority: (id) sender;

- (void) setPeersConnectLimit: (id) sender;

@end
