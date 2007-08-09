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

#import "FilterBarButton.h"
#import "CTGradient.h"
#import "CTGradientAdditions.h"
#import "BezierPathAdditions.h"

@interface FilterBarButton (Private)

- (void) createPaths;
- (void) createFontAttributes;

@end

@implementation FilterBarButton

- (id) initWithCoder: (NSCoder *) coder
{
    if ((self = [super initWithCoder: coder]))
    {
        fEnabled = NO;
        fTrackingTag = 0;
        fCount = -1;
        [self setCount: 0];
        [self createPaths];
        [self createFontAttributes];
        
        NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
        [nc addObserver: self selector: @selector(setForActive:) name: NSWindowDidBecomeKeyNotification object: [self window]];
        [nc addObserver: self selector: @selector(setForInactive:) name: NSWindowDidResignKeyNotification object: [self window]];
        [nc addObserver: self selector: @selector(resetBounds:) name: NSViewFrameDidChangeNotification object: nil];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fPath release];
    [fEdgePath release];
    [fStepPath release];
    [fNormalAttributes release];
    [fNormalDimAttributes release];
    [fHighlightedAttributes release];
    [fHighlightedDimAttributes release];
    
    [super dealloc];
}

- (void)sizeToFit
{
    NSSize size = [[self title] sizeWithAttributes: fNormalAttributes];
    size.width = floorf(size.width + 18.5);
    size.height += 1;
    [self setFrameSize: size];
    [self setBoundsSize: size];
    [self createPaths];
}

- (BOOL)isFlipped
{
    return NO;
}

- (void)drawRect:(NSRect)rect
{
    // draw background
    if ([[self cell] isHighlighted])
    {
        [[CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedRed: 134.0/255 green: 151.0/255
                                                    blue: 176.0/255 alpha: 1.0]
                                    endingColor: [NSColor colorWithCalibratedRed: 104.0/255 green: 125.0/255
                                                    blue: 157.0/255 alpha: 1.0]]
                                 fillBezierPath: fPath angle: -90.0];
        [[CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.25]
//                                  middleColor: [NSColor colorWithCalibratedWhite: 0.5 alpha: 0.00]
                                   middleColor1: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.25]
                                   middleColor2: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.50]
                                    endingColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.50]]
                                 fillBezierPath: fStepPath angle: -90.0];
    }
    else switch (fState)
    {
        case 1:     // active
            [[CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedRed: 151.0/255 green: 166.0/255
                                                        blue: 188.0/255 alpha: 1.0]
                                        endingColor: [NSColor colorWithCalibratedRed: 126.0/255 green: 144.0/255
                                                        blue: 171.0/255 alpha: 1.0]]
                                     fillBezierPath: fPath angle: -90.0];
            [[CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.25]
//                                      middleColor: [NSColor colorWithCalibratedWhite: 0.5 alpha: 0.00]
                                       middleColor1: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.25]
                                       middleColor2: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.50]
                                        endingColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.50]]
                                     fillBezierPath: fStepPath angle: -90.0];
            break;
        case 2:     // hovering
            [[CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedRed: 164.0/255 green: 177.0/255
                                                        blue: 196.0/255 alpha: 1.0]
                                        endingColor: [NSColor colorWithCalibratedRed: 141.0/255 green: 158.0/255
                                                        blue: 182.0/255 alpha: 1.0]]
                                     fillBezierPath: fPath angle: -90.0];
            [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.075] set];
            [fEdgePath stroke];
            break;
        case 3:     // clicked but cell is not highlighted
            break;
    }
    
    // draw title
    NSSize titleSize = [[self title] sizeWithAttributes: fNormalAttributes];
    NSPoint titlePos = NSMakePoint(([self bounds].size.width  - titleSize.width)  * 0.5,
                                   ([self bounds].size.height - titleSize.height) * 0.5 + 1.5);
    if (fEnabled)
    {
        if (fState && !(fState == 3 && ![[self cell] isHighlighted]))
            [[self title] drawAtPoint: titlePos withAttributes: fHighlightedAttributes];
        else
            [[self title] drawAtPoint: titlePos withAttributes: fNormalAttributes];
    }
    else
    {
        if (fState)
            [[self title] drawAtPoint: titlePos withAttributes: fHighlightedDimAttributes];
        else
            [[self title] drawAtPoint: titlePos withAttributes: fNormalDimAttributes];
    }
}

- (void) setCount: (int) count
{
    if (count == fCount)
        return;
    fCount = count;
    
    [self setToolTip: fCount == 1 ? NSLocalizedString(@"1 Transfer", "Filter Bar Button -> tool tip")
            : [NSString stringWithFormat: NSLocalizedString(@"%d Transfers", "Filter Bar Button -> tool tip"), fCount]];
}

