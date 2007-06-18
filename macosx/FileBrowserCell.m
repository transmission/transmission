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

#import "FileBrowserCell.h"
#import "StringAdditions.h"

#define SPACE 2.0

@implementation FileBrowserCell

- (void) awakeFromNib
{
    [self setLeaf: YES];
}

- (void) setImage: (NSImage *) image
{
    if (!image)
        image = [[[[NSWorkspace sharedWorkspace] iconForFileType: NSFileTypeForHFSTypeCode('fldr')] copy] autorelease];
    
    [image setFlipped: YES];
    [image setScalesWhenResized: YES];
    [super setImage: image];
}

- (void) setProgress: (float) progress
{
    fPercent = progress * 100.0;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    NSMutableDictionary * item;
    if (!(item = [self objectValue]))
        return;
    
    //image
    float imageHeight = cellFrame.size.height - 2.0;
    
    NSImage * image = [self image];
    [image setSize: NSMakeSize(imageHeight, imageHeight)];
    NSRect imageRect = NSMakeRect(cellFrame.origin.x + 2.0 * SPACE,
                                    cellFrame.origin.y + (cellFrame.size.height - imageHeight) / 2.0,
                                    imageHeight, imageHeight);
    
    [image drawInRect: imageRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    //text
    NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                    isEqual: [NSColor alternateSelectedControlColor]];
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor controlTextColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
    
    NSString * nameString = [item objectForKey: @"Name"];
    NSRect nameTextRect = NSMakeRect(NSMaxX(imageRect) + SPACE, cellFrame.origin.y + 1.0,
            NSMaxX(cellFrame) - NSMaxX(imageRect) - 2.0 * SPACE, [nameString sizeWithAttributes: nameAttributes].height);
    
    [nameString drawInRect: nameTextRect withAttributes: nameAttributes];
    [nameAttributes release];
    
    //status text
    if (![[item objectForKey: @"IsFolder"] boolValue])
    {
        NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                        highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor], NSForegroundColorAttributeName,
                        [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                        paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        NSString * statusString = [NSString stringWithFormat: NSLocalizedString(@"%.2f%% of %@",
                                    "Inspector -> Files tab -> file status string"), fPercent,
                                    [NSString stringForFileSize: [[item objectForKey: @"Size"] unsignedLongLongValue]]];
        
        NSRect statusTextRect = nameTextRect;
        statusTextRect.size.height = [statusString sizeWithAttributes: statusAttributes].height;
        statusTextRect.origin.y += (cellFrame.size.height + nameTextRect.size.height - statusTextRect.size.height) * 0.5 - 1.0;
        
        [statusString drawInRect: statusTextRect withAttributes: statusAttributes];
        [statusAttributes release];
    }
    
    [paragraphStyle release];
}

@end
