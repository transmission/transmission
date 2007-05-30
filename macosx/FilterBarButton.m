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

@interface FilterBarButton (Private)

- (void) createButtons;

@end

@implementation FilterBarButton

- (id) initWithCoder: (NSCoder *) coder
{
    if ((self = [super initWithCoder: coder]))
    {
        fEnabled = NO;
        fTrackingTag = 0;
        
        [self createButtons];
        [self setCount: 0];
        [self setAlternateImage: fButtonPressed];
        [self setImage: fButtonNormal];
        
        NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
        
        [nc addObserver: self selector: @selector(setForActive:)
                    name: NSWindowDidBecomeKeyNotification object: [self window]];
        
        [nc addObserver: self selector: @selector(setForInactive:)
                    name: NSWindowDidResignKeyNotification object: [self window]];
        
        [nc addObserver: self selector: @selector(resetBounds:)
                    name: NSViewFrameDidChangeNotification object: nil];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fButtonNormal release];
    [fButtonOver release];
    [fButtonPressed release];
    [fButtonSelected release];
    [fButtonSelectedDim release];
    
    [super dealloc];
}

- (void) setCount: (int) count
{
    [self setToolTip: count == 1 ? NSLocalizedString(@"1 Transfer", "Filter Bar Button -> tool tip")
            : [NSString stringWithFormat: NSLocalizedString(@"%d Transfers", "Filter Bar Button -> tool tip"), count]];
}

- (void) mouseEntered: (NSEvent *) event
{
    if (!fEnabled)
        [self setImage: fButtonOver];
    
    [super mouseEntered: event];
}

- (void) mouseExited: (NSEvent *) event
{
    if (!fEnabled)
        [self setImage: fButtonNormal];

    [super mouseExited: event];
}

- (void) setEnabled: (BOOL) enable
{
    fEnabled = enable;
    [self setImage: fEnabled ? fButtonSelected : fButtonNormal];
}

- (void) resetBounds: (NSNotification *) notification
{
    if (fTrackingTag)
        [self removeTrackingRect: fTrackingTag];
    fTrackingTag = [self addTrackingRect: [self bounds] owner: self userData: nil assumeInside: NO];
}

- (void) setForActive: (NSNotification *) notification
{
    [self setImage: fEnabled ? fButtonSelected : fButtonNormal];
    [self resetBounds: nil];
}

- (void) setForInactive: (NSNotification *) notification
{
    [self setImage: fEnabled ? fButtonSelectedDim : fButtonNormalDim];
    
    if (fTrackingTag)
    {
        [self removeTrackingRect: fTrackingTag];
        fTrackingTag = 0;
    }
}

@end

@implementation FilterBarButton (Private)

