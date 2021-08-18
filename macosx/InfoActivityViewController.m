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

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> //tr_getRatio()

#import "InfoActivityViewController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "PiecesView.h"
#import "Torrent.h"

#define PIECES_CONTROL_PROGRESS 0
#define PIECES_CONTROL_AVAILABLE 1

@interface InfoActivityViewController (Private)

- (void)setupInfo;

@end

@implementation InfoActivityViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoActivityView" bundle:nil]))
    {
        self.title = NSLocalizedString(@"Activity", "Inspector view -> title");
    }

    return self;
}

- (void)awakeFromNib
{
    [fTransferSectionLabel sizeToFit];
    [fDatesSectionLabel sizeToFit];
    [fTimeSectionLabel sizeToFit];

    NSArray* labels = @[
        fStateLabel,
        fProgressLabel,
        fHaveLabel,
        fDownloadedLabel,
        fUploadedLabel,
        fFailedDLLabel,
        fRatioLabel,
        fErrorLabel,
        fDateAddedLabel,
        fDateCompletedLabel,
        fDateActivityLabel,
        fDownloadTimeLabel,
        fSeedTimeLabel
    ];

    CGFloat oldMaxWidth = 0.0, originX, newMaxWidth = 0.0;
    for (NSTextField* label in labels)
    {
        NSRect const oldFrame = label.frame;
        if (oldFrame.size.width > oldMaxWidth)
        {
            oldMaxWidth = oldFrame.size.width;
            originX = oldFrame.origin.x;
        }

        [label sizeToFit];
        CGFloat const newWidth = label.bounds.size.width;
        if (newWidth > newMaxWidth)
        {
            newMaxWidth = newWidth;
        }
    }

    for (NSTextField* label in labels)
    {
        NSRect frame = label.frame;
        frame.origin.x = originX + (newMaxWidth - frame.size.width);
        label.frame = frame;
    }

    NSArray* fields = @[
        fDateAddedField,
        fDateCompletedField,
        fDateActivityField,
        fStateField,
        fProgressField,
        fHaveField,
        fDownloadedTotalField,
        fUploadedTotalField,
        fFailedHashField,
        fRatioField,
        fDownloadTimeField,
        fSeedTimeField,
        fErrorScrollView
    ];

    CGFloat const widthIncrease = newMaxWidth - oldMaxWidth;
    for (NSView* field in fields)
    {
        NSRect frame = field.frame;
        frame.origin.x += widthIncrease;
        frame.size.width -= widthIncrease;
        field.frame = frame;
    }

    //set the click action of the pieces view
#warning after 2.8 just hook this up in the xib
    fPiecesView.action = @selector(updatePiecesView:);
    fPiecesView.target = self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)setInfoForTorrents:(NSArray*)torrents
{
    //don't check if it's the same in case the metadata changed
    fTorrents = torrents;

    fSet = NO;
}