- (void) mouseDown: (NSEvent *) event
{
    if ([self state] != 1)
        [self setState: 3];
    [super mouseDown: event];
}

- (void) mouseUp: (NSEvent *) event
{
    [super mouseUp: event];
    if ([self state] != 1)
        [self setState: 1];
}

- (void) mouseEntered: (NSEvent *) event
{
    [super mouseEntered: event];
    if ([self state] == 0)
        [self setState: 2];
}

- (void) mouseExited: (NSEvent *) event
{
    [super mouseExited: event];
    if ([self state] >= 2)
        [self setState: 0];
}

- (void) setEnabled: (BOOL) enable
{
    fEnabled = enable;
    [self setNeedsDisplay: YES];
}

- (int) state
{
    return fState;
}

- (void) setState: (int) state
{
    fState = state;
    [self setNeedsDisplay: YES];
}

- (void) resetBounds: (NSNotification *) notification
{
    if (fTrackingTag)
        [self removeTrackingRect: fTrackingTag];
    fTrackingTag = [self addTrackingRect: [self bounds] owner: self userData: nil assumeInside: NO];
}

- (void) setForActive: (NSNotification *) notification
{
    [self setEnabled: YES];
    [self resetBounds: nil];
    [self setNeedsDisplay: YES];
}

- (void) setForInactive: (NSNotification *) notification
{
    if (fTrackingTag)
    {
        [self removeTrackingRect: fTrackingTag];
        fTrackingTag = 0;
    }
    [self setEnabled: NO];
    [self setNeedsDisplay: YES];
}

@end

@implementation FilterBarButton (Private)

- (void) createPaths
{
    NSSize buttonSize = [self frame].size;
    
    // the main button path
    [fPath release];
    fPath = [[NSBezierPath bezierPathWithRoundedRect: NSMakeRect(0.0, 1.0, buttonSize.width, buttonSize.height - 1.0)
                radius: (buttonSize.height - 1.0) / 2.0] retain];
    
    // the path used to draw the hover edging
    [fEdgePath release];
    fEdgePath = [[NSBezierPath bezierPathWithRoundedRect: NSMakeRect(0.5, 1.5, buttonSize.width - 1.0, buttonSize.height - 2.0)
                    radius: (buttonSize.height - 2.0) / 2.0] retain];
    
    // the path used to draw the depressed shading/highlights of the active button
    [fStepPath release];
    fStepPath = [[NSBezierPath bezierPathWithRoundedRect: NSMakeRect(0.0, 0.0, buttonSize.width, buttonSize.height - 1.0)
                    radius: (buttonSize.height - 1.0) / 2.0] retain];
    [fStepPath appendBezierPath: fPath];
    [fStepPath setWindingRule: NSEvenOddWindingRule];
}

- (void) createFontAttributes
{
    NSFont * boldSystemFont = [NSFont boldSystemFontOfSize: 12.0];
    NSSize shadowOffset = NSMakeSize(0.0, -1.0);
    
    NSShadow * shadowNormal = [[[NSShadow alloc] init] autorelease]; 
    [shadowNormal setShadowOffset: shadowOffset]; 
    [shadowNormal setShadowBlurRadius: 1.0]; 
    [shadowNormal setShadowColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.4]]; 
    
    NSShadow * shadowNormalDim = [[[NSShadow alloc] init] autorelease]; 
    [shadowNormalDim setShadowOffset: shadowOffset]; 
    [shadowNormalDim setShadowBlurRadius: 1.0]; 
    [shadowNormalDim setShadowColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.2]]; 
    
    NSShadow * shadowHighlighted = [[[NSShadow alloc] init] autorelease]; 
    [shadowHighlighted setShadowOffset: shadowOffset]; 
    [shadowHighlighted setShadowBlurRadius: 1.0]; 
    [shadowHighlighted setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.4]]; 
    
    fNormalAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
        [NSColor colorWithCalibratedWhite: 0.259 alpha: 1.0], NSForegroundColorAttributeName,
        boldSystemFont, NSFontAttributeName,
        shadowNormal, NSShadowAttributeName, nil];
    
    fNormalDimAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
        [NSColor disabledControlTextColor], NSForegroundColorAttributeName,
        boldSystemFont, NSFontAttributeName,
        shadowNormalDim, NSShadowAttributeName, nil];
    
    fHighlightedAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
        [NSColor whiteColor], NSForegroundColorAttributeName,
        boldSystemFont, NSFontAttributeName,
        shadowHighlighted, NSShadowAttributeName, nil];
    
    fHighlightedDimAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
        [NSColor colorWithCalibratedWhite: 0.9 alpha: 1.0], NSForegroundColorAttributeName,
        boldSystemFont, NSFontAttributeName,
        shadowHighlighted, NSShadowAttributeName, nil];
}

@end
