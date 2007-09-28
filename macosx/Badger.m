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
#import "NSStringAdditions.h"

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
    }
    
    return self;
}

- (void) dealloc
{
    [fDockIcon release];
    [fAttributes release];
    
    [super dealloc];
}

- (void) updateBadge
{
    //set completed badge to top right
    BOOL baseChange;
    if (baseChange = (fCompleted != fCompletedBadged))
    {
        fCompletedBadged = fCompleted;
        
        //force image to reload - copy does not work
        NSImage * icon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
        NSSize iconSize = [icon size];
        
        if (fDockIcon)
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
            float badgeBottomExtra = 5.0;
            badgeRect.size.height -= badgeBottomExtra;
            badgeRect.origin.y += badgeBottomExtra;
            
            //place badge text
            [self badgeString: [NSString stringWithFormat: @"%d", fCompleted] forRect: badgeRect];
                        
            [fDockIcon unlockFocus];
        }
    }
    
    //set upload and download rate badges
    NSString * downloadRateString = nil, * uploadRateString = nil;
    
    BOOL checkDownload = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeDownloadRate"],
        checkUpload = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeUploadRate"];
    if (checkDownload || checkUpload)
    {
        float downloadRate, uploadRate;
        tr_torrentRates(fLib, &downloadRate, &uploadRate);
        
        if (checkDownload && downloadRate >= 0.1)
            downloadRateString = [NSString stringForSpeedAbbrev: downloadRate];
        if (checkUpload && uploadRate >= 0.1)
            uploadRateString = [NSString stringForSpeedAbbrev: uploadRate];
    }
    
    NSImage * dockIcon = nil;
    BOOL speedChange;
    if ((speedChange = (uploadRateString || downloadRateString)))
    {
        if (!fDockIcon)
            fDockIcon = [[NSImage imageNamed: @"NSApplicationIcon"] copy];
        dockIcon = [fDockIcon copy];
        
        if (!fUploadBadge)
            fUploadBadge = [NSImage imageNamed: @"UploadBadge"];
        if (!fDownloadBadge)
            fDownloadBadge = [NSImage imageNamed: @"DownloadBadge"];
        
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
    if (baseChange || fSpeedBadge || speedChange)
    {
        [NSApp setApplicationIconImage: dockIcon ? dockIcon : fDockIcon];
        if (dockIcon)
            [dockIcon release];
        
        fSpeedBadge = speedChange;
    }
}

- (void) incrementCompleted
{
    fCompleted++;
    [self updateBadge];
}

- (void) clearCompleted
{
    if (fCompleted != 0)
    {
        fCompleted = 0;
        [self updateBadge];
    }
}

- (void) clearBadge
{
    fCompleted = 0;
    fCompletedBadged = 0;
    fSpeedBadge = NO;
    [NSApp setApplicationIconImage: [NSImage imageNamed: @"NSApplicationIcon"]];
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
                        
    [string drawAtPoint: rect.origin withAttributes: fAttributes];
}

@end
