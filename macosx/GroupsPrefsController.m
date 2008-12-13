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

#import "GroupsPrefsController.h"
#import "GroupsController.h"
#import "NSApplicationAdditions.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"

#define GROUP_TABLE_VIEW_DATA_TYPE @"GroupTableViewDataType"

#define ADD_TAG 0
#define REMOVE_TAG 1

#define RULES_ALL_TAG 0
#define RULES_ANY_TAG 1

@interface GroupsPrefsController (Private)

- (void) updateSelectedGroup;

@end

@implementation GroupsPrefsController

- (void) awakeFromNib
{
    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE]];
    
    if (![NSApp isOnLeopardOrBetter])
    {
        [fAddRemoveControl sizeToFit];
        [fAddRemoveControl setLabel: @"+" forSegment: ADD_TAG];
        [fAddRemoveControl setLabel: @"-" forSegment: REMOVE_TAG];
        [fGroupRulesPrefsContainer setHidden: YES]; //get rid of container when 10.5-only
    }
    
    [fRulesSheetOKButton setStringValue: NSLocalizedString(@"OK", "Groups -> rule editor -> button")];
    [fRulesSheetCancelButton setStringValue: NSLocalizedString(@"Cancel", "Groups -> rule editor -> button")];
    [fRulesSheetDescriptionField setStringValue: NSLocalizedString(@"criteria must be met to assign a transfer on add.",
                                                    "Groups -> rule editor -> button (All/Any criteria must....)")];
    
    [[fRulesAllAnyButton itemAtIndex: [fRulesAllAnyButton indexOfItemWithTag: RULES_ALL_TAG]] setTitle:
        NSLocalizedString(@"All", "Groups -> rule editor -> all/any")];
    [[fRulesAllAnyButton itemAtIndex: [fRulesAllAnyButton indexOfItemWithTag: RULES_ANY_TAG]] setTitle:
        NSLocalizedString(@"Any", "Groups -> rule editor -> all/any")];
    
    [fSelectedColorView addObserver: self forKeyPath: @"color" options: 0 context: NULL];
    
    [self updateSelectedGroup];
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
    [self updateSelectedGroup];
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
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData: [pasteboard dataForType: GROUP_TABLE_VIEW_DATA_TYPE]];
        NSInteger oldRow = [indexes firstIndex], selectedRow = [fTableView selectedRow];
        
        [[GroupsController groups] moveGroupAtRow: oldRow toRow: newRow];
        
        if (oldRow < newRow)
            newRow--;
        
        if (selectedRow == oldRow)
            selectedRow = newRow;
        else if (selectedRow > oldRow && selectedRow <= newRow)
            selectedRow--;
        else if (selectedRow < oldRow && selectedRow >= newRow)
            selectedRow++;
        else;
        
        [fTableView selectRow: selectedRow byExtendingSelection: NO];
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
    
    [self updateSelectedGroup];
}

- (void) customDownloadLocationSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [fCustomLocationPopUp window] modalDelegate: self didEndSelector:
        @selector(customDownloadLocationSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (IBAction) toggleUseCustomDownloadLocation: (id) sender
{
    NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
    if ([fCustomLocationEnableCheck state] == NSOnState)
    {
        if ([[GroupsController groups] customDownloadLocationForIndex: index])
            [[GroupsController groups] setUsesCustomDownloadLocation: YES forIndex: index];
        else
            [self customDownloadLocationSheetShow: nil];
    }
    else
        [[GroupsController groups] setUsesCustomDownloadLocation: NO forIndex: index];

    [fCustomLocationPopUp setEnabled: ([fCustomLocationEnableCheck state] == NSOnState)];
}

- (void) customDownloadLocationSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
    if (code == NSOKButton)
    {
        NSString * path = [[openPanel filenames] objectAtIndex: 0];
        [[GroupsController groups] setCustomDownloadLocation: path forIndex: index];
        [[GroupsController groups] setUsesCustomDownloadLocation: YES forIndex: index];
        [self updateSelectedGroup]; //update the popup's icon/title
    }
    else
    {
        if (![[GroupsController groups] customDownloadLocationForIndex: index])
        {
            [[GroupsController groups] setUsesCustomDownloadLocation: NO forIndex: index];
            [fCustomLocationEnableCheck setState: NSOffState];
            [fCustomLocationPopUp setEnabled: NO];
        }
    }

    [fCustomLocationPopUp selectItemAtIndex: 0];
}

#pragma mark -
#pragma mark Rule editor

