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
#import "StringAdditions.h"

@interface FilterBarButton (Private)

- (NSImage *) badgeCount: (int) count color: (NSColor *) color;

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

- (BOOL) setCount: (int) count
{
    if (fCount == count)
        return NO;
    fCount = count;
    
    if (fButtonNormal)
    {
        [fButtonNormal release];
        [fButtonOver release];
        [fButtonPressed release];
        [fButtonSelected release];
        [fButtonSelectedDim release];
    }
    
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
    
    //create badge images and adjust size
    NSImage * badgeNormal, * badgeNormalDim, * badgeHighlighted, * badgeHighlightedDim;
    NSSize badgeSize;
    float badgePadding;
    if (fCount > 0)
    {
        badgeNormal = [self badgeCount: fCount color: [normalAttributes objectForKey: NSForegroundColorAttributeName]];
        badgeNormalDim = [self badgeCount: fCount color: [normalDimAttributes objectForKey: NSForegroundColorAttributeName]];
        badgeHighlighted = [self badgeCount: fCount
                            color: [highlightedAttributes objectForKey: NSForegroundColorAttributeName]];
        badgeHighlightedDim = [self badgeCount: fCount
                                color: [highlightedDimAttributes objectForKey: NSForegroundColorAttributeName]];
        
        badgeSize = [badgeNormal size];
        badgeSize.width = ceilf(badgeSize.width);
        
        badgePadding = 2.0;
        float increase = badgeSize.width + badgePadding;
        mainSize.width += increase;
        buttonSize.width += increase;
    }
    
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
    
    //append count
    if (fCount > 0)
    {
        NSPoint badgePoint = NSMakePoint(NSMaxX(textRect) + badgePadding * 2.0, (mainSize.height - badgeSize.height) * 0.5 + 1.0);
        
        //normal button
        [fButtonNormal lockFocus];
        [badgeNormal compositeToPoint: badgePoint operation: NSCompositeSourceOver];
        [fButtonNormal unlockFocus];
        
        //dim button
        [fButtonNormalDim lockFocus];
        [badgeNormalDim compositeToPoint: badgePoint operation: NSCompositeSourceOver];
        [fButtonNormalDim unlockFocus];
        
        //rolled over button
        [fButtonOver lockFocus];
        [badgeHighlighted compositeToPoint: badgePoint operation: NSCompositeSourceOver];
        [fButtonOver unlockFocus];
        
        [fButtonPressed lockFocus];
        [badgeHighlighted compositeToPoint: badgePoint operation: NSCompositeSourceOver];
        [fButtonPressed unlockFocus];
        
        //selected button
        [fButtonSelected lockFocus];
        [badgeHighlighted compositeToPoint: badgePoint operation: NSCompositeSourceOver];
        [fButtonSelected unlockFocus];
        
        //selected and dim button
        [fButtonSelectedDim lockFocus];
        [badgeHighlightedDim compositeToPoint: badgePoint operation: NSCompositeSourceOver];
        [fButtonSelectedDim unlockFocus];
    }
    
    [normalAttributes release];
    [normalDimAttributes release];
    [highlightedAttributes release];
    [highlightedDimAttributes release];
    
    //resize button
    NSPoint point = [self frame].origin;
    [self setFrame: NSMakeRect(point.x, point.y, buttonSize.width, buttonSize.height)];
    
    //set image
    [self setAlternateImage: fButtonPressed];
    
    if ([[self window] isKeyWindow])
        [self setImage: fEnabled ? fButtonSelected : fButtonNormal];
    else
        [self setImage: fEnabled ? fButtonSelectedDim : fButtonNormalDim];
    
    return YES;
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

- (NSImage *) badgeCount: (int) count color: (NSColor *) color
{
    NSDictionary * attributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    [[NSFontManager sharedFontManager] convertFont: [NSFont fontWithName: @"Lucida Grande" size: 10.0]
                        toHaveTrait: NSBoldFontMask], NSFontAttributeName, nil];
    
    NSString * string = [NSString stringWithInt: count];
    NSSize stringSize = [string sizeWithAttributes: attributes];
    NSRect badgeRect = NSMakeRect(0, 0, stringSize.width + 6.0, stringSize.height);
    
    //create badge part
    NSImage * tempBadge = [[NSImage alloc] initWithSize: badgeRect.size];
    NSBezierPath * bp = [NSBezierPath bezierPathWithOvalInRect: badgeRect];
    [tempBadge lockFocus];
    
    [color set];
    [bp fill];
    
    [tempBadge unlockFocus];
    
    //create string part
    NSImage * badge = [[NSImage alloc] initWithSize: badgeRect.size];
    [badge lockFocus];
    
    [string drawAtPoint: NSMakePoint((badgeRect.size.width - stringSize.width) * 0.5,
                        (badgeRect.size.height - stringSize.height) * 0.5) withAttributes: attributes];
    [tempBadge compositeToPoint: badgeRect.origin operation: NSCompositeSourceOut];
    
    [badge unlockFocus];
    
    [tempBadge release];
    [attributes release];
    
    return [badge autorelease];
}

@end
