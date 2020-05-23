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

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> //tr_addressIsIP()

#import "TrackerCell.h"
#import "TrackerNode.h"

#define PADDING_HORIZONAL 3.0
#define PADDING_STATUS_HORIZONAL 3.0
#define ICON_SIZE 16.0
#define PADDING_BETWEEN_ICON_AND_NAME 4.0
#define PADDING_ABOVE_ICON 1.0
#define PADDING_ABOVE_NAME 1.0
#define PADDING_BETWEEN_LINES 1.0
#define PADDING_BETWEEN_LINES_ON_SAME_LINE 4.0
#define COUNT_WIDTH 40.0

@interface TrackerCell (Private)

- (NSImage *) favIcon;
- (void) loadTrackerIcon: (NSString *) baseAddress;

- (NSRect) imageRectForBounds: (NSRect) bounds;
- (NSRect) rectForNameWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForCountWithString: (NSAttributedString *) string withAboveRect: (NSRect) aboveRect inBounds: (NSRect) bounds;
- (NSRect) rectForCountLabelWithString: (NSAttributedString *) string withRightRect: (NSRect) rightRect inBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithString: (NSAttributedString *) string withAboveRect: (NSRect) aboveRect withRightRect: (NSRect) rightRect
            inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedName;
- (NSAttributedString *) attributedStatusWithString: (NSString *) statusString;
- (NSAttributedString *) attributedCount: (NSInteger) count;

@end

@implementation TrackerCell

//make the favicons accessible to all tracker cells
NSCache * fTrackerIconCache;
NSMutableSet * fTrackerIconLoading;

+ (void) initialize
{
    fTrackerIconCache = [[NSCache alloc] init];
    fTrackerIconLoading = [[NSMutableSet alloc] init];
}

- (id) init
{
    if ((self = [super init]))
    {
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];

        fNameAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                            [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                            paragraphStyle, NSParagraphStyleAttributeName, nil];

        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];

    }
    return self;
}


- (id) copyWithZone: (NSZone *) zone
{
    TrackerCell * copy = [super copyWithZone: zone];

    copy->fNameAttributes = fNameAttributes;
    copy->fStatusAttributes = fStatusAttributes;

    return copy;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //icon
    [[self favIcon] drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];

    //set table colors
    NSColor * nameColor, * statusColor;
    if ([self backgroundStyle] == NSBackgroundStyleDark)
        nameColor = statusColor = [NSColor whiteColor];
    else
    {
        nameColor = [NSColor labelColor];
        statusColor = [NSColor secondaryLabelColor];
    }

    fNameAttributes[NSForegroundColorAttributeName] = nameColor;
    fStatusAttributes[NSForegroundColorAttributeName] = statusColor;

    TrackerNode * node = (TrackerNode *)[self objectValue];

    //name
    NSAttributedString * nameString = [self attributedName];
    const NSRect nameRect = [self rectForNameWithString: nameString inBounds: cellFrame];
    [nameString drawInRect: nameRect];

    //count strings
    NSAttributedString * seederString = [self attributedCount: [node totalSeeders]];
    const NSRect seederRect = [self rectForCountWithString: seederString withAboveRect: nameRect inBounds: cellFrame];
    [seederString drawInRect: seederRect];

    NSAttributedString * leecherString = [self attributedCount: [node totalLeechers]];
    const NSRect leecherRect = [self rectForCountWithString: leecherString withAboveRect: seederRect inBounds: cellFrame];
    [leecherString drawInRect: leecherRect];

    NSAttributedString * downloadedString = [self attributedCount: [node totalDownloaded]];
    const NSRect downloadedRect = [self rectForCountWithString: downloadedString withAboveRect: leecherRect inBounds: cellFrame];
    [downloadedString drawInRect: downloadedRect];

    //count label strings
    NSString * seederLabelBaseString = [NSLocalizedString(@"Seeders", "tracker peer stat") stringByAppendingFormat: @": "];
    NSAttributedString * seederLabelString = [self attributedStatusWithString: seederLabelBaseString];
    const NSRect seederLabelRect = [self rectForCountLabelWithString: seederLabelString withRightRect: seederRect
                                        inBounds: cellFrame];
    [seederLabelString drawInRect: seederLabelRect];

    NSString * leecherLabelBaseString = [NSLocalizedString(@"Leechers", "tracker peer stat") stringByAppendingFormat: @": "];
    NSAttributedString * leecherLabelString = [self attributedStatusWithString: leecherLabelBaseString];
    const NSRect leecherLabelRect = [self rectForCountLabelWithString: leecherLabelString withRightRect: leecherRect
                                        inBounds: cellFrame];
    [leecherLabelString drawInRect: leecherLabelRect];

    NSString * downloadedLabelBaseString = [NSLocalizedString(@"Downloaded", "tracker peer stat") stringByAppendingFormat: @": "];
    NSAttributedString * downloadedLabelString = [self attributedStatusWithString: downloadedLabelBaseString];
    const NSRect downloadedLabelRect = [self rectForCountLabelWithString: downloadedLabelString withRightRect: downloadedRect
                                        inBounds: cellFrame];
    [downloadedLabelString drawInRect: downloadedLabelRect];

    //status strings
    NSAttributedString * lastAnnounceString = [self attributedStatusWithString: [node lastAnnounceStatusString]];
    const NSRect lastAnnounceRect = [self rectForStatusWithString: lastAnnounceString withAboveRect: nameRect
                                        withRightRect: seederLabelRect inBounds: cellFrame];
    [lastAnnounceString drawInRect: lastAnnounceRect];

    NSAttributedString * nextAnnounceString = [self attributedStatusWithString: [node nextAnnounceStatusString]];
    const NSRect nextAnnounceRect = [self rectForStatusWithString: nextAnnounceString withAboveRect: lastAnnounceRect
                                        withRightRect: leecherLabelRect inBounds: cellFrame];
    [nextAnnounceString drawInRect: nextAnnounceRect];

    NSAttributedString * lastScrapeString = [self attributedStatusWithString: [node lastScrapeStatusString]];
    const NSRect lastScrapeRect = [self rectForStatusWithString: lastScrapeString withAboveRect: nextAnnounceRect
                                    withRightRect: downloadedLabelRect inBounds: cellFrame];
    [lastScrapeString drawInRect: lastScrapeRect];
}

