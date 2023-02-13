// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BlocklistScheduler.h"
#import "BlocklistDownloader.h"

//thirty second delay before running after option is changed
static NSTimeInterval const kSmallDelay = 30;

//update one week after previous update
static NSTimeInterval const kFullWait = 60 * 60 * 24 * 7;

@interface BlocklistScheduler ()

@property(nonatomic) NSTimer* fTimer;

@end

@implementation BlocklistScheduler

BlocklistScheduler* fScheduler = nil;

+ (BlocklistScheduler*)scheduler
{
    if (!fScheduler)
    {
        fScheduler = [[BlocklistScheduler alloc] init];
    }

    return fScheduler;
}

- (void)updateSchedule
{
    if (BlocklistDownloader.isRunning)
        return;

    [self cancelSchedule];

    NSString* blocklistURL;
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"BlocklistNew"] ||
        !((blocklistURL = [NSUserDefaults.standardUserDefaults stringForKey:@"BlocklistURL"]) && ![blocklistURL isEqualToString:@""]) ||
        ![NSUserDefaults.standardUserDefaults boolForKey:@"BlocklistAutoUpdate"])
    {
        return;
    }

    NSDate* lastUpdateDate = [NSUserDefaults.standardUserDefaults objectForKey:@"BlocklistNewLastUpdate"];
    if (lastUpdateDate)
    {
        lastUpdateDate = [lastUpdateDate dateByAddingTimeInterval:kFullWait];
    }
    NSDate* closeDate = [NSDate dateWithTimeIntervalSinceNow:kSmallDelay];

    NSDate* useDate = lastUpdateDate ? [lastUpdateDate laterDate:closeDate] : closeDate;

    self.fTimer = [[NSTimer alloc] initWithFireDate:useDate interval:0 target:self selector:@selector(runUpdater) userInfo:nil
                                            repeats:NO];

    //current run loop usually means a second update won't work
    NSRunLoop* loop = NSRunLoop.mainRunLoop;
    [loop addTimer:self.fTimer forMode:NSDefaultRunLoopMode];
    [loop addTimer:self.fTimer forMode:NSModalPanelRunLoopMode];
    [loop addTimer:self.fTimer forMode:NSEventTrackingRunLoopMode];
}

- (void)cancelSchedule
{
    [self.fTimer invalidate];
    self.fTimer = nil;
}

#pragma mark - Private

- (void)runUpdater
{
    self.fTimer = nil;
    [BlocklistDownloader downloader];
}

@end
