/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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
}

- (void) windowWillClose: (id)sender
{
    if (fTimer)
        [fTimer invalidate];
    
	[fStatsWindowInstance release];
    fStatsWindowInstance = nil;
}

@end

@implementation StatsWindowController (Private)

- (void) updateStats
{
    tr_global_stats stats;
    tr_getGlobalStats(fLib, &stats);
    
    [fUploadedField setStringValue: [NSString stringForLargeFileSizeGigs: stats.uploadedGigs bytes: stats.uploadedBytes]];
    [fDownloadedField setStringValue: [NSString stringForLargeFileSizeGigs: stats.downloadedGigs bytes: stats.downloadedBytes]];
    [fRatioField setStringValue: [NSString stringForRatio: stats.ratio]];
    
    [fNumOpenedField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d Times", "stats window -> times opened"),
                                        stats.sessionCount]];
}

@end
