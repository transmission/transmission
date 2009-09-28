/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2009 Transmission authors and contributors
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

#import "TrackerCell.h"
#import "NSApplicationAdditions.h"
#import "TrackerNode.h"

#define PADDING_HORIZONAL 3.0
#define PADDING_STATUS_HORIZONAL 3.0
#define ICON_SIZE 14.0
#define PADDING_BETWEEN_ICON_AND_NAME 4.0
#define PADDING_ABOVE_ICON 1.0
#define PADDING_ABOVE_NAME 2.0
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
#warning make NSCache when 10.6-only
NSMutableDictionary * fTrackerIconCache;
NSMutableSet * fTrackerIconLoading;

+ (void) initialize
{
    fTrackerIconCache = [[NSMutableDictionary alloc] init];
    fTrackerIconLoading = [[NSMutableSet alloc] init];
}

- (id) init
{
    if ((self = [super init]))
    {
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
        
        fNameAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                            [NSFont messageFontOfSize: 11.0], NSFontAttributeName,
                            paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        [paragraphStyle release];
    }
    return self;
}

- (void) dealloc
{
    [fNameAttributes release];
    [fStatusAttributes release];
    
    [super dealloc];
}

- (id) copyWithZone: (NSZone *) zone
{
    TrackerCell * copy = [super copyWithZone: zone];
    
    copy->fNameAttributes = [fNameAttributes retain];
    copy->fStatusAttributes = [fStatusAttributes retain];
    
    return copy;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //icon
    if ([NSApp isOnSnowLeopardOrBetter])
        [[self favIcon] drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver
                        fraction: 1.0 respectFlipped: YES hints: nil];
    else
    {
        NSImage * icon = [self favIcon];
        [icon setFlipped: YES];
        [icon drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }

    //set table colors
    NSColor * nameColor, * statusColor;
    if ([self backgroundStyle] == NSBackgroundStyleDark)
        nameColor = statusColor = [NSColor whiteColor];
    else
    {
        nameColor = [NSColor controlTextColor];
        statusColor = [NSColor darkGrayColor];
    }
    
    [fNameAttributes setObject: nameColor forKey: NSForegroundColorAttributeName];
    [fStatusAttributes setObject: statusColor forKey: NSForegroundColorAttributeName];
    
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
    NSURL * address = [NSURL URLWithString: [(TrackerNode *)[self objectValue] host]];
    NSArray * hostComponents = [[address host] componentsSeparatedByString: @"."];
    
    //let's try getting the tracker address without using any subdomains
    NSString * baseAddress;
    if ([hostComponents count] > 1)
        baseAddress = [NSString stringWithFormat: @"http://%@.%@",
                        [hostComponents objectAtIndex: [hostComponents count] - 2], [hostComponents lastObject]];
    else
        baseAddress = [NSString stringWithFormat: @"http://%@", [hostComponents lastObject]];
    
    id icon = [fTrackerIconCache objectForKey: baseAddress];
    if (!icon && ![fTrackerIconLoading containsObject: baseAddress])
    {
        [fTrackerIconLoading addObject: baseAddress];
        [NSThread detachNewThreadSelector: @selector(loadTrackerIcon:) toTarget: self withObject: baseAddress];
    }
    
    return (icon && icon != [NSNull null]) ? icon : [NSImage imageNamed: @"FavIcon.png"];
}

- (void) loadTrackerIcon: (NSString *) baseAddress
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    
    NSURL * favIconUrl = [NSURL URLWithString: [baseAddress stringByAppendingPathComponent: @"favicon.ico"]];
    
    NSURLRequest * request = [NSURLRequest requestWithURL: favIconUrl cachePolicy: NSURLRequestUseProtocolCachePolicy
                                timeoutInterval: 30.0];
    NSData * iconData = [NSURLConnection sendSynchronousRequest: request returningResponse: NULL error: NULL];
    NSImage * icon = [[NSImage alloc] initWithData: iconData];
    
    if (icon)
    {
        [fTrackerIconCache setObject: icon forKey: baseAddress];
        [icon release];
    }
    else
        [fTrackerIconCache setObject: [NSNull null] forKey: baseAddress];
    
    [fTrackerIconLoading removeObject: baseAddress];

    [pool drain];
}

- (NSRect) imageRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    result.origin.x += PADDING_HORIZONAL;
    result.origin.y += PADDING_ABOVE_ICON;
    result.size = NSMakeSize(ICON_SIZE, ICON_SIZE);
    
    return result;
}

- (NSRect) rectForNameWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    const NSSize nameSize = [string size];
    
    NSRect result = bounds;
    result.origin.x += PADDING_HORIZONAL + ICON_SIZE + PADDING_BETWEEN_ICON_AND_NAME;
    result.origin.y += PADDING_ABOVE_NAME;
        
    result.size = nameSize;
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - NSMinX(result));
    
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
    result.origin.x -= result.size.width;
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string withAboveRect: (NSRect) aboveRect withRightRect: (NSRect) rightRect
            inBounds: (NSRect) bounds
{
    const NSSize statusSize = [string size];
    
    NSRect result = bounds;
    result.origin.x += PADDING_STATUS_HORIZONAL;
    result.origin.y = NSMaxY(aboveRect) + PADDING_BETWEEN_LINES;
    
    result.size = statusSize;
    result.size.width = MIN(result.size.width, (NSMinX(rightRect) - PADDING_BETWEEN_LINES_ON_SAME_LINE) - NSMinX(result));
    
    return result;
}

- (NSAttributedString *) attributedName
{
    NSString * name = [(TrackerNode *)[self objectValue] host];
    return [[[NSAttributedString alloc] initWithString: name attributes: fNameAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusWithString: (NSString *) statusString
{
    return [[[NSAttributedString alloc] initWithString: statusString attributes: fStatusAttributes] autorelease];
}

- (NSAttributedString *) attributedCount: (NSInteger) count
{
    NSString * countString = count != -1 ? [NSString stringWithFormat: @"%d", count] : NSLocalizedString(@"N/A", "tracker peer stat");
    return [[[NSAttributedString alloc] initWithString: countString attributes: fStatusAttributes] autorelease];
}

@end
