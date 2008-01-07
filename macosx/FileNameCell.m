/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2008 Transmission authors and contributors
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
#import "FileOutlineView.h"
#import "Torrent.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define PADDING_HORIZONAL 2.0
#define IMAGE_FOLDER_SIZE 16.0
#define IMAGE_ICON_SIZE 32.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 4.0
#define PADDING_ABOVE_TITLE_FILE 2.0
#define PADDING_BELOW_STATUS_FILE 2.0

@interface FileNameCell (Private)

- (NSRect) rectForTitleWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color;
- (NSAttributedString *) attributedStatusWithColor: (NSColor *) color;

@end

@implementation FileNameCell

- (id) init
{
    if ((self = [super init]))
    {
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
        
        fTitleAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                            [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                            paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        [paragraphStyle release];
    }
    return self;
}

- (void) dealloc
{
    [fTitleAttributes release];
    [fStatusAttributes release];
    
    [fFolderImage release];
    
    [super dealloc];
}

- (id) copyWithZone: (NSZone *) zone
{
    FileNameCell * copy = [super copyWithZone: zone];
    
    copy->fTitleAttributes = [fTitleAttributes retain];
    copy->fStatusAttributes = [fStatusAttributes retain];
    
    copy->fFolderImage = [fFolderImage retain];
    
    return copy;
}

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
    return [self rectForTitleWithString: [self attributedTitleWithColor: nil] inBounds: bounds];
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    return [self rectForStatusWithString: [self attributedStatusWithColor: nil] inBounds: bounds];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //icon
    [[self image] drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    //title
    NSColor * specialColor = nil;
    if ([NSApp isOnLeopardOrBetter] ? [self backgroundStyle] == NSBackgroundStyleDark : [self isHighlighted]
            && [[self highlightColorWithFrame: cellFrame inView: controlView] isEqual: [NSColor alternateSelectedControlColor]])
        specialColor = [NSColor whiteColor];
    else if ([[(FileOutlineView *)[self controlView] torrent] checkForFiles:
                    [[self objectValue] objectForKey: @"Indexes"]] == NSOffState)
        specialColor = [NSColor disabledControlTextColor];
    else;
    
    NSAttributedString * titleString = [self attributedTitleWithColor: specialColor ? specialColor : [NSColor controlTextColor]];
    NSRect titleRect = [self rectForTitleWithString: titleString inBounds: cellFrame];
    [titleString drawInRect: titleRect];
    
    //status
    if (![[[self objectValue] objectForKey: @"IsFolder"] boolValue])
    {
        NSAttributedString * statusString = [self attributedStatusWithColor: specialColor ? specialColor : [NSColor darkGrayColor]];
        NSRect statusRect = [self rectForStatusWithString: statusString inBounds: cellFrame];
        [statusString drawInRect: statusRect];
    }
}

@end

@implementation FileNameCell (Private)

- (NSRect) rectForTitleWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    NSSize titleSize = [string size];
    
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

- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([[[self objectValue] objectForKey: @"IsFolder"] boolValue])
        return NSZeroRect;
    
    NSSize statusSize = [string size];
    
    NSRect result = bounds;
    
    result.origin.x += PADDING_HORIZONAL + IMAGE_ICON_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
    result.origin.y += result.size.height - PADDING_BELOW_STATUS_FILE - statusSize.height;
    
    result.size = statusSize;
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color
{
    if (color)
        [fTitleAttributes setObject: color forKey: NSForegroundColorAttributeName];
        
    NSString * title = [[self objectValue] objectForKey: @"Name"];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusWithColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    Torrent * torrent = [(FileOutlineView *)[self controlView] torrent];
    float percent = [torrent fileProgress: [[[self objectValue] objectForKey: @"Indexes"] firstIndex]] * 100.0;
    
    NSString * status = [NSString stringWithFormat: NSLocalizedString(@"%.2f%% of %@",
                            "Inspector -> Files tab -> file status string"), percent,
                            [NSString stringForFileSize: [[[self objectValue] objectForKey: @"Size"] unsignedLongLongValue]]];
    
    return [[[NSAttributedString alloc] initWithString: status attributes: fStatusAttributes] autorelease];
}

@end