- (IBAction) toggleUseAutoAssignRules: (id) sender;
{
    NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
    if ([fAutoAssignRulesEnableCheck state] == NSOnState)
    {
        if ([[GroupsController groups] autoAssignRulesForIndex: index])
            [[GroupsController groups] setUsesAutoAssignRules: YES forIndex: index];
        else
            [self orderFrontRulesSheet: nil];
    }
    else
        [[GroupsController groups] setUsesAutoAssignRules: NO forIndex: index];

    [fAutoAssignRulesEditButton setEnabled: [fAutoAssignRulesEnableCheck state] == NSOnState];
}

- (IBAction) orderFrontRulesSheet: (id) sender;
{
    if (!fGroupRulesSheetWindow)
        [NSBundle loadNibNamed: @"GroupRules" owner: self];

    [fRuleEditor removeRowsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fRuleEditor numberOfRows])]
        includeSubrows: YES];

    const NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
    NSArray * rules = [[GroupsController groups] autoAssignRulesForIndex: index];
    if (rules)
    {
        for (NSInteger i = 0; i < [rules count]; i++)
        {
            [fRuleEditor addRow: nil];
            [fRuleEditor setCriteria: [rules objectAtIndex: i] andDisplayValues: [NSArray array] forRowAtIndex: i];
        }
    }
    
    if ([fRuleEditor numberOfRows] == 0)
        [fRuleEditor addRow: nil];
    
    [fRulesAllAnyButton selectItemWithTag: [[GroupsController groups] rulesNeedAllForIndex: index] ? RULES_ALL_TAG : RULES_ANY_TAG];
    
    [NSApp beginSheet: fGroupRulesSheetWindow modalForWindow: [fTableView window] modalDelegate: nil didEndSelector: NULL
        contextInfo: NULL];
}

- (IBAction) cancelRules: (id) sender;
{
    [fGroupRulesSheetWindow orderOut: nil];
    [NSApp endSheet: fGroupRulesSheetWindow];
    
    NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
    if (![[GroupsController groups] autoAssignRulesForIndex: index])
    {
        [[GroupsController groups] setUsesAutoAssignRules: NO forIndex: index];
        [fAutoAssignRulesEnableCheck setState: NO];
        [fAutoAssignRulesEditButton setEnabled: NO];
    }
}

- (IBAction) saveRules: (id) sender;
{
    [fGroupRulesSheetWindow orderOut: nil];
    [NSApp endSheet: fGroupRulesSheetWindow];
    
    NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
    [[GroupsController groups] setRulesNeedAllForIndex: [[fRulesAllAnyButton selectedItem] tag] == RULES_ALL_TAG forIndex: index];
    [[GroupsController groups] setUsesAutoAssignRules: YES forIndex: index];
    
    NSMutableArray * rules = [NSMutableArray arrayWithCapacity: [fRuleEditor numberOfRows]];
    for (NSInteger index = 0; index < [fRuleEditor numberOfRows]; ++index)
    {
        NSString * string = [[[fRuleEditor displayValuesForRow: index] objectAtIndex: 2] stringValue];
        if (string && [string length] > 0)
        {
            NSMutableArray * rule = [[[fRuleEditor criteriaForRow: index] mutableCopy] autorelease];
            [rule replaceObjectAtIndex: 2 withObject: string];
            [rules addObject: rule];
        }
    }
    
    [[GroupsController groups] setAutoAssignRules: rules forIndex: index];
    [fAutoAssignRulesEnableCheck setState: [[GroupsController groups] usesAutoAssignRulesForIndex: index]];
    [fAutoAssignRulesEditButton setEnabled: [fAutoAssignRulesEnableCheck state] == NSOnState];
}

static NSString * torrentTitleCriteria = @"title";
static NSString * trackerURLCriteria = @"tracker";
static NSString * startsWithCriteria = @"begins";
static NSString * containsCriteria = @"contains";
static NSString * endsWithCriteria = @"ends";

- (NSInteger) ruleEditor: (NSRuleEditor *) editor numberOfChildrenForCriterion: (id) criterion withRowType: (NSRuleEditorRowType) rowType
{
    if (!criterion)
        return 2;
    else if ([criterion isEqualToString: torrentTitleCriteria] || [criterion isEqualToString: trackerURLCriteria])
        return 3;
    else if ([criterion isEqualToString: startsWithCriteria] || [criterion isEqualToString: containsCriteria]
                || [criterion isEqualToString: endsWithCriteria])
        return 1;
    else
        return 0;
}

- (id) ruleEditor: (NSRuleEditor *) editor child: (NSInteger) index forCriterion: (id) criterion
    withRowType: (NSRuleEditorRowType) rowType
{
    if (criterion == nil)
        return [[NSArray arrayWithObjects: torrentTitleCriteria, trackerURLCriteria, nil] objectAtIndex: index];
    else if ([criterion isEqualToString: torrentTitleCriteria] || [criterion isEqualToString: trackerURLCriteria])
        return [[NSArray arrayWithObjects: startsWithCriteria, containsCriteria, endsWithCriteria, nil] objectAtIndex: index];
    else
        return @"";
}

