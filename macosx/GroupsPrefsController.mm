/******************************************************************************
 * Copyright (c) 2007-2012 Transmission authors and contributors
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

#import "GroupsPrefsController.h"
#import "GroupsController.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"
#import "NSApplicationAdditions.h"

#define GROUP_TABLE_VIEW_DATA_TYPE @"GroupTableViewDataType"

#define ADD_TAG 0
#define REMOVE_TAG 1

@interface GroupsPrefsController ()

@property(nonatomic) IBOutlet NSWindow* groupRulesSheetWindow;
@property(nonatomic, weak) IBOutlet NSPredicateEditor* ruleEditor;
@property(nonatomic, weak) IBOutlet NSLayoutConstraint* ruleEditorHeightConstraint;

@end

@interface GroupsPrefsController (Private)

- (void)updateSelectedGroup;
- (void)refreshCustomLocationWithSingleGroup;

@end

@implementation GroupsPrefsController

@synthesize groupRulesSheetWindow;
@synthesize ruleEditor;
@synthesize ruleEditorHeightConstraint;

- (void)awakeFromNib
{
    [fTableView registerForDraggedTypes:@[ GROUP_TABLE_VIEW_DATA_TYPE ]];

    [fSelectedColorView addObserver:self forKeyPath:@"color" options:0 context:NULL];

    [self updateSelectedGroup];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableview
{
    return GroupsController.groups.numberOfGroups;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    GroupsController* groupsController = GroupsController.groups;
    NSInteger groupsIndex = [groupsController indexForRow:row];

    NSString* identifier = tableColumn.identifier;
    if ([identifier isEqualToString:@"Color"])
    {
        return [groupsController imageForIndex:groupsIndex];
    }
    else
    {
        return [groupsController nameForIndex:groupsIndex];
    }
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self updateSelectedGroup];
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context
{
    if (object == fSelectedColorView && fTableView.numberOfSelectedRows == 1)
    {
        NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
        [GroupsController.groups setColor:fSelectedColorView.color forIndex:index];
        fTableView.needsDisplay = YES;
    }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if (notification.object == fSelectedColorNameField)
    {
        NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
        [GroupsController.groups setName:fSelectedColorNameField.stringValue forIndex:index];
        fTableView.needsDisplay = YES;
    }
}

- (BOOL)tableView:(NSTableView*)tableView writeRowsWithIndexes:(NSIndexSet*)rowIndexes toPasteboard:(NSPasteboard*)pboard
{
    [pboard declareTypes:@[ GROUP_TABLE_VIEW_DATA_TYPE ] owner:self];
    [pboard setData:[NSKeyedArchiver archivedDataWithRootObject:rowIndexes] forType:GROUP_TABLE_VIEW_DATA_TYPE];
    return YES;
}

- (NSDragOperation)tableView:(NSTableView*)tableView
                validateDrop:(id<NSDraggingInfo>)info
                 proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)operation
{
    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:GROUP_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow:row dropOperation:NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }

    return NSDragOperationNone;
}

- (BOOL)tableView:(NSTableView*)tableView
       acceptDrop:(id<NSDraggingInfo>)info
              row:(NSInteger)newRow
    dropOperation:(NSTableViewDropOperation)operation
{
    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:GROUP_TABLE_VIEW_DATA_TYPE])
    {
        NSIndexSet* indexes = [NSKeyedUnarchiver unarchiveObjectWithData:[pasteboard dataForType:GROUP_TABLE_VIEW_DATA_TYPE]];
        NSInteger oldRow = indexes.firstIndex;

        if (oldRow < newRow)
        {
            newRow--;
        }

        [fTableView beginUpdates];

        [GroupsController.groups moveGroupAtRow:oldRow toRow:newRow];

        [fTableView moveRowAtIndex:oldRow toIndex:newRow];
        [fTableView endUpdates];
    }

    return YES;
}

- (void)addRemoveGroup:(id)sender
{
    if (NSColorPanel.sharedColorPanelExists)
    {
        [NSColorPanel.sharedColorPanel close];
    }

    NSInteger row;

    switch ([[sender cell] tagForSegment:[sender selectedSegment]])
    {
    case ADD_TAG:
        [fTableView beginUpdates];

        [GroupsController.groups addNewGroup];

        row = fTableView.numberOfRows;

        [fTableView insertRowsAtIndexes:[NSIndexSet indexSetWithIndex:row] withAnimation:NSTableViewAnimationSlideUp];
        [fTableView endUpdates];

        [fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [fTableView scrollRowToVisible:row];

        [fSelectedColorNameField.window makeFirstResponder:fSelectedColorNameField];

        break;

    case REMOVE_TAG:
        row = fTableView.selectedRow;

        [fTableView beginUpdates];

        [GroupsController.groups removeGroupWithRowIndex:row];

        [fTableView removeRowsAtIndexes:[NSIndexSet indexSetWithIndex:row] withAnimation:NSTableViewAnimationSlideUp];
        [fTableView endUpdates];

        if (fTableView.numberOfRows > 0)
        {
            if (row == fTableView.numberOfRows)
            {
                --row;
            }
            [fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            [fTableView scrollRowToVisible:row];
        }

        break;
    }

    [self updateSelectedGroup];
}

- (void)customDownloadLocationSheetShow:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.prompt = NSLocalizedString(@"Select", "Preferences -> Open panel prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;

    [panel beginSheetModalForWindow:fCustomLocationPopUp.window completionHandler:^(NSInteger result) {
        NSInteger const index = [GroupsController.groups indexForRow:fTableView.selectedRow];
        if (result == NSFileHandlingPanelOKButton)
        {
            NSString* path = panel.URLs[0].path;
            [GroupsController.groups setCustomDownloadLocation:path forIndex:index];
            [GroupsController.groups setUsesCustomDownloadLocation:YES forIndex:index];
        }
        else
        {
            if (![GroupsController.groups customDownloadLocationForIndex:index])
            {
                [GroupsController.groups setUsesCustomDownloadLocation:NO forIndex:index];
            }
        }

        [self refreshCustomLocationWithSingleGroup];

        [fCustomLocationPopUp selectItemAtIndex:0];
    }];
}

- (IBAction)toggleUseCustomDownloadLocation:(id)sender
{
    NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
    if (fCustomLocationEnableCheck.state == NSOnState)
    {
        if ([GroupsController.groups customDownloadLocationForIndex:index])
        {
            [GroupsController.groups setUsesCustomDownloadLocation:YES forIndex:index];
        }
        else
        {
            [self customDownloadLocationSheetShow:nil];
        }
    }
    else
    {
        [GroupsController.groups setUsesCustomDownloadLocation:NO forIndex:index];
    }

    fCustomLocationPopUp.enabled = (fCustomLocationEnableCheck.state == NSOnState);
}

#pragma mark -
#pragma mark Rule editor

- (IBAction)toggleUseAutoAssignRules:(id)sender
{
    NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
    if (fAutoAssignRulesEnableCheck.state == NSOnState)
    {
        if ([GroupsController.groups autoAssignRulesForIndex:index])
        {
            [GroupsController.groups setUsesAutoAssignRules:YES forIndex:index];
        }
        else
        {
            [self orderFrontRulesSheet:nil];
        }
    }
    else
    {
        [GroupsController.groups setUsesAutoAssignRules:NO forIndex:index];
    }

    fAutoAssignRulesEditButton.enabled = fAutoAssignRulesEnableCheck.state == NSOnState;
}

- (IBAction)orderFrontRulesSheet:(id)sender
{
    if (!self.groupRulesSheetWindow)
    {
        [NSBundle.mainBundle loadNibNamed:@"GroupRules" owner:self topLevelObjects:NULL];
    }

    NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
    NSPredicate* predicate = [GroupsController.groups autoAssignRulesForIndex:index];
    self.ruleEditor.objectValue = predicate;

    if (self.ruleEditor.numberOfRows == 0)
    {
        [self.ruleEditor addRow:nil];
    }

    [fTableView.window beginSheet:self.groupRulesSheetWindow completionHandler:nil];
}

- (IBAction)cancelRules:(id)sender
{
    [self.groupRulesSheetWindow orderOut:nil];
    [NSApp endSheet:self.groupRulesSheetWindow];

    NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
    if (![GroupsController.groups autoAssignRulesForIndex:index])
    {
        [GroupsController.groups setUsesAutoAssignRules:NO forIndex:index];
        fAutoAssignRulesEnableCheck.state = NO;
        fAutoAssignRulesEditButton.enabled = NO;
    }
}

- (IBAction)saveRules:(id)sender
{
    [self.groupRulesSheetWindow orderOut:nil];
    [NSApp endSheet:self.groupRulesSheetWindow];

    NSInteger index = [GroupsController.groups indexForRow:fTableView.selectedRow];
    [GroupsController.groups setUsesAutoAssignRules:YES forIndex:index];

    NSPredicate* predicate = self.ruleEditor.objectValue;
    [GroupsController.groups setAutoAssignRules:predicate forIndex:index];

    fAutoAssignRulesEnableCheck.state = [GroupsController.groups usesAutoAssignRulesForIndex:index];
    fAutoAssignRulesEditButton.enabled = fAutoAssignRulesEnableCheck.state == NSOnState;
}

- (void)ruleEditorRowsDidChange:(NSNotification*)notification
{
    NSScrollView* ruleEditorScrollView = self.ruleEditor.enclosingScrollView;

    CGFloat const rowHeight = self.ruleEditor.rowHeight;
    CGFloat const bordersHeight = ruleEditorScrollView.frame.size.height - ruleEditorScrollView.contentSize.height;

    CGFloat const requiredRowCount = self.ruleEditor.numberOfRows;
    CGFloat const maxVisibleRowCount = (long)((NSHeight(self.ruleEditor.window.screen.visibleFrame) * 2 / 3) / rowHeight);

    self.ruleEditorHeightConstraint.constant = MIN(requiredRowCount, maxVisibleRowCount) * rowHeight + bordersHeight;
    ruleEditorScrollView.hasVerticalScroller = requiredRowCount > maxVisibleRowCount;
}

@end

@implementation GroupsPrefsController (Private)

- (void)updateSelectedGroup
{
    [fAddRemoveControl setEnabled:fTableView.numberOfSelectedRows > 0 forSegment:REMOVE_TAG];
    if (fTableView.numberOfSelectedRows == 1)
    {
        NSInteger const index = [GroupsController.groups indexForRow:fTableView.selectedRow];
        fSelectedColorView.color = [GroupsController.groups colorForIndex:index];
        fSelectedColorView.enabled = YES;
        fSelectedColorNameField.stringValue = [GroupsController.groups nameForIndex:index];
        fSelectedColorNameField.enabled = YES;

        [self refreshCustomLocationWithSingleGroup];

        fAutoAssignRulesEnableCheck.state = [GroupsController.groups usesAutoAssignRulesForIndex:index];
        fAutoAssignRulesEnableCheck.enabled = YES;
        fAutoAssignRulesEditButton.enabled = (fAutoAssignRulesEnableCheck.state == NSOnState);
    }
    else
    {
        fSelectedColorView.color = NSColor.whiteColor;
        fSelectedColorView.enabled = NO;
        fSelectedColorNameField.stringValue = @"";
        fSelectedColorNameField.enabled = NO;
        fCustomLocationEnableCheck.enabled = NO;
        fCustomLocationPopUp.enabled = NO;
        fAutoAssignRulesEnableCheck.enabled = NO;
        fAutoAssignRulesEditButton.enabled = NO;
    }
}

- (void)refreshCustomLocationWithSingleGroup
{
    NSInteger const index = [GroupsController.groups indexForRow:fTableView.selectedRow];

    BOOL const hasCustomLocation = [GroupsController.groups usesCustomDownloadLocationForIndex:index];
    fCustomLocationEnableCheck.state = hasCustomLocation;
    fCustomLocationEnableCheck.enabled = YES;
    fCustomLocationPopUp.enabled = hasCustomLocation;

    NSString* location = [GroupsController.groups customDownloadLocationForIndex:index];
    if (location)
    {
        ExpandedPathToPathTransformer* pathTransformer = [[ExpandedPathToPathTransformer alloc] init];
        [fCustomLocationPopUp itemAtIndex:0].title = [pathTransformer transformedValue:location];
        ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
        [fCustomLocationPopUp itemAtIndex:0].image = [iconTransformer transformedValue:location];
    }
    else
    {
        [fCustomLocationPopUp itemAtIndex:0].title = @"";
        [fCustomLocationPopUp itemAtIndex:0].image = nil;
    }
}

@end
