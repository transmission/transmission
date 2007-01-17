/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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
#import "Torrent.h"
#import "PiecesView.h"
#import <transmission.h>

@interface InfoWindowController : NSWindowController
{
    NSArray * fTorrents;
    NSArray * fPeers, * fFiles;
    NSImage * fAppIcon, * fDotGreen, * fDotRed;
    
    IBOutlet NSTabView * fTabView;

    IBOutlet NSImageView * fImageView;
    IBOutlet NSTextField * fNameField, * fSizeField, * fTrackerField,
                        * fPiecesField, * fHashField, * fSecureField,
                        * fTorrentLocationField, * fDataLocationField,
                        * fDateStartedField,
                        * fCreatorField, * fDateCreatedField,
                        * fStateField,
                        * fDownloadedValidField, * fDownloadedTotalField, * fUploadedTotalField,
                        * fRatioField, * fSwarmSpeedField;
    IBOutlet NSTextView * fCommentView;
    IBOutlet NSButton * fRevealDataButton, * fRevealTorrentButton;

    IBOutlet NSTableView * fPeerTable;
    IBOutlet NSTextField * fSeedersField, * fLeechersField, * fConnectedPeersField,
                        * fDownloadingFromField, * fUploadingToField, * fCompletedFromTrackerField;
    IBOutlet NSTextView * fErrorMessageView;
    IBOutlet PiecesView * fPiecesView;
    
    IBOutlet NSTableView * fFileTable;
    IBOutlet NSTextField * fFileTableStatusField;
    
    IBOutlet NSButton * fRatioCustomCheck, * fRatioStopCheck,
                    * fLimitCustomCheck, * fUploadLimitCheck, * fDownloadLimitCheck;
    IBOutlet NSTextField * fUploadLimitField, * fDownloadLimitField, * fRatioLimitField,
                        * fUploadLimitLabel, * fDownloadLimitLabel;
}

- (void) updateInfoForTorrents: (NSArray *) torrents;
- (void) updateInfoStats;

- (void) setNextTab;
- (void) setPreviousTab;

- (void) revealTorrentFile: (id) sender;
- (void) revealDataFile: (id) sender;
- (void) revealFile: (id) sender;

- (void) setLimitCustom: (id) sender;
- (void) setLimitCheck: (id) sender;
- (void) setSpeedLimit: (id) sender;

- (void) setRatioCustom: (id) sender;
- (void) setRatioSetting: (id) sender;
- (void) setRatioLimit: (id) sender;

@end