- (id) ruleEditor: (NSRuleEditor *) editor displayValueForCriterion: (id) criterion inRow: (NSInteger) row
{
    if ([criterion isEqualToString: torrentTitleCriteria])
        return NSLocalizedString(@"Torrent Title", "Groups -> rule editor");
    else if ([criterion isEqualToString: trackerURLCriteria])
        return NSLocalizedString(@"Tracker URL", "Groups -> rule editor");
    else if ([criterion isEqualToString: startsWithCriteria])
        return NSLocalizedString(@"Starts With", "Groups -> rule editor");
    else if ([criterion isEqualToString: containsCriteria])
        return NSLocalizedString(@"Contains", "Groups -> rule editor");
    else if ([criterion isEqualToString: endsWithCriteria])
        return NSLocalizedString(@"Ends With", "Groups -> rule editor");
    else
    {
        NSTextField * field = [[NSTextField alloc] initWithFrame: NSMakeRect(0, 0, 130, 22)];
        [field setStringValue: criterion];
        return [field autorelease];
    }
}

- (void) ruleEditorRowsDidChange: (NSNotification *) notification
{
    CGFloat rowHeight        = [fRuleEditor rowHeight];
    NSInteger numberOfRows   = [fRuleEditor numberOfRows];
    CGFloat ruleEditorHeight = numberOfRows * rowHeight;
    CGFloat heightDifference = ruleEditorHeight - [fRuleEditor frame].size.height;
    NSRect windowFrame       = [fRuleEditor window].frame;
    windowFrame.size.height += heightDifference;
    windowFrame.origin.y    -= heightDifference;
    [fRuleEditor.window setFrame: windowFrame display: YES animate: YES];
}

@end

@implementation GroupsPrefsController (Private)

- (void) updateSelectedGroup
{
    [fAddRemoveControl setEnabled: [fTableView numberOfSelectedRows] > 0 forSegment: REMOVE_TAG];
    if ([fTableView numberOfSelectedRows] == 1)
    {
        NSInteger index = [[GroupsController groups] indexForRow: [fTableView selectedRow]];
        [fSelectedColorView setColor: [[GroupsController groups] colorForIndex: index]];
        [fSelectedColorView setEnabled: YES];
        [fSelectedColorNameField setStringValue: [[GroupsController groups] nameForIndex: index]];
        [fSelectedColorNameField setEnabled: YES];
        [fCustomLocationEnableCheck setState: [[GroupsController groups] usesCustomDownloadLocationForIndex: index]];
        [fCustomLocationEnableCheck setEnabled: YES];
        [fCustomLocationPopUp setEnabled: [fCustomLocationEnableCheck state] == NSOnState];
        if ([[GroupsController groups] customDownloadLocationForIndex: index])
        {
            NSString * location = [[GroupsController groups] customDownloadLocationForIndex: index];
            ExpandedPathToPathTransformer * pathTransformer = [[[ExpandedPathToPathTransformer alloc] init] autorelease];
            [[fCustomLocationPopUp itemAtIndex: 0] setTitle: [pathTransformer transformedValue: location]];
            ExpandedPathToIconTransformer * iconTransformer = [[[ExpandedPathToIconTransformer alloc] init] autorelease];
            [[fCustomLocationPopUp itemAtIndex: 0] setImage: [iconTransformer transformedValue: location]];
        }
        else
        {
            [[fCustomLocationPopUp itemAtIndex: 0] setTitle: @""];
            [[fCustomLocationPopUp itemAtIndex: 0] setImage: nil];
        }

        [fAutoAssignRulesEnableCheck setState: [[GroupsController groups] usesAutoAssignRulesForIndex: index]];
        [fAutoAssignRulesEnableCheck setEnabled: YES];
        [fAutoAssignRulesEditButton setEnabled: ([fAutoAssignRulesEnableCheck state] == NSOnState)];
    }
    else
    {
        [fSelectedColorView setColor: [NSColor whiteColor]];
        [fSelectedColorView setEnabled: NO];
        [fSelectedColorNameField setStringValue: @""];
        [fSelectedColorNameField setEnabled: NO];
        [fCustomLocationEnableCheck setEnabled: NO];
        [fCustomLocationPopUp setEnabled: NO];
        [fAutoAssignRulesEnableCheck setEnabled: NO];
        [fAutoAssignRulesEditButton setEnabled: NO];
    }
}

@end