- (void) createButtons
{
    //create attributes
    NSFont * boldFont = [[NSFontManager sharedFontManager] convertFont:
                            [NSFont fontWithName: @"Lucida Grande" size: 12.0] toHaveTrait: NSBoldFontMask];
    
    NSSize shadowOffset = NSMakeSize(0.0, -1.0);
    
    NSShadow * shadowNormal = [NSShadow alloc];
    [shadowNormal setShadowOffset: shadowOffset];
    [shadowNormal setShadowBlurRadius: 1.0];
    [shadowNormal setShadowColor: [NSColor colorWithDeviceWhite: 1.0 alpha: 0.4]];

    NSShadow * shadowDim = [NSShadow alloc];
    [shadowDim setShadowOffset: shadowOffset];
    [shadowDim setShadowBlurRadius: 1.0];
    [shadowDim setShadowColor: [NSColor colorWithDeviceWhite: 1.0 alpha: 0.2]];
    
    NSShadow * shadowHighlighted = [NSShadow alloc];
    [shadowHighlighted setShadowOffset: shadowOffset];
    [shadowHighlighted setShadowBlurRadius: 1.0];
    [shadowHighlighted setShadowColor: [NSColor colorWithDeviceWhite: 0.0 alpha: 0.4]];
    
    NSDictionary * normalAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor colorWithCalibratedRed: 0.259 green: 0.259 blue: 0.259 alpha: 1.0],
                NSForegroundColorAttributeName,
                boldFont, NSFontAttributeName,
                shadowNormal, NSShadowAttributeName, nil],
        * normalDimAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor disabledControlTextColor], NSForegroundColorAttributeName,
                boldFont, NSFontAttributeName,
                shadowDim, NSShadowAttributeName, nil],
        * highlightedAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor whiteColor], NSForegroundColorAttributeName,
                boldFont, NSFontAttributeName,
                shadowHighlighted, NSShadowAttributeName, nil],
        * highlightedDimAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor colorWithCalibratedRed: 0.9 green: 0.9 blue: 0.9 alpha: 1.0], NSForegroundColorAttributeName,
                boldFont, NSFontAttributeName,
                shadowHighlighted, NSShadowAttributeName, nil];
    
    [shadowNormal release];
    [shadowDim release];
    [shadowHighlighted release];
    
    //create button text
    NSString * text = [self title],
            * number = fCount > 0 ? [NSString stringWithFormat: @" (%d)", fCount] : nil;
    
    //get images
    NSImage * leftOver = [NSImage imageNamed: @"FilterButtonOverLeft.png"],
            * rightOver = [NSImage imageNamed: @"FilterButtonOverRight.png"],
            * mainOver = [NSImage imageNamed: @"FilterButtonOverMain.png"];
    
    NSImage * leftPressed = [NSImage imageNamed: @"FilterButtonPressedLeft.png"],
            * rightPressed = [NSImage imageNamed: @"FilterButtonPressedRight.png"],
            * mainPressed = [NSImage imageNamed: @"FilterButtonPressedMain.png"];
    
    NSImage * leftSelected = [NSImage imageNamed: @"FilterButtonSelectedLeft.png"],
            * rightSelected = [NSImage imageNamed: @"FilterButtonSelectedRight.png"],
            * mainSelected = [NSImage imageNamed: @"FilterButtonSelectedMain.png"];
    
    //get button sizes and placement
    NSSize textSize = [text sizeWithAttributes: normalAttributes];
    textSize.width = ceilf(textSize.width);
    
    float overlap = 7.0;
    NSSize endSize = [leftOver size],
            mainSize = NSMakeSize(textSize.width - overlap * 2.0, endSize.height),
            buttonSize = NSMakeSize(mainSize.width + 2.0 * endSize.width, endSize.height);
    NSRect textRect = NSMakeRect(endSize.width - overlap, (buttonSize.height - textSize.height) * 0.5 + 1.5,
                                textSize.width, textSize.height);
    
    NSPoint leftPoint = NSZeroPoint,
            mainPoint = NSMakePoint(endSize.width, 0),
            rightPoint = NSMakePoint(mainPoint.x + mainSize.width, 0);
    
    fButtonNormal = [[NSImage alloc] initWithSize: buttonSize];
    fButtonNormalDim = [[NSImage alloc] initWithSize: buttonSize];
    fButtonOver = [[NSImage alloc] initWithSize: buttonSize];
    fButtonPressed = [[NSImage alloc] initWithSize: buttonSize];
    fButtonSelected = [[NSImage alloc] initWithSize: buttonSize];
    fButtonSelectedDim = [[NSImage alloc] initWithSize: buttonSize];
    
    //rolled over button
    [mainOver setScalesWhenResized: YES];
    [mainOver setSize: mainSize];
    
    [fButtonOver lockFocus];
    [leftOver compositeToPoint: leftPoint operation: NSCompositeSourceOver];
    [mainOver compositeToPoint: mainPoint operation: NSCompositeSourceOver];
    [rightOver compositeToPoint: rightPoint operation: NSCompositeSourceOver];
    [fButtonOver unlockFocus];
    
    //pressed button
    [mainPressed setScalesWhenResized: YES];
    [mainPressed setSize: mainSize];
    
    [fButtonPressed lockFocus];
    [leftPressed compositeToPoint: leftPoint operation: NSCompositeSourceOver];
    [mainPressed compositeToPoint: mainPoint operation: NSCompositeSourceOver];
    [rightPressed compositeToPoint: rightPoint operation: NSCompositeSourceOver];
    [fButtonPressed unlockFocus];
    
    //selected button
    [mainSelected setScalesWhenResized: YES];
    [mainSelected setSize: mainSize];
    
    [fButtonSelected lockFocus];
    [leftSelected compositeToPoint: leftPoint operation: NSCompositeSourceOver];
    [mainSelected compositeToPoint: mainPoint operation: NSCompositeSourceOver];
    [rightSelected compositeToPoint: rightPoint operation: NSCompositeSourceOver];
    [fButtonSelected unlockFocus];
    
    //selected and dimmed button
    fButtonSelectedDim = [fButtonSelected copy];
    
    //normal button
    [fButtonNormal lockFocus];
    [text drawInRect: textRect withAttributes: normalAttributes];
    [fButtonNormal unlockFocus];
    
    //normal and dim button
    [fButtonNormalDim lockFocus];
    [text drawInRect: textRect withAttributes: normalDimAttributes];
    [fButtonNormalDim unlockFocus];
    
    //rolled over button
    [fButtonOver lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonOver unlockFocus];
    
    //pressed button
    [fButtonPressed lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonPressed unlockFocus];
    
    //selected button
    [fButtonSelected lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonSelected unlockFocus];
    
    //selected and dim button
    [fButtonSelectedDim lockFocus];
    [text drawInRect: textRect withAttributes: highlightedDimAttributes];
    [fButtonSelectedDim unlockFocus];
    
    [normalAttributes release];
    [normalDimAttributes release];
    [highlightedAttributes release];
    [highlightedDimAttributes release];
    
    //resize button
    NSPoint point = [self frame].origin;
    [self setFrame: NSMakeRect(point.x, point.y, buttonSize.width, buttonSize.height)];
}

@end
