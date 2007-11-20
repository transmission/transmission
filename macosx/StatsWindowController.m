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
- (NSString *) timeString: (uint64_t) seconds;

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

#warning make like pref window panel

@end

@implementation StatsWindowController (Private)

- (void) updateStats
{
    tr_session_stats statsAll, statsSession;
    tr_getCumulativeSessionStats(fLib, &statsAll);
    tr_getSessionStats(fLib, &statsSession);
    
    [fUploadedField setStringValue: [NSString stringForLargeFileSizeGigs: statsAll.uploadedGigs bytes: statsAll.uploadedBytes]];
    [fDownloadedField setStringValue: [NSString stringForLargeFileSizeGigs: statsAll.downloadedGigs bytes: statsAll.downloadedBytes]];
    [fRatioField setStringValue: [NSString stringForRatio: statsAll.ratio]];
    
    [fTimeField setStringValue: [self timeString: statsAll.secondsActive]];
    
    [fNumOpenedField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d Times", "stats window -> times opened"),
                                        statsAll.sessionCount]];
}

- (NSString *) timeString: (uint64_t) seconds
{
    NSMutableArray * timeArray = [NSMutableArray arrayWithCapacity: 4];
    
    if (seconds >= 86400) //24 * 60 * 60
    {
        int days = seconds / 86400;
        if (days == 1)
            [timeArray addObject: NSLocalizedString(@"1 day", "stats window -> running time")];
        else
            [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%d days", "stats window -> running time"),
                                    seconds / 86400]];
        seconds %= 86400;
    }
    if (seconds >= 3600) //60 * 60
    {
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%d hours", "stats window -> running time"),
                                seconds / 3600]];
        seconds %= 3600;
    }
    if (seconds >= 60)
    {
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%d min", "stats window -> running time"), seconds / 60]];
        seconds %= 60;
    }
    [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%d sec", "stats window -> running time"), seconds]];
    
    return [timeArray componentsJoinedByString: @" "];
}

@end
