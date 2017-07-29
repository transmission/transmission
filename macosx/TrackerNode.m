/******************************************************************************
 * Copyright (c) 2009-2012 Transmission authors and contributors
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

#import "TrackerNode.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

@implementation TrackerNode

#warning remove ivars in header when 64-bit only (or it compiles in 32-bit mode)
@synthesize torrent = fTorrent;

- (id) initWithTrackerStat: (tr_tracker_stat *) stat torrent: (Torrent *) torrent
{
    if ((self = [super init]))
    {
        fStat = *stat;
        fTorrent = torrent; //weak reference
    }

    return self;
}

- (NSString *) description
{
    return [@"Tracker: " stringByAppendingString: [self fullAnnounceAddress]];
}

- (id) copyWithZone: (NSZone *) zone
{
    //this object is essentially immutable after initial setup
    return self;
}

- (BOOL) isEqual: (id) object
{
    if (self == object)
        return YES;

    if (![object isKindOfClass: [self class]])
        return NO;

    if ([self torrent] != [object torrent])
        return NO;

    return [self tier] == [object tier] && [[self fullAnnounceAddress] isEqualToString: [object fullAnnounceAddress]];
}

- (NSString *) host
{
    return @(fStat.host);
}

- (NSString *) fullAnnounceAddress
{
    return @(fStat.announce);
}

- (NSInteger) tier
{
    return fStat.tier;
}

- (NSUInteger) identifier
{
    return fStat.id;
}

- (NSInteger) totalSeeders
{
    return fStat.seederCount;
}

- (NSInteger) totalLeechers
{
    return fStat.leecherCount;
}

- (NSInteger) totalDownloaded
{
    return fStat.downloadCount;
}

- (NSString *) lastAnnounceStatusString
{
    NSString * dateString;
    if (fStat.hasAnnounced)
    {
        NSDateFormatter * dateFormatter = [[NSDateFormatter alloc] init];
        [dateFormatter setDateStyle: NSDateFormatterFullStyle];
        [dateFormatter setTimeStyle: NSDateFormatterShortStyle];
        [dateFormatter setDoesRelativeDateFormatting: YES];

        dateString = [dateFormatter stringFromDate: [NSDate dateWithTimeIntervalSince1970: fStat.lastAnnounceTime]];
    }
    else
        dateString = NSLocalizedString(@"N/A", "Tracker last announce");

    NSString * baseString;
    if (fStat.hasAnnounced && fStat.lastAnnounceTimedOut)
        baseString = [NSLocalizedString(@"Announce timed out", "Tracker last announce") stringByAppendingFormat: @": %@", dateString];
    else if (fStat.hasAnnounced && !fStat.lastAnnounceSucceeded)
    {
        baseString = NSLocalizedString(@"Announce error", "Tracker last announce");

        NSString * errorString = @(fStat.lastAnnounceResult);
        if ([errorString isEqualToString: @""])
            baseString = [baseString stringByAppendingFormat: @": %@", dateString];
        else
            baseString = [baseString stringByAppendingFormat: @": %@ - %@", errorString, dateString];
    }
    else
    {
        baseString = [NSLocalizedString(@"Last Announce", "Tracker last announce") stringByAppendingFormat: @": %@", dateString];
        if (fStat.hasAnnounced && fStat.lastAnnounceSucceeded && fStat.lastAnnouncePeerCount > 0)
        {
            NSString * peerString;
            if (fStat.lastAnnouncePeerCount == 1)
                peerString = NSLocalizedString(@"got 1 peer", "Tracker last announce");
            else
                peerString = [NSString stringWithFormat: NSLocalizedString(@"got %d peers", "Tracker last announce"),
                                        fStat.lastAnnouncePeerCount];
            baseString = [baseString stringByAppendingFormat: @" (%@)", peerString];
        }
    }

    return baseString;
}

- (NSString *) nextAnnounceStatusString
{
    switch (fStat.announceState)
    {
        case TR_TRACKER_ACTIVE:
            return [NSLocalizedString(@"Announce in progress", "Tracker next announce") stringByAppendingEllipsis];

        case TR_TRACKER_WAITING:
        {
            const NSTimeInterval nextAnnounceTimeLeft = fStat.nextAnnounceTime - [[NSDate date] timeIntervalSince1970];

            NSString *timeString;
            if ([NSApp isOnYosemiteOrBetter]) {
                static NSDateComponentsFormatter *formatter;
                static dispatch_once_t onceToken;
                dispatch_once(&onceToken, ^{
                    formatter = [NSDateComponentsFormatter new];
                    formatter.unitsStyle = NSDateComponentsFormatterUnitsStyleAbbreviated;
                    formatter.zeroFormattingBehavior = NSDateComponentsFormatterZeroFormattingBehaviorDropLeading;
                    formatter.collapsesLargestUnit = YES;
                });

                timeString = [formatter stringFromTimeInterval: nextAnnounceTimeLeft];
            }
            else {
                timeString = [NSString timeString: nextAnnounceTimeLeft includesTimeRemainingPhrase: NO showSeconds: YES];
            }
            return [NSString stringWithFormat: NSLocalizedString(@"Next announce in %@", "Tracker next announce"),
                    timeString];
        }
        case TR_TRACKER_QUEUED:
            return [NSLocalizedString(@"Announce is queued", "Tracker next announce") stringByAppendingEllipsis];

        case TR_TRACKER_INACTIVE:
            return fStat.isBackup ? NSLocalizedString(@"Tracker will be used as a backup", "Tracker next announce")
                                    : NSLocalizedString(@"Announce not scheduled", "Tracker next announce");

        default:
            NSAssert1(NO, @"unknown announce state: %d", fStat.announceState);
            return nil;
    }
}

- (NSString *) lastScrapeStatusString
{
    NSString * dateString;
    if (fStat.hasScraped)
    {
        NSDateFormatter * dateFormatter = [[NSDateFormatter alloc] init];
        [dateFormatter setDateStyle: NSDateFormatterFullStyle];
        [dateFormatter setTimeStyle: NSDateFormatterShortStyle];
        [dateFormatter setDoesRelativeDateFormatting: YES];

        dateString = [dateFormatter stringFromDate: [NSDate dateWithTimeIntervalSince1970: fStat.lastScrapeTime]];
    }
    else
        dateString = NSLocalizedString(@"N/A", "Tracker last scrape");

    NSString * baseString;
    if (fStat.hasScraped && fStat.lastScrapeTimedOut)
        baseString = [NSLocalizedString(@"Scrape timed out", "Tracker last scrape") stringByAppendingFormat: @": %@", dateString];
    else if (fStat.hasScraped && !fStat.lastScrapeSucceeded)
    {
        baseString = NSLocalizedString(@"Scrape error", "Tracker last scrape");

        NSString * errorString = @(fStat.lastScrapeResult);
        if ([errorString isEqualToString: @""])
            baseString = [baseString stringByAppendingFormat: @": %@", dateString];
        else
            baseString = [baseString stringByAppendingFormat: @": %@ - %@", errorString, dateString];
    }
    else
        baseString = [NSLocalizedString(@"Last Scrape", "Tracker last scrape") stringByAppendingFormat: @": %@", dateString];

    return baseString;
}

@end
