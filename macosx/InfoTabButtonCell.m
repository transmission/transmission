/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2011 Transmission authors and contributors
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

#import "InfoTabButtonCell.h"

@implementation InfoTabButtonCell

- (void) awakeFromNib
{
    [(NSMatrix *)[self controlView] setToolTip: [self title] forCell: self];
    
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    [nc addObserver: self selector: @selector(updateControlTint:)
        name: NSControlTintDidChangeNotification object: NSApp];
        
    fSelected = NO;
    
    //expects the icon to currently be set as the image
    fIcon = [[self image] retain];
    [self setSelectedTab: fSelected];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fIcon release];
    [super dealloc];
}

- (void) setSelectedTab: (BOOL) selected
{
    fSelected = selected;
    
    NSInteger row, col;
    [(NSMatrix *)[self controlView] getRow: &row column: &col ofCell: self];
    NSRect tabRect = [(NSMatrix *)[self controlView] cellFrameAtRow: row column: col];
    tabRect.origin.x = 0.0;
    tabRect.origin.y = 0.0;
    
    NSImage * tabImage = [[NSImage alloc] initWithSize: tabRect.size];
        
    [tabImage lockFocus];
    
    if (fSelected)
    {
        NSColor * lightColor = [NSColor colorForControlTint: [NSColor currentControlTint]];
        NSColor * darkColor = [lightColor blendedColorWithFraction: 0.2 ofColor: [NSColor blackColor]];
        NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        [gradient drawInRect: tabRect angle: 270.0];
        [gradient release];
    }
    else
    {
        NSColor * lightColor = [NSColor colorWithCalibratedRed: 255.0/255.0 green: 255.0/255.0 blue: 255.0/255.0 alpha: 1.0];
        NSColor * darkColor = [NSColor colorWithCalibratedRed: 225.0/255.0 green: 225.0/255.0 blue: 225.0/255.0 alpha: 1.0];
        NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        [gradient drawInRect: tabRect angle: 270.0];
        [gradient release];
    }
    
    [[NSColor grayColor] set];
    NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(tabRect), 1.0));
    NSRectFill(NSMakeRect(0.0, NSHeight(tabRect) - 1.0, NSWidth(tabRect), 1.0));
    NSRectFill(NSMakeRect(NSWidth(tabRect) - 1.0, 1.0, NSWidth(tabRect) - 1.0, NSHeight(tabRect) - 2.0));
    
    if (fIcon)
    {
        const NSSize iconSize = [fIcon size];
        
        const NSRect iconRect = NSMakeRect(floor((NSWidth(tabRect) - iconSize.width) * 0.5),
                                            floor((NSHeight(tabRect) - iconSize.height) * 0.5),
                                            iconSize.width, iconSize.height);
        
        [fIcon drawInRect: iconRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
        [tabImage unlockFocus];
    }
    
    [self setImage: tabImage];
    [tabImage release];
}

- (void) updateControlTint: (NSNotification *) notification
{
    if (fSelected)
        [self setSelectedTab: YES];
}

@end
