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

#import "BadgeView.h"
#import "NSStringAdditions.h"

#define BOTTOM_PADDING 2.0
#define BETWEEN_PADDING 2.0

@interface BadgeView (Private)

- (void) badgeString: (NSString *) string forRect: (NSRect) rect;

@end

@implementation BadgeView

- (id) initWithFrame: (NSRect) frame lib: (tr_handle *) lib
{
    if ((self = [super initWithFrame: frame]))
    {
        fLib = lib;
    }
    return self;
}

- (void) dealloc
{
    [fAttributes release];
    [super dealloc];
}

- (void) drawRect: (NSRect) rect
{
    [[NSImage imageNamed: @"NSApplicationIcon"] drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
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
        
        if (uploadRateString || downloadRateString)
        {
            NSRect badgeRect = NSZeroRect;
            badgeRect.size = [[NSImage imageNamed: @"UploadBadge"] size];
            
            //ignore shadow of badge when placing string
            NSRect stringRect = badgeRect;
            stringRect.size.height -= BOTTOM_PADDING;
            stringRect.origin.y += BOTTOM_PADDING;
            
            if (uploadRateString)
            {
                //place badge and text
                [[NSImage imageNamed: @"UploadBadge"] drawInRect: badgeRect fromRect: NSZeroRect
                                                        operation: NSCompositeSourceOver fraction: 1.0];
                [self badgeString: uploadRateString forRect: stringRect];
            }
            
            if (downloadRateString)
            {
                //download rate above upload rate
                if (uploadRateString)
                {
                    float spaceBetween = badgeRect.size.height + BETWEEN_PADDING;
                    badgeRect.origin.y += spaceBetween;
                    stringRect.origin.y += spaceBetween;
                }
                
                //place badge and text
                [[NSImage imageNamed: @"DownloadBadge"] drawInRect: badgeRect fromRect: NSZeroRect
                                                        operation: NSCompositeSourceOver fraction: 1.0];
                [self badgeString: downloadRateString forRect: stringRect];
            }
        }
    }
}

@end

@implementation BadgeView (Private)

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
