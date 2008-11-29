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

#define ADD_TAG 0
#define REMOVE_TAG 1

@interface GroupsWindowController (Private)

- (void) updateSelectedColor;

@end

@implementation GroupsWindowController

#warning should this still be a window controller?
- (void) awakeFromNib
{
    [[[fTableView tableColumnWithIdentifier: @"Button"] dataCell] setTitle: NSLocalizedString(@"Color", "Groups -> color button")];
    
    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE]];
    
    if ([NSApp isOnLeopardOrBetter])
        [[self window] setContentBorderThickness: [[fTableView enclosingScrollView] frame].origin.y forEdge: NSMinYEdge];
    else
    {
        [fAddRemoveControl sizeToFit];
        [fAddRemoveControl setLabel: @"+" forSegment: ADD_TAG];
        [fAddRemoveControl setLabel: @"-" forSegment: REMOVE_TAG];
    }

    [fAddRemoveControl setEnabled: NO forSegment: REMOVE_TAG];
    [fSelectedColorView addObserver: self forKeyPath: @"color" options: 0 context: NULL];
    
    [self updateSelectedColor];
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableview
{
    return [[GroupsController groups] numberOfGroups];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    GroupsController * groupsController = [GroupsController groups];
    NSInteger groupsIndex = [groupsController indexForRow: row];
    
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Color"])
        return [groupsController imageForIndex: groupsIndex];
    else
        return [groupsController nameForIndex: groupsIndex];
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [self updateSelectedColor];
}

- (void) observeValueForKeyPath: (NSString *) keyPath ofObject: (id) object change: (NSDictionary *) change context: (void *) context
{
    if (object == fSelectedColorView && [fTableView numberOfSelectedRows] == 1)
    {
       NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
       [[GroupsController groups] setColor: [fSelectedColorView color] forIndex: index];
       [fTableView setNeedsDisplay: YES];
    }
}

- (void) controlTextDidEndEditing: (NSNotification *) notification
{
    if ([notification object] == fSelectedColorNameField)
    {
       NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
       [[GroupsController groups] setName: [fSelectedColorNameField stringValue] forIndex: index];
       [fTableView setNeedsDisplay: YES];
    }
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) rowIndexes toPasteboard: (NSPasteboard *) pboard
{
    [pboard declareTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE] owner: self];
    [pboard setData: [NSKeyedArchiver archivedDataWithRootObject: rowIndexes] forType: GROUP_TABLE_VIEW_DATA_TYPE];
    return YES;
}

- (NSDragOperation) tableView: (NSTableView *) tableView validateDrop: (id <NSDraggingInfo>) info
    proposedRow: (NSInteger) row proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: GROUP_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow: row dropOperation: NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    
    return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) tableView acceptDrop: (id <NSDraggingInfo>) info row: (NSInteger) newRow
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
    [[NSColorPanel sharedColorPanel] close];

    NSInteger row;
    
    switch ([[sender cell] tagForSegment: [sender selectedSegment]])
    {
        case ADD_TAG:
            [[GroupsController groups] addNewGroup];
            
            [fTableView reloadData];
            
            row = [fTableView numberOfRows]-1;
            [fTableView selectRow: row byExtendingSelection: NO];
            [fTableView scrollRowToVisible: row];
            
            [[fSelectedColorNameField window] makeFirstResponder: fSelectedColorNameField];
            
            break;
        
        case REMOVE_TAG:
            row = [fTableView selectedRow];
            [[GroupsController groups] removeGroupWithRowIndex: row];            
                        
            [fTableView reloadData];
            
            NSInteger selectedRow = [fTableView selectedRow];
            if (selectedRow != -1)
                [fTableView scrollRowToVisible: selectedRow];
            
            break;
    }
}

@end

@implementation GroupsWindowController (Private)

- (void) updateSelectedColor
{
    [fAddRemoveControl setEnabled: [fTableView numberOfSelectedRows] > 0 forSegment: REMOVE_TAG];
    if ([fTableView numberOfSelectedRows] == 1)
    {
        NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
        [fSelectedColorView setColor: [[GroupsController groups] colorForIndex: index]];
        [fSelectedColorView setEnabled: YES];
        [fSelectedColorNameField setStringValue: [[GroupsController groups] nameForIndex: index]];
        [fSelectedColorNameField setEnabled: YES];
    }
    else
    {
        [fSelectedColorView setColor: [NSColor whiteColor]];
        [fSelectedColorView setEnabled: NO];
        [fSelectedColorNameField setStringValue: @""];
        [fSelectedColorNameField setEnabled: NO];
    }
}

@end
