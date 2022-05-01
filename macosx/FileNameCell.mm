// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "FileNameCell.h"
#import "FileOutlineView.h"
#import "Torrent.h"
#import "FileListNode.h"
#import "NSStringAdditions.h"

#define PADDING_HORIZONTAL 2.0
#define IMAGE_FOLDER_SIZE 16.0
#define IMAGE_ICON_SIZE 32.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 4.0
#define PADDING_ABOVE_TITLE_FILE 2.0
#define PADDING_BELOW_STATUS_FILE 2.0
#define PADDING_BETWEEN_NAME_AND_FOLDER_STATUS 4.0
#define PADDING_EXPANSION_FRAME 2.0

@interface FileNameCell ()

@property(nonatomic, readonly) NSAttributedString* attributedTitle;
@property(nonatomic, readonly) NSAttributedString* attributedStatus;
@property(nonatomic, readonly) NSMutableDictionary* fTitleAttributes;
@property(nonatomic, readonly) NSMutableDictionary* fStatusAttributes;

- (NSRect)rectForTitleWithString:(NSAttributedString*)string inBounds:(NSRect)bounds;
- (NSRect)rectForStatusWithString:(NSAttributedString*)string withTitleRect:(NSRect)titleRect inBounds:(NSRect)bounds;

@end

@implementation FileNameCell

- (instancetype)init
{
    if ((self = [super init]))
    {
        NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        paragraphStyle.lineBreakMode = NSLineBreakByTruncatingMiddle;

        _fTitleAttributes = [[NSMutableDictionary alloc]
            initWithObjectsAndKeys:[NSFont messageFontOfSize:12.0], NSFontAttributeName, paragraphStyle, NSParagraphStyleAttributeName, nil];

        NSMutableParagraphStyle* statusParagraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        statusParagraphStyle.lineBreakMode = NSLineBreakByTruncatingTail;

        _fStatusAttributes = [[NSMutableDictionary alloc]
            initWithObjectsAndKeys:[NSFont messageFontOfSize:9.0], NSFontAttributeName, statusParagraphStyle, NSParagraphStyleAttributeName, nil];
    }
    return self;
}

- (id)copyWithZone:(NSZone*)zone
{
    FileNameCell* copy = [super copyWithZone:zone];

    copy->_fTitleAttributes = _fTitleAttributes;
    copy->_fStatusAttributes = _fStatusAttributes;

    return copy;
}

- (NSImage*)image
{
    FileListNode* node = (FileListNode*)self.objectValue;
    return node.icon;
}

- (NSRect)imageRectForBounds:(NSRect)bounds
{
    NSRect result = bounds;

    result.origin.x += PADDING_HORIZONTAL;

    CGFloat const IMAGE_SIZE = ((FileListNode*)self.objectValue).isFolder ? IMAGE_FOLDER_SIZE : IMAGE_ICON_SIZE;
    result.origin.y += (result.size.height - IMAGE_SIZE) * 0.5;
    result.size = NSMakeSize(IMAGE_SIZE, IMAGE_SIZE);

    return result;
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    //icon
    [self.image drawInRect:[self imageRectForBounds:cellFrame] fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
                  fraction:1.0
            respectFlipped:YES
                     hints:nil];

    NSColor *titleColor, *statusColor;
    FileListNode* node = self.objectValue;
    if (self.backgroundStyle == NSBackgroundStyleEmphasized)
    {
        titleColor = statusColor = NSColor.whiteColor;
    }
    else if ([node.torrent checkForFiles:node.indexes] == NSControlStateValueOff)
    {
        titleColor = statusColor = NSColor.disabledControlTextColor;
    }
    else
    {
        titleColor = NSColor.controlTextColor;
        statusColor = NSColor.secondaryLabelColor;
    }

    self.fTitleAttributes[NSForegroundColorAttributeName] = titleColor;
    self.fStatusAttributes[NSForegroundColorAttributeName] = statusColor;

    //title
    NSAttributedString* titleString = self.attributedTitle;
    NSRect titleRect = [self rectForTitleWithString:titleString inBounds:cellFrame];
    [titleString drawInRect:titleRect];

    //status
    NSAttributedString* statusString = self.attributedStatus;
    NSRect statusRect = [self rectForStatusWithString:statusString withTitleRect:titleRect inBounds:cellFrame];
    [statusString drawInRect:statusRect];
}

