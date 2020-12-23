/******************************************************************************
 * Copyright (c) 2006-2012 Transmission authors and contributors
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

#import <QuartzCore/QuartzCore.h>

#import "StatusBarView.h"
#import "NSApplicationAdditions.h"

@interface StatusBarView (Private)

- (void) reload;

@end

@implementation StatusBarView

- (id) initWithFrame: (NSRect) rect
{
    if ((self = [super initWithFrame: rect]))
    {
        NSColor * lightColor = [NSColor colorWithCalibratedRed: 160.0/255.0 green: 160.0/255.0 blue: 160.0/255.0 alpha: 1.0];
        NSColor * darkColor = [NSColor colorWithCalibratedRed: 155.0/255.0 green: 155.0/255.0 blue: 155.0/255.0 alpha: 1.0];
        fGradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        
        CIFilter * randomFilter = [CIFilter filterWithName: @"CIRandomGenerator"];
        [randomFilter setDefaults];
        
        fNoiseImage = [randomFilter valueForKey: @"outputImage"];
        
        CIFilter * monochromeFilter = [CIFilter filterWithName: @"CIColorMonochrome"];
        [monochromeFilter setDefaults];
        [monochromeFilter setValue: fNoiseImage forKey: @"inputImage"];
        CIColor * monoFilterColor = [CIColor colorWithRed: 1.0 green: 1.0 blue: 1.0];
        [monochromeFilter setValue: monoFilterColor forKey: @"inputColor"];
        fNoiseImage = [monochromeFilter valueForKey:@"outputImage"];
        
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(reload) name: NSWindowDidBecomeMainNotification object: [self window]];
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(reload) name: NSWindowDidResignMainNotification object: [self window]];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
}

- (BOOL) mouseDownCanMoveWindow
{
    return YES;
}

- (BOOL) isOpaque
{
    return YES;
}

- (void) drawRect: (NSRect) rect
{
    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(rect);
    
    const NSRect lineBorderRect = NSMakeRect(NSMinX(rect), 0.0, NSWidth(rect), 1.0);
    if (NSIntersectsRect(lineBorderRect, rect))
    {
        [[NSColor gridColor] setFill];
        NSRectFill(lineBorderRect);
    }
}

@end

@implementation StatusBarView (Private)

- (void) reload
{
    [self setNeedsDisplay: YES];
}

@end
