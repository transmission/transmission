/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008 Transmission authors and contributors
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

#import "BlocklistScheduler.h"
#import "BlocklistDownloader.h"
#import "NSApplicationAdditions.h"

//thirty second delay before running after option is changed
#define SMALL_DELAY 30

//update one week after previous update
#define FULL_WAIT (60 * 60 * 24 * 7)

@interface BlocklistScheduler (Private)

- (void) runUpdater;

@end

@implementation BlocklistScheduler

BlocklistScheduler * fScheduler = nil;
+ (BlocklistScheduler *) scheduler
{
    if (!fScheduler)
        fScheduler = [[BlocklistScheduler alloc] init];
    
    return fScheduler;
}

- (void) updateSchedule
{
    if ([BlocklistDownloader isRunning])
        return;
    
    [self cancelSchedule];
    
    if (![[NSUserDefaults standardUserDefaults] boolForKey: @"Blocklist"]
        || ![[NSUserDefaults standardUserDefaults] boolForKey: @"BlocklistAutoUpdate"])
        return;
    
    NSDate * lastUpdateDate = [[NSUserDefaults standardUserDefaults] objectForKey: @"BlocklistLastUpdate"];
    if (lastUpdateDate)
        lastUpdateDate = [lastUpdateDate addTimeInterval: FULL_WAIT];
    NSDate * closeDate = [NSDate dateWithTimeIntervalSinceNow: SMALL_DELAY];
    
    NSDate * useDate = lastUpdateDate ? [lastUpdateDate laterDate: closeDate] : closeDate;
    
    fTimer = [[NSTimer alloc] initWithFireDate: useDate interval: 0 target: self selector: @selector(runUpdater)
                userInfo: nil repeats: NO];
    
    //current run loop usually means a second update won't work
    NSRunLoop * loop = [NSApp isOnLeopardOrBetter] ? [NSRunLoop mainRunLoop] : [NSRunLoop currentRunLoop];
    [loop addTimer: fTimer forMode: NSDefaultRunLoopMode];
    [loop addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [loop addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
}

- (void) cancelSchedule
{
    [fTimer invalidate];
    fTimer = nil;
}

@end

@implementation BlocklistScheduler (Private)

- (void) runUpdater
{
    fTimer = nil;
    [BlocklistDownloader downloader];
}

@end
