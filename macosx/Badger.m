/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#import "Badger.h"
#import "BadgeView.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "NSBezierPathAdditions.h"

#define COMPLETED_BOTTOM_PADDING 5.0
#define SPEED_BOTTOM_PADDING 2.0
#define SPEED_BETWEEN_PADDING 2.0
#define BADGE_HEIGHT 30.0

@interface Badger (Private)

- (void) badgeString: (NSString *) string forRect: (NSRect) rect;

@end

@implementation Badger

- (id) initWithLib: (tr_handle *) lib
{
    if ((self = [super init]))
    {
        fLib = lib;
        
        fCompleted = 0;
        fCompletedBadged = 0;
        fSpeedBadge = NO;
        
        if ([NSApp isOnLeopardOrBetter])
        {
            BadgeView * view = [[BadgeView alloc] initWithFrame: [[[NSApp dockTile] contentView] frame] lib: lib];
            [[NSApp dockTile] setContentView: view];
            [view release];
        }
        else
            fQuittingTiger = NO;
        
        //change that just impacts the dock badge
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateBadge) name: @"DockBadgeChange" object: nil];
    }
    
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [NSApp setApplicationIconImage: nil]; //needed on 10.4
    
    [fDockIcon release];
    [fAttributes release];
    
    [super dealloc];
}

