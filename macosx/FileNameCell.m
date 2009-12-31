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

#import "FileNameCell.h"
#import "FileOutlineView.h"
#import "Torrent.h"
#import "FileListNode.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define PADDING_HORIZONAL 2.0
#define IMAGE_FOLDER_SIZE 16.0
#define IMAGE_ICON_SIZE 32.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 4.0
#define PADDING_ABOVE_TITLE_FILE 2.0
#define PADDING_BELOW_STATUS_FILE 2.0
#define PADDING_BETWEEN_NAME_AND_FOLDER_STATUS 4.0

@interface FileNameCell (Private)

- (NSRect) rectForTitleWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithString: (NSAttributedString *) string withTitleRect: (NSRect) titleRect inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitle;
- (NSAttributedString *) attributedStatus;

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
    
    [super dealloc];
}

- (id) copyWithZone: (NSZone *) zone
{
    FileNameCell * copy = [super copyWithZone: zone];
    
    copy->fTitleAttributes = [fTitleAttributes retain];
    copy->fStatusAttributes = [fStatusAttributes retain];
    
    return copy;
}

- (NSImage *) image
{
    FileListNode * node = (FileListNode *)[self objectValue];
    return [node icon];
}

- (NSRect) imageRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    
    result.origin.x += PADDING_HORIZONAL;
    
    const CGFloat IMAGE_SIZE = [(FileListNode *)[self objectValue] isFolder] ? IMAGE_FOLDER_SIZE : IMAGE_ICON_SIZE;
    result.origin.y += (result.size.height - IMAGE_SIZE) * 0.5;
    result.size = NSMakeSize(IMAGE_SIZE, IMAGE_SIZE);
    
    return result;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //icon
    if ([NSApp isOnSnowLeopardOrBetter])
        [[self image] drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0
            respectFlipped: YES hints: nil];
    else
    {
        NSImage * image = [self image];
        [image setFlipped: YES];
        [image drawInRect: [self imageRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }
    
    NSColor * titleColor, * statusColor;
    if ([self backgroundStyle] == NSBackgroundStyleDark)
        titleColor = statusColor = [NSColor whiteColor];
    else if ([[(FileOutlineView *)[self controlView] torrent] checkForFiles: [(FileListNode *)[self objectValue] indexes]] == NSOffState)
        titleColor = statusColor = [NSColor disabledControlTextColor];
    else
    {
        titleColor = [NSColor controlTextColor];
        statusColor = [NSColor darkGrayColor];
    }
    
    [fTitleAttributes setObject: titleColor forKey: NSForegroundColorAttributeName];
    [fStatusAttributes setObject: statusColor forKey: NSForegroundColorAttributeName];
    
    //title
    NSAttributedString * titleString = [self attributedTitle];
    NSRect titleRect = [self rectForTitleWithString: titleString inBounds: cellFrame];
    [titleString drawInRect: titleRect];
    
    //status
    NSAttributedString * statusString = [self attributedStatus];
    NSRect statusRect = [self rectForStatusWithString: statusString withTitleRect: titleRect inBounds: cellFrame];
    [statusString drawInRect: statusRect];
}

@end

@implementation FileNameCell (Private)

- (NSRect) rectForTitleWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    const NSSize titleSize = [string size];
    
    NSRect result;
    if (![(FileListNode *)[self objectValue] isFolder])
    {
        result.origin.x = NSMinX(bounds) + PADDING_HORIZONAL + IMAGE_ICON_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE_FILE;
    }
    else
    {
        result.origin.x = NSMinX(bounds) + PADDING_HORIZONAL + IMAGE_FOLDER_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y = NSMidY(bounds) - titleSize.height * 0.5;
    }
    result.size.height = titleSize.height;
    result.size.width = MIN(titleSize.width, NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONAL);
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string withTitleRect: (NSRect) titleRect inBounds: (NSRect) bounds;
{
    const NSSize statusSize = [string size];
    
    NSRect result;
    if (![(FileListNode *)[self objectValue] isFolder])
    {
        result.origin.x = NSMinX(bounds) + PADDING_HORIZONAL + IMAGE_ICON_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y = NSMaxY(bounds) - PADDING_BELOW_STATUS_FILE - statusSize.height;
    }
    else
    {
        result.origin.x = NSMaxX(titleRect) + PADDING_BETWEEN_NAME_AND_FOLDER_STATUS;
        result.origin.y = NSMaxY(titleRect) - statusSize.height - 1.0;
    }
        
    result.size.height = statusSize.height;
    result.size.width = NSMaxX(bounds) - NSMaxX(result) - PADDING_HORIZONAL;
    
    return result;
}

- (NSAttributedString *) attributedTitle
{
    NSString * title = [(FileListNode *)[self objectValue] name];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatus
{
    Torrent * torrent = [(FileOutlineView *)[self controlView] torrent];
    FileListNode * node = (FileListNode *)[self objectValue];
    
    const CGFloat progress = [torrent fileProgress: node];
    NSString * percentString = progress == 1.0 ? @"100%" : [NSString localizedStringWithFormat: @"%.2f%%", progress * 100.0];
    
    NSString * status = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@",
                            "Inspector -> Files tab -> file status string"), percentString, [NSString stringForFileSize: [node size]]];
    
    return [[[NSAttributedString alloc] initWithString: status attributes: fStatusAttributes] autorelease];
}

@end
