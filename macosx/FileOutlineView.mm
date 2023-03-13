// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoWindowController.h"
#import "FileListNode.h"
#import "FileNameCell.h"
#import "FileOutlineView.h"
#import "FilePriorityCell.h"
#import "Torrent.h"

@interface FileOutlineView ()

@property(nonatomic) NSInteger hoveredRow;

@end

@implementation FileOutlineView

- (void)awakeFromNib
{
    FileNameCell* nameCell = [[FileNameCell alloc] init];
    [self tableColumnWithIdentifier:@"Name"].dataCell = nameCell;

    FilePriorityCell* priorityCell = [[FilePriorityCell alloc] init];
    [self tableColumnWithIdentifier:@"Priority"].dataCell = priorityCell;

    self.autoresizesOutlineColumn = NO;
    self.indentationPerLevel = 14.0;

    self.hoveredRow = -1;
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
    FileNameCell* cell = (FileNameCell*)[self preparedCellAtColumn:[self columnWithIdentifier:@"Name"] row:row];
    NSRect iconRect = [cell imageRectForBounds:[self rectOfRow:row]];

    iconRect.origin.x += self.indentationPerLevel * (CGFloat)([self levelForRow:row] + 1);
    return iconRect;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    for (NSTrackingArea* area in self.trackingAreas)
    {
        if (area.owner == self && area.userInfo[@"Row"])
        {
            [self removeTrackingArea:area];
        }
    }

    NSRange visibleRows = [self rowsInRect:self.visibleRect];
    if (visibleRows.length == 0)
    {
        return;
    }

    NSPoint mouseLocation = [self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];

    for (NSInteger row = visibleRows.location, col = [self columnWithIdentifier:@"Priority"]; (NSUInteger)row < NSMaxRange(visibleRows); row++)
    {
        FilePriorityCell* cell = (FilePriorityCell*)[self preparedCellAtColumn:col row:row];

        NSDictionary* userInfo = @{ @"Row" : @(row) };
        [cell addTrackingAreasForView:self inRect:[self frameOfCellAtColumn:col row:row] withUserInfo:userInfo
                        mouseLocation:mouseLocation];
    }
}

- (void)mouseEntered:(NSEvent*)event
{
    NSNumber* row;
    if ((row = ((NSDictionary*)event.userData)[@"Row"]))
    {
        self.hoveredRow = row.intValue;
        [self setNeedsDisplayInRect:[self frameOfCellAtColumn:[self columnWithIdentifier:@"Priority"] row:self.hoveredRow]];
    }
}

- (void)mouseExited:(NSEvent*)event
{
    NSNumber* row;
    if ((row = ((NSDictionary*)event.userData)[@"Row"]))
    {
        [self setNeedsDisplayInRect:[self frameOfCellAtColumn:[self columnWithIdentifier:@"Priority"] row:row.intValue]];
        self.hoveredRow = -1;
    }
}

@end
