/******************************************************************************
 * Copyright (c) 2010-2012 Transmission authors and contributors
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

#import "InfoViewController.h"

@class PiecesView;
@class Torrent;

@interface InfoActivityViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    IBOutlet NSTextField* fDateAddedField;
    IBOutlet NSTextField* fDateCompletedField;
    IBOutlet NSTextField* fDateActivityField;
    IBOutlet NSTextField* fStateField;
    IBOutlet NSTextField* fProgressField;
    IBOutlet NSTextField* fHaveField;
    IBOutlet NSTextField* fDownloadedTotalField;
    IBOutlet NSTextField* fUploadedTotalField;
    IBOutlet NSTextField* fFailedHashField;
    IBOutlet NSTextField* fRatioField;
    IBOutlet NSTextField* fDownloadTimeField;
    IBOutlet NSTextField* fSeedTimeField;
    IBOutlet NSTextView* fErrorMessageView;

    IBOutlet PiecesView* fPiecesView;
    IBOutlet NSSegmentedControl* fPiecesControl;

    //remove when we switch to auto layout on 10.7
    IBOutlet NSTextField* fTransferSectionLabel;
    IBOutlet NSTextField* fDatesSectionLabel;
    IBOutlet NSTextField* fTimeSectionLabel;
    IBOutlet NSTextField* fStateLabel;
    IBOutlet NSTextField* fProgressLabel;
    IBOutlet NSTextField* fHaveLabel;
    IBOutlet NSTextField* fDownloadedLabel;
    IBOutlet NSTextField* fUploadedLabel;
    IBOutlet NSTextField* fFailedDLLabel;
    IBOutlet NSTextField* fRatioLabel;
    IBOutlet NSTextField* fErrorLabel;
    IBOutlet NSTextField* fDateAddedLabel;
    IBOutlet NSTextField* fDateCompletedLabel;
    IBOutlet NSTextField* fDateActivityLabel;
    IBOutlet NSTextField* fDownloadTimeLabel;
    IBOutlet NSTextField* fSeedTimeLabel;
    IBOutlet NSScrollView* fErrorScrollView;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

- (IBAction)setPiecesView:(id)sender;
- (IBAction)updatePiecesView:(id)sender;
- (void)clearView;

@end
