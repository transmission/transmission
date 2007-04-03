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

- (id) init
{
    if ((self = [super init]))
    {
        fDockIcon = [NSImage imageNamed: @"NSApplicationIcon"];
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
    }
    
    return self;
}

- (void) dealloc
{
    [fAttributes release];
    [super dealloc];
}

- (void) updateBadgeWithCompleted: (int) completed uploadRate: (float) uploadRate downloadRate: (float) downloadRate
{
    NSImage * dockIcon = nil;
        
    //set completed badge to top right
    if (completed > 0)
    {
        dockIcon = [fDockIcon copy];
    
        NSRect badgeRect;
        NSSize iconSize = [dockIcon size];
        badgeRect.size = [fBadge size];
        badgeRect.origin.x = iconSize.width - badgeRect.size.width;
        badgeRect.origin.y = iconSize.height - badgeRect.size.height;
                                    
        [dockIcon lockFocus];
        
        //place badge
        [fBadge compositeToPoint: badgeRect.origin operation: NSCompositeSourceOver];
        
        //ignore shadow of badge when placing string
        float badgeBottomExtra = 5.0;
        badgeRect.size.height -= badgeBottomExtra;
        badgeRect.origin.y += badgeBottomExtra;
        
        //place badge text
        [self badgeString: [NSString stringWithInt: completed] forRect: badgeRect];
                    
        [dockIcon unlockFocus];
    }

    //set upload and download rate badges
    NSString * uploadRateString = uploadRate >= 0.1 && [[NSUserDefaults standardUserDefaults]
                    boolForKey: @"BadgeUploadRate"] ? [NSString stringForSpeedAbbrev: uploadRate] : nil,
            * downloadRateString = downloadRate >= 0.1 && [[NSUserDefaults standardUserDefaults]
                    boolForKey: @"BadgeDownloadRate"] ? [NSString stringForSpeedAbbrev: downloadRate] : nil;
    
    if (uploadRateString || downloadRateString)
    {
        if (!dockIcon)
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
    if (!dockIcon)
        dockIcon = [fDockIcon retain];
    
    [NSApp setApplicationIconImage: dockIcon];
    [dockIcon release];
}

- (void) clearBadge
{
    [NSApp setApplicationIconImage: fDockIcon];
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
