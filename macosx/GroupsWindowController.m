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

#import "GroupsWindowController.h"
#import "GroupsController.h"
#import "NSApplicationAdditions.h"

#define GROUP_TABLE_VIEW_DATA_TYPE @"GroupTableViewDataType"

typedef enum
{
    ADD_TAG = 0,
    REMOVE_TAG = 1
} controlTag;

@interface GroupsWindowController (Private)

- (void) changeColor: (id) sender;

@end

@implementation GroupsWindowController

GroupsWindowController * fGroupsWindowInstance = nil;
+ (GroupsWindowController *) groupsWindow
{
    if (!fGroupsWindowInstance)
        fGroupsWindowInstance = [[GroupsWindowController alloc] initWithWindowNibName: @"GroupsWindow"];
    return fGroupsWindowInstance;
}

- (void) awakeFromNib
{
    [[[fTableView tableColumnWithIdentifier: @"Button"] dataCell] setTitle: NSLocalizedString(@"Color", "Groups -> Color Button")];
    
    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE]];
    
    if ([NSApp isOnLeopardOrBetter])
        [[self window] setContentBorderThickness: [[fTableView enclosingScrollView] frame].origin.y forEdge: NSMinYEdge];
    else
    {
        [fAddRemoveControl setLabel: @"+" forSegment: 0];
        [fAddRemoveControl setLabel: @"-" forSegment: 1];
    }
    
    [fAddRemoveControl setEnabled: NO forSegment: REMOVE_TAG];
}

- (void) windowWillClose: (id) sender
{
    [[NSColorPanel sharedColorPanel] close];
    
	[fGroupsWindowInstance release];
    fGroupsWindowInstance = nil;
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableview
{
    return [[GroupsController groups] numberOfGroups];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    GroupsController * groupsController = [GroupsController groups];
    int groupsIndex = [groupsController indexForRow: row];
    
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Color"])
        return [groupsController imageForIndex: groupsIndex isSmall: NO];
    else
        return [groupsController nameForIndex: groupsIndex];
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Name"])
        [[GroupsController groups] setName: object forIndex: [[GroupsController groups] indexForRow: row]];
    else if ([identifier isEqualToString: @"Button"])
    {
        fCurrentColorIndex = [[GroupsController groups] indexForRow: row];
        
        NSColorPanel * colorPanel = [NSColorPanel sharedColorPanel];
        [colorPanel setContinuous: YES];
        [colorPanel setColor: [[GroupsController groups] colorForIndex: fCurrentColorIndex]];
        
        [colorPanel setTarget: self];
        [colorPanel setAction: @selector(changeColor:)];
        
        [colorPanel orderFront: self];
    }
    else;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fAddRemoveControl setEnabled: [fTableView numberOfSelectedRows] > 0 forSegment: REMOVE_TAG];
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) rowIndexes toPasteboard: (NSPasteboard *) pboard
{
    [pboard declareTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE] owner: self];
    [pboard setData: [NSKeyedArchiver archivedDataWithRootObject: rowIndexes] forType: GROUP_TABLE_VIEW_DATA_TYPE];
    return YES;
}

- (NSDragOperation) tableView: (NSTableView *) tableView validateDrop: (id <NSDraggingInfo>) info
    proposedRow: (int) row proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: GROUP_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow: row dropOperation: NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    
    return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) t acceptDrop: (id <NSDraggingInfo>) info row: (int) newRow
    dropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: GROUP_TABLE_VIEW_DATA_TYPE])
    {
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData: [pasteboard dataForType: GROUP_TABLE_VIEW_DATA_TYPE]],
            * selectedIndexes = [[GroupsController groups] moveGroupsAtRowIndexes: indexes toRow: newRow
                                        oldSelected: [fTableView selectedRowIndexes]];
        
        [fTableView selectRowIndexes: selectedIndexes byExtendingSelection: NO];
        [fTableView reloadData];
    }
    
    return YES;
}

- (void) addRemoveGroup: (id) sender
{
    NSIndexSet * indexes;
    
    switch ([[sender cell] tagForSegment: [sender selectedSegment]])
    {
        case ADD_TAG:
            [[GroupsController groups] addGroupWithName: @"" color: [NSColor cyanColor]];
            
            [fTableView reloadData];
            [fTableView deselectAll: self];
            
            [fTableView editColumn: [fTableView columnWithIdentifier: @"Name"] row: [fTableView numberOfRows]-1 withEvent: nil
                        select: NO];
            break;
        
        case REMOVE_TAG:
            //close color picker if corresponding row is removed
            indexes = [fTableView selectedRowIndexes];
            if ([[NSColorPanel sharedColorPanel] isVisible]
                && [indexes containsIndex: [[GroupsController groups] rowValueForIndex: fCurrentColorIndex]])
                [[NSColorPanel sharedColorPanel] close];
            
            [[GroupsController groups] removeGroupWithRowIndexes: indexes];
            
            [fTableView deselectAll: self];
            [fTableView reloadData];
            
            break;
    }
}

@end

@implementation GroupsWindowController (Private)

- (void) changeColor: (id) sender
{
    [[GroupsController groups] setColor: [sender color] forIndex: fCurrentColorIndex];
    [fTableView reloadData];
}

@end