- (void)updateInfo
{
    if (!fSet)
    {
        [self setupInfo];
    }

    NSInteger const numberSelected = fTorrents.count;
    if (numberSelected == 0)
    {
        return;
    }

    uint64_t have = 0, haveVerified = 0, downloadedTotal = 0, uploadedTotal = 0, failedHash = 0;
    NSDate* lastActivity = nil;
    for (Torrent* torrent in fTorrents)
    {
        have += torrent.haveTotal;
        haveVerified += torrent.haveVerified;
        downloadedTotal += torrent.downloadedTotal;
        uploadedTotal += torrent.uploadedTotal;
        failedHash += torrent.failedHash;

        NSDate* nextLastActivity;
        if ((nextLastActivity = torrent.dateActivity))
        {
            lastActivity = lastActivity ? [lastActivity laterDate:nextLastActivity] : nextLastActivity;
        }
    }

    if (have == 0)
    {
        fHaveField.stringValue = [NSString stringForFileSize:0];
    }
    else
    {
        NSString* verifiedString = [NSString stringWithFormat:NSLocalizedString(@"%@ verified", "Inspector -> Activity tab -> have"),
                                                              [NSString stringForFileSize:haveVerified]];
        if (have == haveVerified)
        {
            fHaveField.stringValue = verifiedString;
        }
        else
        {
            fHaveField.stringValue = [NSString stringWithFormat:@"%@ (%@)", [NSString stringForFileSize:have], verifiedString];
        }
    }

    fDownloadedTotalField.stringValue = [NSString stringForFileSize:downloadedTotal];
    fUploadedTotalField.stringValue = [NSString stringForFileSize:uploadedTotal];
    fFailedHashField.stringValue = [NSString stringForFileSize:failedHash];

    fDateActivityField.objectValue = lastActivity;

    if (numberSelected == 1)
    {
        Torrent* torrent = fTorrents[0];

        fStateField.stringValue = torrent.stateString;

        NSString* progressString = [NSString percentString:torrent.progress longDecimals:YES];
        if (torrent.folder)
        {
            NSString* progressSelectedString = [NSString
                stringWithFormat:NSLocalizedString(@"%@ selected", "Inspector -> Activity tab -> progress"),
                                 [NSString percentString:torrent.progressDone longDecimals:YES]];
            progressString = [progressString stringByAppendingFormat:@" (%@)", progressSelectedString];
        }
        fProgressField.stringValue = progressString;

        fRatioField.stringValue = [NSString stringForRatio:torrent.ratio];

        NSString* errorMessage = torrent.errorMessage;
        if (![errorMessage isEqualToString:fErrorMessageView.string])
            fErrorMessageView.string = errorMessage;

        fDateCompletedField.objectValue = torrent.dateCompleted;

        //uses a relative date, so can't be set once
        fDateAddedField.objectValue = torrent.dateAdded;

        static NSDateComponentsFormatter* timeFormatter;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            timeFormatter = [NSDateComponentsFormatter new];
            timeFormatter.unitsStyle = NSDateComponentsFormatterUnitsStyleShort;
            timeFormatter.allowedUnits = NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond;
            timeFormatter.zeroFormattingBehavior = NSDateComponentsFormatterZeroFormattingBehaviorDropLeading;
        });

        fDownloadTimeField.stringValue = [timeFormatter stringFromTimeInterval:torrent.secondsDownloading];
        fSeedTimeField.stringValue = [timeFormatter stringFromTimeInterval:torrent.secondsSeeding];

        [fPiecesView updateView];
    }
    else if (numberSelected > 1)
    {
        fRatioField.stringValue = [NSString stringForRatio:tr_getRatio(uploadedTotal, downloadedTotal)];
    }
}

- (void)setPiecesView:(id)sender
{
    BOOL const availability = [sender selectedSegment] == PIECES_CONTROL_AVAILABLE;
    [NSUserDefaults.standardUserDefaults setBool:availability forKey:@"PiecesViewShowAvailability"];
    [self updatePiecesView:nil];
}

- (void)updatePiecesView:(id)sender
{
    BOOL const piecesAvailableSegment = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];

    [fPiecesControl setSelected:piecesAvailableSegment forSegment:PIECES_CONTROL_AVAILABLE];
    [fPiecesControl setSelected:!piecesAvailableSegment forSegment:PIECES_CONTROL_PROGRESS];

    [fPiecesView updateView];
}

- (void)clearView
{
    [fPiecesView clearView];
}

@end

@implementation InfoActivityViewController (Private)

- (void)setupInfo
{
    NSUInteger const count = fTorrents.count;
    if (count != 1)
    {
        if (count == 0)
        {
            fHaveField.stringValue = @"";
            fDownloadedTotalField.stringValue = @"";
            fUploadedTotalField.stringValue = @"";
            fFailedHashField.stringValue = @"";
            fDateActivityField.objectValue = @""; //using [field setStringValue: @""] causes "December 31, 1969 7:00 PM" to be displayed, at least on 10.7.3
            fRatioField.stringValue = @"";
        }

        fStateField.stringValue = @"";
        fProgressField.stringValue = @"";

        fErrorMessageView.string = @"";

        //using [field setStringValue: @""] causes "December 31, 1969 7:00 PM" to be displayed, at least on 10.7.3
        fDateAddedField.objectValue = @"";
        fDateCompletedField.objectValue = @"";

        fDownloadTimeField.stringValue = @"";
        fSeedTimeField.stringValue = @"";

        [fPiecesControl setSelected:NO forSegment:PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected:NO forSegment:PIECES_CONTROL_PROGRESS];
        fPiecesControl.enabled = NO;
        [fPiecesView setTorrent:nil];
    }
    else
    {
        Torrent* torrent = fTorrents[0];

        BOOL const piecesAvailableSegment = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
        [fPiecesControl setSelected:piecesAvailableSegment forSegment:PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected:!piecesAvailableSegment forSegment:PIECES_CONTROL_PROGRESS];
        fPiecesControl.enabled = YES;

        [fPiecesView setTorrent:torrent];
    }

    fSet = YES;
}

@end
