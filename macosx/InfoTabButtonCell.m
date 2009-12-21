/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2009 Transmission authors and contributors
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
        name: NSControlTintDidChangeNotification object: nil];
    
    fSelected = NO;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fIcon release];
    [super dealloc];
}

- (void) setIcon: (NSImage *) image
{
    [fIcon release];
    fIcon = [image retain];
    
    [self setSelectedTab: fSelected];
}

- (void) setSelectedTab: (BOOL) selected
{
    fSelected = selected;
    
    NSImage * tabImage;
    if (fSelected)
        tabImage = [NSColor currentControlTint] == NSGraphiteControlTint
                    ? [[NSImage imageNamed: @"InfoTabBackGraphite.png"] copy] : [[NSImage imageNamed: @"InfoTabBackBlue.png"] copy];
    else
        tabImage = [[NSImage imageNamed: @"InfoTabBack.png"] copy];
    
    if (fIcon)
    {
        const NSSize iconSize = [fIcon size], tabSize = [tabImage size];
        
        const NSRect iconRect = NSMakeRect(floor((tabSize.width - iconSize.width) * 0.5),
                                            floor((tabSize.height - iconSize.height) * 0.5),
                                            iconSize.width, iconSize.height);
        
        [tabImage lockFocus];
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