- (void) updateBadge
{
    if ([NSApp isOnLeopardOrBetter])
    {
        [[NSApp dockTile] display];
        return;
    }
    else if (fQuittingTiger)
        return;
    else;
    
    //set completed badge to top right
    BOOL completedChange = fCompleted != fCompletedBadged;
    if (completedChange)
    {
        fCompletedBadged = fCompleted;
        
        //force image to reload - copy does not work
        NSImage * icon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
        NSSize iconSize = [icon size];
        
        [fDockIcon release];
        fDockIcon = [[NSImage alloc] initWithSize: iconSize];
        [fDockIcon addRepresentation: [icon bestRepresentationForDevice: nil]];
        [icon release];
        
        if (fCompleted > 0)
        {
            if (!fBadge)
                fBadge = [NSImage imageNamed: @"Badge"];
            
            NSRect badgeRect;
            badgeRect.size = [fBadge size];
            badgeRect.origin.x = iconSize.width - badgeRect.size.width;
            badgeRect.origin.y = iconSize.height - badgeRect.size.height;
            
            [fDockIcon lockFocus];
            
            //place badge
            [fBadge compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
            
            //ignore shadow of badge when placing string
            badgeRect.size.height -= COMPLETED_BOTTOM_PADDING;
            badgeRect.origin.y += COMPLETED_BOTTOM_PADDING;
            
            //place badge text
            [self badgeString: [NSString stringWithFormat: @"%d", fCompleted] forRect: badgeRect];
                        
            [fDockIcon unlockFocus];
        }
    }
    
    NSImage * dockIcon = nil;
    BOOL speedBadge = NO;
    
    BOOL checkDownload = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeDownloadRate"],
        checkUpload = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeUploadRate"];
    if (checkDownload || checkUpload)
    {
        //set upload and download rate badges
        NSString * downloadRateString = nil, * uploadRateString = nil;
        
        float downloadRate, uploadRate;
        tr_torrentRates(fLib, &downloadRate, &uploadRate);
        
        if (checkDownload && downloadRate >= 0.1)
            downloadRateString = [NSString stringForSpeedAbbrev: downloadRate];
        if (checkUpload && uploadRate >= 0.1)
            uploadRateString = [NSString stringForSpeedAbbrev: uploadRate];
        
        speedBadge = uploadRateString || downloadRateString;
        if (speedBadge)
        {
            if (!fDockIcon)
                fDockIcon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
            dockIcon = [fDockIcon copy];
            
            NSRect badgeRect;
            badgeRect.size = [[NSImage imageNamed: @"UploadBadge.png"] size];
            badgeRect.origin = NSZeroPoint;
            
            //ignore shadow of badge when placing string
            NSRect stringRect = badgeRect;
            stringRect.size.height -= SPEED_BOTTOM_PADDING;
            stringRect.origin.y += SPEED_BOTTOM_PADDING;
            
            [dockIcon lockFocus];
            
            if (uploadRateString)
            {
                //place badge and text
                [[NSImage imageNamed: @"UploadBadge.png"] compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
                [self badgeString: uploadRateString forRect: stringRect];
            }
            
            if (downloadRateString)
            {
                //download rate above upload rate
                if (uploadRateString)
                {
                    float spaceBetween = badgeRect.size.height + SPEED_BETWEEN_PADDING;
                    badgeRect.origin.y += spaceBetween;
                    stringRect.origin.y += spaceBetween;
                }
                
                //place badge and text
                [[NSImage imageNamed: @"DownloadBadge.png"] compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
                [self badgeString: downloadRateString forRect: stringRect];
            }
            
            [dockIcon unlockFocus];
        }
    }
    
    //update dock badge
    if (fSpeedBadge || speedBadge || completedChange)
    {
        [NSApp setApplicationIconImage: dockIcon ? dockIcon : fDockIcon];
        [dockIcon release];
        
        fSpeedBadge = speedBadge;
    }
}

- (void) incrementCompleted
{
    fCompleted++;
    
    if ([NSApp isOnLeopardOrBetter])
        [[NSApp dockTile] setBadgeLabel: [NSString stringWithFormat: @"%d", fCompleted]];
    else
        [self updateBadge];
}

- (void) clearCompleted
{
    if (fCompleted != 0)
    {
        fCompleted = 0;
        if ([NSApp isOnLeopardOrBetter])
            [[NSApp dockTile] setBadgeLabel: @""];
        else
            [self updateBadge];
    }
}

- (void) setQuitting
{
    if ([NSApp isOnLeopardOrBetter])
    {
        [self clearCompleted];
        [(BadgeView *)[[NSApp dockTile] contentView] setQuitting];
        [self updateBadge];
    }
    else
    {
        fQuittingTiger = YES;
        
        fSpeedBadge = NO;
        fCompleted = 0;
        fCompletedBadged = 0;
        
        NSImage * quitIcon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
        NSRect rect = NSZeroRect;
        rect.size = [quitIcon size];
        
        NSRect badgeRect = NSMakeRect(0.0, (rect.size.height - BADGE_HEIGHT) * 0.5, rect.size.width, BADGE_HEIGHT);
        
        [quitIcon lockFocus];
        
        [[NSImage imageNamed: @"QuitBadge.png"] compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
        [self badgeString: NSLocalizedString(@"Quitting", "Dock Badger -> quit message") forRect: badgeRect];
        
        [quitIcon unlockFocus];
        
        [NSApp setApplicationIconImage: quitIcon];
        [quitIcon release];
    }
}

@end

@implementation Badger (Private)

//dock icon must have locked focus
- (void) badgeString: (NSString *) string forRect: (NSRect) rect
{
    if (!fAttributes)
    {
        NSShadow * stringShadow = [[NSShadow alloc] init];
        [stringShadow setShadowOffset: NSMakeSize(2.0, -2.0)];
        [stringShadow setShadowBlurRadius: 4.0];
        
        fAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
            [NSColor whiteColor], NSForegroundColorAttributeName,
            [NSFont boldSystemFontOfSize: 26.0], NSFontAttributeName, stringShadow, NSShadowAttributeName, nil];
        
        [stringShadow release];
    }
    
    NSSize stringSize = [string sizeWithAttributes: fAttributes];
    
    //string is in center of image
    rect.origin.x += (rect.size.width - stringSize.width) * 0.5;
    rect.origin.y += (rect.size.height - stringSize.height) * 0.5;
    rect.size = stringSize;
                        
    [string drawInRect: rect withAttributes: fAttributes];
}

@end
