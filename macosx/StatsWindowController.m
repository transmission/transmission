/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#import "StatsWindowController.h"
#import "NSStringAdditions.h"

#define UPDATE_SECONDS 1.0

@interface StatsWindowController (Private)

- (void) updateStats;

@end

@implementation StatsWindowController

StatsWindowController * fStatsWindowInstance = nil;
tr_handle * fLib;
+ (StatsWindowController *) statsWindow: (tr_handle *) lib
{
    if (!fStatsWindowInstance)
    {
        if ((fStatsWindowInstance = [[self alloc] initWithWindowNibName: @"StatsWindow"]))
        {
            fLib = lib;
        }
    }
    return fStatsWindowInstance;
}

- (void) awakeFromNib
{
    [self updateStats];
    
    fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self
                selector: @selector(updateStats) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
    
    [[self window] setTitle: NSLocalizedString(@"Statistics", "Stats window -> title")];
    
    //set label text
    [fUploadedLabelField setStringValue: [NSLocalizedString(@"Uploaded", "Stats window -> label") stringByAppendingString: @":"]];
    [fDownloadedLabelField setStringValue: [NSLocalizedString(@"Downloaded", "Stats window -> label") stringByAppendingString: @":"]];
    [fRatioLabelField setStringValue: [NSLocalizedString(@"Ratio", "Stats window -> label") stringByAppendingString: @":"]];
    [fTimeLabelField setStringValue: [NSLocalizedString(@"Running Time", "Stats window -> label") stringByAppendingString: @":"]];
    [fNumOpenedLabelField setStringValue: [NSLocalizedString(@"Program Started", "Stats window -> label")
                                            stringByAppendingString: @":"]];
    
    //size all elements
    float oldWidth = [fUploadedLabelField frame].size.width;
    
    [fUploadedLabelField sizeToFit];
    [fDownloadedLabelField sizeToFit];
    [fRatioLabelField sizeToFit];
    [fTimeLabelField sizeToFit];
    [fNumOpenedLabelField sizeToFit];
    
    float maxWidth = MAX([fUploadedLabelField frame].size.width, [fDownloadedLabelField frame].size.width);
    maxWidth = MAX(maxWidth, [fRatioLabelField frame].size.width);
    maxWidth = MAX(maxWidth, [fTimeLabelField frame].size.width);
    maxWidth = MAX(maxWidth, [fNumOpenedLabelField frame].size.width);
    
    NSRect frame = [fUploadedLabelField frame];
    frame.size.width = maxWidth;
    [fUploadedLabelField setFrame: frame];
    
    frame = [fDownloadedLabelField frame];
    frame.size.width = maxWidth;
    [fDownloadedLabelField setFrame: frame];
    
    frame = [fRatioLabelField frame];
    frame.size.width = maxWidth;
    [fRatioLabelField setFrame: frame];
    
    frame = [fTimeLabelField frame];
    frame.size.width = maxWidth;
    [fTimeLabelField setFrame: frame];
    
    frame = [fNumOpenedLabelField frame];
    frame.size.width = maxWidth;
    [fNumOpenedLabelField setFrame: frame];
    
    //resize window for new label width - fields are set in nib to adjust correctly
    NSRect windowRect = [[self window] frame];
    windowRect.size.width += maxWidth - oldWidth;
    [[self window] setFrame: windowRect display: YES];
}

- (void) windowWillClose: (id)sender
{
    [fTimer invalidate];
    
    [fStatsWindowInstance release];
    fStatsWindowInstance = nil;
}

@end

@implementation StatsWindowController (Private)

- (void) updateStats
{
    tr_session_stats statsAll, statsSession;
    tr_getCumulativeSessionStats(fLib, &statsAll);
    tr_getSessionStats(fLib, &statsSession);
    
    [fUploadedField setStringValue: [NSString stringForFileSize: statsSession.uploadedBytes]];
    [fUploadedField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%u bytes", "stats -> bytes"),
                                    statsSession.uploadedBytes]];
    [fUploadedAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"),
                                        [NSString stringForFileSize: statsAll.uploadedBytes]]];
    [fUploadedAllField setToolTip: [NSString stringWithFormat:NSLocalizedString(@"%u bytes", "stats -> bytes"),
                                    statsAll.uploadedBytes]];
    
    [fDownloadedField setStringValue: [NSString stringForFileSize: statsSession.downloadedBytes]];
    [fDownloadedField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%u bytes", "stats -> bytes"),
                                    statsSession.downloadedBytes]];
    [fDownloadedAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"),
                                        [NSString stringForFileSize: statsAll.downloadedBytes]]];
    [fDownloadedAllField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%u bytes", "stats -> bytes"),
                                        statsAll.downloadedBytes]];
    
    [fRatioField setStringValue: [NSString stringForRatio: statsSession.ratio]];
    
    NSString * totalRatioString = statsAll.ratio != TR_RATIO_NA
        ? [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"), [NSString stringForRatio: statsAll.ratio]]
        : NSLocalizedString(@"Total N/A", "stats total");
    [fRatioAllField setStringValue: totalRatioString];
    
    [fTimeField setStringValue: [NSString timeString: statsSession.secondsActive showSeconds: NO]];
    [fTimeAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"),
                                        [NSString timeString: statsAll.secondsActive showSeconds: NO]]];
    
    if (statsAll.sessionCount == 1)
        [fNumOpenedField setStringValue: NSLocalizedString(@"1 time", "stats window -> times opened")];
    else
        [fNumOpenedField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d times", "stats window -> times opened"),
                                        statsAll.sessionCount]];
}

@end