@end

@implementation TrackerCell (Private)

- (NSImage *) favIcon
{
    id icon = nil;
    NSURL * address = [NSURL URLWithString: [(TrackerNode *)[self objectValue] fullAnnounceAddress]];
    NSString * host;
    if ((host = [address host]))
    {
        //don't try to parse ip address
        const BOOL separable = !tr_addressIsIP([host UTF8String]);

        NSArray * hostComponents = separable ? [host componentsSeparatedByString: @"."] : nil;

        //let's try getting the tracker address without using any subdomains
        NSString * baseAddress;
        if (separable && [hostComponents count] > 1)
            baseAddress = [NSString stringWithFormat: @"http://%@.%@",
                            hostComponents[[hostComponents count]-2], [hostComponents lastObject]];
        else
            baseAddress = [NSString stringWithFormat: @"http://%@", host];

        icon = [fTrackerIconCache objectForKey: baseAddress];
        if (!icon)
            [self loadTrackerIcon: baseAddress];
    }

    return (icon && icon != [NSNull null]) ? icon : [NSImage imageNamed: @"FavIcon"];
}

#warning better favicon detection
- (void) loadTrackerIcon: (NSString *) baseAddress
{
    if ([fTrackerIconLoading containsObject: baseAddress]) {
        return;
    }
    [fTrackerIconLoading addObject: baseAddress];
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSImage *icon = nil;
        
        NSArray<NSString *> *filenamesToTry = @[ @"favicon.png", @"favicon.ico" ];
        for (NSString *filename in filenamesToTry) {
            NSURL * favIconUrl = [NSURL URLWithString: [baseAddress stringByAppendingPathComponent:filename]];
            
            NSURLRequest * request = [NSURLRequest requestWithURL: favIconUrl cachePolicy: NSURLRequestUseProtocolCachePolicy
                                                  timeoutInterval: 30.0];
            
            NSData * iconData = [NSURLConnection sendSynchronousRequest: request returningResponse: NULL error: NULL];
            if (iconData) {
                icon = [[NSImage alloc] initWithData: iconData];
                if (icon) {
                    break;
                }
            }
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (icon)
            {
                [fTrackerIconCache setObject: icon forKey: baseAddress];
                
                [[self controlView] setNeedsDisplay: YES];
            }
            else
                [fTrackerIconCache setObject: [NSNull null] forKey: baseAddress];
            
            [fTrackerIconLoading removeObject: baseAddress];
        });
    });
}

- (NSRect) imageRectForBounds: (NSRect) bounds
{
    return NSMakeRect(NSMinX(bounds) + PADDING_HORIZONAL, NSMinY(bounds) + PADDING_ABOVE_ICON, ICON_SIZE, ICON_SIZE);
}

- (NSRect) rectForNameWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONAL + ICON_SIZE + PADDING_BETWEEN_ICON_AND_NAME;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_NAME;

    result.size.height = [string size].height;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONAL;

    return result;
}

- (NSRect) rectForCountWithString: (NSAttributedString *) string withAboveRect: (NSRect) aboveRect inBounds: (NSRect) bounds
{
    return NSMakeRect(NSMaxX(bounds) - PADDING_HORIZONAL - COUNT_WIDTH,
                        NSMaxY(aboveRect) + PADDING_BETWEEN_LINES,
                        COUNT_WIDTH, [string size].height);
}

- (NSRect) rectForCountLabelWithString: (NSAttributedString *) string withRightRect: (NSRect) rightRect inBounds: (NSRect) bounds
{
    NSRect result = rightRect;
    result.size.width = [string size].width;
    result.origin.x -= NSWidth(result);

    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string withAboveRect: (NSRect) aboveRect withRightRect: (NSRect) rightRect
            inBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + PADDING_STATUS_HORIZONAL;
    result.origin.y = NSMaxY(aboveRect) + PADDING_BETWEEN_LINES;

    result.size.height = [string size].height;
    result.size.width = NSMinX(rightRect) - PADDING_BETWEEN_LINES_ON_SAME_LINE - NSMinX(result);

    return result;
}

- (NSAttributedString *) attributedName
{
    NSString * name = [(TrackerNode *)[self objectValue] host];
    return [[NSAttributedString alloc] initWithString: name attributes: fNameAttributes];
}

- (NSAttributedString *) attributedStatusWithString: (NSString *) statusString
{
    return [[NSAttributedString alloc] initWithString: statusString attributes: fStatusAttributes];
}

- (NSAttributedString *) attributedCount: (NSInteger) count
{
    NSString * countString = count != -1 ? [NSString stringWithFormat: @"%ld", count] : NSLocalizedString(@"N/A", "tracker peer stat");
    return [[NSAttributedString alloc] initWithString: countString attributes: fStatusAttributes];
}

@end