- (NSRect)expansionFrameWithFrame:(NSRect)cellFrame inView:(NSView*)view
{
    NSAttributedString* titleString = self.attributedTitle;
    NSRect realRect = [self rectForTitleWithString:titleString inBounds:cellFrame];

    if ([titleString size].width > NSWidth(realRect) &&
        NSMouseInRect([view convertPoint:view.window.mouseLocationOutsideOfEventStream fromView:nil], realRect, view.flipped))
    {
        realRect.size.width = [titleString size].width;
        return NSInsetRect(realRect, -PADDING_EXPANSION_FRAME, -PADDING_EXPANSION_FRAME);
    }

    return NSZeroRect;
}

- (void)drawWithExpansionFrame:(NSRect)cellFrame inView:(NSView*)view
{
    cellFrame.origin.x += PADDING_EXPANSION_FRAME;
    cellFrame.origin.y += PADDING_EXPANSION_FRAME;

    self.fTitleAttributes[NSForegroundColorAttributeName] = NSColor.controlTextColor;
    NSAttributedString* titleString = self.attributedTitle;
    [titleString drawInRect:cellFrame];
}

#pragma mark - Private

- (NSRect)rectForTitleWithString:(NSAttributedString*)string inBounds:(NSRect)bounds
{
    NSSize const titleSize = [string size];

    //no right padding, so that there's not too much space between this and the priority image
    NSRect result;
    if (!((FileListNode*)self.objectValue).isFolder)
    {
        result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_ICON_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE_FILE;
        result.size.width = NSMaxX(bounds) - NSMinX(result);
    }
    else
    {
        result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_FOLDER_SIZE + PADDING_BETWEEN_IMAGE_AND_TITLE;
        result.origin.y = NSMidY(bounds) - titleSize.height * 0.5;
        result.size.width = MIN(titleSize.width, NSMaxX(bounds) - NSMinX(result));
    }
    result.size.height = titleSize.height;

    return result;
}

- (NSRect)rectForStatusWithString:(NSAttributedString*)string withTitleRect:(NSRect)titleRect inBounds:(NSRect)bounds
{
    NSSize const statusSize = [string size];

    NSRect result;
    if (!((FileListNode*)self.objectValue).isFolder)
    {
        result.origin.x = NSMinX(titleRect);
        result.origin.y = NSMaxY(bounds) - PADDING_BELOW_STATUS_FILE - statusSize.height;
        result.size.width = NSWidth(titleRect);
    }
    else
    {
        result.origin.x = NSMaxX(titleRect) + PADDING_BETWEEN_NAME_AND_FOLDER_STATUS;
        result.origin.y = NSMaxY(titleRect) - statusSize.height - 1.0;
        result.size.width = NSMaxX(bounds) - NSMaxX(titleRect);
    }
    result.size.height = statusSize.height;

    return result;
}

- (NSAttributedString*)attributedTitle
{
    NSString* title = ((FileListNode*)self.objectValue).name;
    return [[NSAttributedString alloc] initWithString:title attributes:self.fTitleAttributes];
}

- (NSAttributedString*)attributedStatus
{
    FileListNode* node = (FileListNode*)self.objectValue;
    Torrent* torrent = node.torrent;

    CGFloat const progress = [torrent fileProgress:node];
    NSString* percentString = [NSString percentString:progress longDecimals:YES];

    NSString* status = [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "Inspector -> Files tab -> file status string"),
                                                  percentString,
                                                  [NSString stringForFileSize:node.size]];

    return [[NSAttributedString alloc] initWithString:status attributes:self.fStatusAttributes];
}

@end
