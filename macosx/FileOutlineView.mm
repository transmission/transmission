// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoWindowController.h"
#import "FileListNode.h"
#import "FileNameCellView.h"
#import "FileOutlineView.h"
#import "FilePriorityCellView.h"
#import "Torrent.h"

@interface FileOutlineView ()

@end

@implementation FileOutlineView

- (void)awakeFromNib
{
    [super awakeFromNib];

    self.autoresizesOutlineColumn = NO;
    self.indentationPerLevel = 14.0;
}

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeKeyWindow];
    [super mouseDown:event];
}

- (NSMenu*)menuForEvent:(NSEvent*)event
{
    NSInteger const row = [self rowAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];

    if (row >= 0)
    {
        if (![self isRowSelected:row])
        {
            [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        }
    }
    else
    {
        [self deselectAll:self];
    }

    return self.menu;
}

- (NSRect)iconRectForRow:(NSInteger)row
{
    NSView* view = [self viewAtColumn:[self columnWithIdentifier:@"Name"] row:row makeIfNecessary:NO];
    if (![view isKindOfClass:[FileNameCellView class]])
    {
        return NSZeroRect;
    }

    FileNameCellView* cellView = (FileNameCellView*)view;
    NSImageView* iconView = [cellView valueForKey:@"iconView"];
    if (!iconView)
    {
        return NSZeroRect;
    }

    NSRect iconRect = [self convertRect:iconView.frame fromView:cellView];
    iconRect.origin.x += self.indentationPerLevel * (CGFloat)([self levelForRow:row] + 1);
    return iconRect;
}

@end
