/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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
#import "StringAdditions.h"

@interface Badger (Private)

- (void) badgeString: (NSString *) string forRect: (NSRect) rect;

@end

@implementation Badger

- (id) initWithLib: (tr_handle_t *) lib
{
    if ((self = [super init]))
    {
        fLib = lib;
        
        fDockIcon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
        fBadge = [NSImage imageNamed: @"Badge"];
        fUploadBadge = [NSImage imageNamed: @"UploadBadge"];
        fDownloadBadge = [NSImage imageNamed: @"DownloadBadge"];
        
        NSShadow * stringShadow = [[NSShadow alloc] init];
        [stringShadow setShadowOffset: NSMakeSize(2.0, -2.0)];
        [stringShadow setShadowBlurRadius: 4.0];
        
        NSFont * boldFont = [[NSFontManager sharedFontManager] convertFont:
                                [NSFont fontWithName: @"Helvetica" size: 28.0] toHaveTrait: NSBoldFontMask];
        
        fAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
            [NSColor whiteColor], NSForegroundColorAttributeName,
            boldFont, NSFontAttributeName, stringShadow, NSShadowAttributeName, nil];
        
        [stringShadow release];
        
        fCompleted = 0;
        fSpeedBadge = NO;
        
        fLock = [[NSLock alloc] init];
    }
    
    return self;
}

- (void) dealloc
{
    [fDockIcon release];
    [fAttributes release];
    [fLock release];
    [super dealloc];
}

- (void) updateBadgeWithCompleted: (int) completed
{
    [fLock lock];
    
    //set completed badge to top right
    BOOL baseChange;
    if (baseChange = (fCompleted != completed))
    {
        fCompleted = completed;
        
        [fDockIcon release];
        fDockIcon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
        
        if (completed > 0)
        {
            NSRect badgeRect;
            NSSize iconSize = [fDockIcon size];
            badgeRect.size = [fBadge size];
            badgeRect.origin.x = iconSize.width - badgeRect.size.width;
            badgeRect.origin.y = iconSize.height - badgeRect.size.height;
                                        
            [fDockIcon lockFocus];
            
            //place badge
            [fBadge compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
            
            //ignore shadow of badge when placing string
            float badgeBottomExtra = 5.0;
            badgeRect.size.height -= badgeBottomExtra;
            badgeRect.origin.y += badgeBottomExtra;
            
            //place badge text
            [self badgeString: [NSString stringWithInt: completed] forRect: badgeRect];
                        
            [fDockIcon unlockFocus];
        }
    }

    //set upload and download rate badges
    BOOL checkUpload = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeUploadRate"],
        checkDownload = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeDownloadRate"];
    float downloadRate, uploadRate;
    
    if (checkUpload || checkDownload)
        tr_torrentRates(fLib, & downloadRate, & uploadRate);
    
    NSString * downloadRateString = checkDownload && downloadRate >= 0.1 ? [NSString stringForSpeedAbbrev: downloadRate] : nil,
            * uploadRateString = checkUpload && uploadRate >= 0.1 ? [NSString stringForSpeedAbbrev: uploadRate] : nil;
    
    NSImage * dockIcon = nil;
    BOOL speedBadge;
    if (speedBadge = (uploadRateString || downloadRateString))
    {
        dockIcon = [fDockIcon copy];
        
        NSRect badgeRect;
        badgeRect.size = [fUploadBadge size];
        badgeRect.origin = NSZeroPoint;
        
        //ignore shadow of badge when placing string
        NSRect stringRect = badgeRect;
        float badgeBottomExtra = 2.0;
        stringRect.size.height -= badgeBottomExtra;
        stringRect.origin.y += badgeBottomExtra;
        
        [dockIcon lockFocus];
        
        if (uploadRateString)
        {
            //place badge
            [fUploadBadge compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
            
            //place badge text
            [self badgeString: uploadRateString forRect: stringRect];
        }
        
        if (downloadRateString)
        {
            //download rate above upload rate
            if (uploadRateString)
            {
                float spaceBetween = badgeRect.size.height + 2.0;
                badgeRect.origin.y += spaceBetween;
                stringRect.origin.y += spaceBetween;
            }
        
            //place badge
            [fDownloadBadge compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
            
            //place badge text
            [self badgeString: downloadRateString forRect: stringRect];
        }
        
        [dockIcon unlockFocus];
    }
    
    //update dock badge
    if (baseChange || fSpeedBadge || speedBadge)
    {
        if (!dockIcon)
            dockIcon = [fDockIcon retain];
        [NSApp setApplicationIconImage: dockIcon];
        [dockIcon release];
    }
    fSpeedBadge = speedBadge;
    
    [fLock unlock];
}

- (void) clearBadge
{
    [NSApp setApplicationIconImage: [NSImage imageNamed: @"NSApplicationIcon"]];
    fCompleted = 0;
}

@end

@implementation Badger (Private)

//dock icon must have locked focus
- (void) badgeString: (NSString *) string forRect: (NSRect) rect
{
    NSSize stringSize = [string sizeWithAttributes: fAttributes];
    
    //string is in center of image
    rect.origin.x += (rect.size.width - stringSize.width) * 0.5;
    rect.origin.y += (rect.size.height - stringSize.height) * 0.5;
                        
    [string drawAtPoint: rect.origin withAttributes: fAttributes];
}

@end
