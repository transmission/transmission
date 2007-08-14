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

#import "FileNameCell.h"
#import "InfoWindowController.h"
#import "StringAdditions.h"

#define PADDING_HORIZONAL 2.0
#define IMAGE_FOLDER_SIZE 16.0
#define IMAGE_ICON_SIZE 32.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 4.0
#define PADDING_ABOVE_TITLE_FILE 2.0
#define PADDING_BELOW_STATUS_FILE 2.0

@interface FileNameCell (Private)

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color;
- (NSAttributedString *) attributedStatusWithColor: (NSColor *) color;

@end

@implementation FileNameCell

- (NSImage *) image
{
    NSImage * image = [[self objectValue] objectForKey: @"Icon"];
    if (!image)
    {
        if (!fFolderImage)
        {
            fFolderImage = [[[NSWorkspace sharedWorkspace] iconForFileType: NSFileTypeForHFSTypeCode('fldr')] copy];
            [fFolderImage setFlipped: YES];
        }
        image = fFolderImage;
    }
    return image;
}

- (NSRect) imageRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    
    result.origin.x += PADDING_HORIZONAL;
    
    const float IMAGE_SIZE = [[[self objectValue] objectForKey: @"IsFolder"] boolValue] ? IMAGE_FOLDER_SIZE : IMAGE_ICON_SIZE;
    result.origin.y += (result.size.height - IMAGE_SIZE) * 0.5;
    result.size = NSMakeSize(IMAGE_SIZE, IMAGE_SIZE);
    
    return result;
}

- (NSRect) titleRectForBounds: (NSRect) bounds
{
    NSAttributedString * title = [self attributedTitleWithColor: nil];
    NSSize titleSize = [title size];
    
    NSRect result = bounds;
    
    if (![[[self objectValue] objectForKey: @"IsFolder"] boolValue])
    {
        result.origin.x += PADDING_HORIZONAL + IMAGE_ICON_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y += PADDING_ABOVE_TITLE_FILE;
    }
    else
    {
        result.origin.x += PADDING_HORIZONAL + IMAGE_FOLDER_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y += (result.size.height - titleSize.height) * 0.5;
    }
    result.size = titleSize;
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    if ([[[self objectValue] objectForKey: @"IsFolder"] boolValue])
        return NSZeroRect;
    
    NSAttributedString * status = [self attributedStatusWithColor: nil];
    NSSize statusSize = [status size];
    
    NSRect result = bounds;
    
    result.origin.x += PADDING_HORIZONAL + IMAGE_ICON_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
    result.origin.y += result.size.height - PADDING_BELOW_STATUS_FILE - statusSize.height;
    
    result.size = statusSize;
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //icon
    NSImage * icon = [self image];
    NSSize iconSize = [icon size];
    [icon drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
            operation: NSCompositeSourceOver fraction: 1.0];
    
    //title
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                                isEqual: [NSColor alternateSelectedControlColor]];
    
    #warning mimic torrent cell
    [[self attributedTitleWithColor: highlighted ? [NSColor whiteColor] : [NSColor controlTextColor]]
        drawInRect: [self titleRectForBounds: cellFrame]];
    
    //status
    NSRect statusRect = [self statusRectForBounds: cellFrame];
    if (!NSEqualRects(statusRect, NSZeroRect))
        [[self attributedStatusWithColor: highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor]] drawInRect: statusRect];
}

@end

@implementation FileNameCell (Private)

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color
{
    if (!fTitleAttributes)
    {
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
        
        fTitleAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                            [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                            paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        [paragraphStyle release];
    }
    
    if (color)
        [fTitleAttributes setObject: color forKey: NSForegroundColorAttributeName];
        
    NSString * title = [[self objectValue] objectForKey: @"Name"];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusWithColor: (NSColor *) color
{
    if (!fStatusAttributes)
    {
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
        
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        [paragraphStyle release];
    }
    
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    Torrent * torrent = [[[[self controlView] window] windowController] selectedTorrent];
    float percent = [torrent fileProgress: [[[self objectValue] objectForKey: @"Indexes"] firstIndex]] * 100.0;
    
    NSString * status = [NSString stringWithFormat: NSLocalizedString(@"%.2f%% of %@",
                            "Inspector -> Files tab -> file status string"), percent,
                            [NSString stringForFileSize: [[[self objectValue] objectForKey: @"Size"] unsignedLongLongValue]]];
    
    return [[[NSAttributedString alloc] initWithString: status attributes: fStatusAttributes] autorelease];
}

@end
