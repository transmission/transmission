/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2006-2010 Transmission authors and contributors
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

#import "StatusBarView.h"

@interface StatusBarView (Private)

- (void) reload;

@end

@implementation StatusBarView

- (id) initWithFrame: (NSRect) rect
{
    if ((self = [super initWithFrame: rect]))
    {
        fIsFilter = NO;
        fGrayBorderColor = [[NSColor colorWithCalibratedRed: 171.0/255.0 green: 171.0/255.0 blue: 171.0/255.0 alpha: 1.0] retain];
        
        NSColor * lightColor = [NSColor colorWithCalibratedRed: 230.0/255.0 green: 230.0/255.0 blue: 230.0/255.0 alpha: 1.0];
        NSColor * darkColor = [NSColor colorWithCalibratedRed: 220.0/255.0 green: 220.0/255.0 blue: 220.0/255.0 alpha: 1.0];
        fInactiveGradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        
        lightColor = [NSColor colorWithCalibratedRed: 160.0/255.0 green: 160.0/255.0 blue: 160.0/255.0 alpha: 1.0];
        darkColor = [NSColor colorWithCalibratedRed: 155.0/255.0 green: 155.0/255.0 blue: 155.0/255.0 alpha: 1.0];
        fStatusGradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        
        lightColor = [NSColor colorWithCalibratedRed: 235.0/255.0 green: 235.0/255.0 blue: 235.0/255.0 alpha: 1.0];
        darkColor = [NSColor colorWithCalibratedRed: 205.0/255.0 green: 205.0/255.0 blue: 205.0/255.0 alpha: 1.0];
        fFilterGradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(reload)
            name: NSWindowDidBecomeMainNotification object: [self window]];
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(reload)
            name: NSWindowDidResignMainNotification object: [self window]];
    }
    return self;
}

- (void) dealloc
{
    [fGrayBorderColor release];
    [fStatusGradient release];
    [fInactiveGradient release];
    [fFilterGradient release];
    [super dealloc];
}

- (BOOL) mouseDownCanMoveWindow
{
    return !fIsFilter;
}

#warning get rid of asap
- (void) setIsFilter: (BOOL) isFilter
{
    fIsFilter = isFilter;
}

- (void) drawRect: (NSRect) rect
{
    if (fIsFilter)
    {
        NSInteger count = 0;
        NSRect gridRects[2];
        NSColor * colorRects[2];
        
        NSRect lineBorderRect = NSMakeRect(NSMinX(rect), NSHeight([self bounds]) - 1.0, NSWidth(rect), 1.0);
        if ([[self window] isMainWindow])
        {
            if (NSIntersectsRect(lineBorderRect, rect))
            {
                gridRects[count] = lineBorderRect;
                colorRects[count] = [NSColor whiteColor];
                ++count;
                
                rect.size.height -= 1.0;
            }
        }
        
        lineBorderRect.origin.y = 0.0;
        if (NSIntersectsRect(lineBorderRect, rect))
        {
            gridRects[count] = lineBorderRect;
            colorRects[count] = [[self window] isMainWindow] ? [NSColor colorWithCalibratedWhite: 0.25 alpha: 1.0]
                                    : [NSColor colorWithCalibratedWhite: 0.5 alpha: 1.0];
            ++count;
            
            rect.origin.y += 1.0;
            rect.size.height -= 1.0;
        }
        
        NSRectFillListWithColors(gridRects, colorRects, count);
        
        [fFilterGradient drawInRect: rect angle: 270.0];
    }
    else
    {
        const BOOL active = [[self window] isMainWindow];
        
        NSInteger count = 0;
        NSRect gridRects[2];
        NSColor * colorRects[2];
        
        NSRect lineBorderRect = NSMakeRect(NSMinX(rect), NSHeight([self bounds]) - 1.0, NSWidth(rect), 1.0);
        if (active)
        {
            if (NSIntersectsRect(lineBorderRect, rect))
            {
                gridRects[count] = lineBorderRect;
                colorRects[count] = [NSColor colorWithCalibratedWhite: 0.75 alpha: 1.0];
                ++count;
                
                rect.size.height -= 1.0;
            }
        }
        
        lineBorderRect.origin.y = 0.0;
        if (NSIntersectsRect(lineBorderRect, rect))
        {
            gridRects[count] = lineBorderRect;
            colorRects[count] = [[self window] isMainWindow] ? [NSColor colorWithCalibratedWhite: 0.25 alpha: 1.0]
                                    : [NSColor colorWithCalibratedWhite: 0.5 alpha: 1.0];
            ++count;
            
            rect.origin.y += 1.0;
            rect.size.height -= 1.0;
        }
        
        if (active)
            [fStatusGradient drawInRect: rect angle: 270.0];
        else
            [fInactiveGradient drawInRect: rect angle: 270.0];
        
        NSRectFillListWithColors(gridRects, colorRects, count);
    }
}

@end

@implementation StatusBarView (Private)

- (void) reload
{
    [self setNeedsDisplay: YES];
}

@end
