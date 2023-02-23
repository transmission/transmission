// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "CocoaCompatibility.h"

#import "GroupsPrefsController.h"
#import "GroupsController.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"

static NSString* const kGroupTableViewDataType = @"GroupTableViewDataType";

typedef NS_ENUM(NSInteger, SegmentTag) {
    SegmentTagAdd = 0,
    SegmentTagRemove = 1,
};

@interface GroupsPrefsController ()

@property(nonatomic) IBOutlet NSTableView* fTableView;
@property(nonatomic) IBOutlet NSSegmentedControl* fAddRemoveControl;

@property(nonatomic) IBOutlet NSColorWell* fSelectedColorView;
@property(nonatomic) IBOutlet NSTextField* fSelectedColorNameField;
@property(nonatomic) IBOutlet NSButton* fCustomLocationEnableCheck;
@property(nonatomic) IBOutlet NSPopUpButton* fCustomLocationPopUp;

@property(nonatomic) IBOutlet NSButton* fAutoAssignRulesEnableCheck;
@property(nonatomic) IBOutlet NSButton* fAutoAssignRulesEditButton;

@property(nonatomic) IBOutlet NSWindow* groupRulesSheetWindow;
@property(nonatomic, weak) IBOutlet NSPredicateEditor* ruleEditor;
@property(nonatomic, weak) IBOutlet NSLayoutConstraint* ruleEditorHeightConstraint;

@end

@implementation GroupsPrefsController

- (void)awakeFromNib
{
    [self.fTableView registerForDraggedTypes:@[ kGroupTableViewDataType ]];

    [self.fSelectedColorView addObserver:self forKeyPath:@"color" options:0 context:NULL];

    if (@available(macOS 13.0, *))
    {
        self.fSelectedColorView.colorWellStyle = NSColorWellStyleMinimal;
    }

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
    if (object == self.fSelectedColorView && self.fTableView.numberOfSelectedRows == 1)
    {
        NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        [GroupsController.groups setColor:self.fSelectedColorView.color forIndex:index];
        self.fTableView.needsDisplay = YES;
    }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if (notification.object == self.fSelectedColorNameField)
    {
        NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        [GroupsController.groups setName:self.fSelectedColorNameField.stringValue forIndex:index];
        self.fTableView.needsDisplay = YES;
    }
}

- (BOOL)tableView:(NSTableView*)tableView writeRowsWithIndexes:(NSIndexSet*)rowIndexes toPasteboard:(NSPasteboard*)pboard
{
    [pboard declareTypes:@[ kGroupTableViewDataType ] owner:self];
    [pboard setData:[NSKeyedArchiver archivedDataWithRootObject:rowIndexes requiringSecureCoding:YES error:nil]
            forType:kGroupTableViewDataType];
    return YES;
}

- (NSDragOperation)tableView:(NSTableView*)tableView
                validateDrop:(id<NSDraggingInfo>)info
                 proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)operation
{
    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:kGroupTableViewDataType])
    {
        [self.fTableView setDropRow:row dropOperation:NSTableViewDropAbove];
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
    if ([pasteboard.types containsObject:kGroupTableViewDataType])
    {
        NSIndexSet* indexes = [NSKeyedUnarchiver unarchivedObjectOfClass:NSIndexSet.class fromData:[pasteboard dataForType:kGroupTableViewDataType]
                                                                   error:nil];
        NSInteger oldRow = indexes.firstIndex;

        if (oldRow < newRow)
        {
            newRow--;
        }

        [self.fTableView beginUpdates];

        [GroupsController.groups moveGroupAtRow:oldRow toRow:newRow];

        [self.fTableView moveRowAtIndex:oldRow toIndex:newRow];
        [self.fTableView endUpdates];
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
    case SegmentTagAdd:
        [self.fTableView beginUpdates];

        [GroupsController.groups addNewGroup];

        row = self.fTableView.numberOfRows;

        [self.fTableView insertRowsAtIndexes:[NSIndexSet indexSetWithIndex:row] withAnimation:NSTableViewAnimationSlideUp];
        [self.fTableView endUpdates];

        [self.fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [self.fTableView scrollRowToVisible:row];

        [self.fSelectedColorNameField.window makeFirstResponder:self.fSelectedColorNameField];

        break;

    case SegmentTagRemove:
        row = self.fTableView.selectedRow;

        [self.fTableView beginUpdates];

        [GroupsController.groups removeGroupWithRowIndex:row];

        [self.fTableView removeRowsAtIndexes:[NSIndexSet indexSetWithIndex:row] withAnimation:NSTableViewAnimationSlideUp];
        [self.fTableView endUpdates];

        if (self.fTableView.numberOfRows > 0)
        {
            if (row == self.fTableView.numberOfRows)
            {
                --row;
            }
            [self.fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            [self.fTableView scrollRowToVisible:row];
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

    [panel beginSheetModalForWindow:self.fCustomLocationPopUp.window completionHandler:^(NSInteger result) {
        NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        if (result == NSModalResponseOK)
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

        [self.fCustomLocationPopUp selectItemAtIndex:0];
    }];
}

- (IBAction)toggleUseCustomDownloadLocation:(id)sender
{
    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    if (self.fCustomLocationEnableCheck.state == NSControlStateValueOn)
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

    self.fCustomLocationPopUp.enabled = (self.fCustomLocationEnableCheck.state == NSControlStateValueOn);
}

#pragma mark -
#pragma mark Rule editor

- (IBAction)toggleUseAutoAssignRules:(id)sender
{
    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    if (self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn)
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

    self.fAutoAssignRulesEditButton.enabled = self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn;
}

- (IBAction)orderFrontRulesSheet:(id)sender
{
    if (!self.groupRulesSheetWindow)
    {
        [NSBundle.mainBundle loadNibNamed:@"GroupRules" owner:self topLevelObjects:NULL];
    }

    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    NSPredicate* predicate = [GroupsController.groups autoAssignRulesForIndex:index];
    self.ruleEditor.objectValue = predicate;

    if (self.ruleEditor.numberOfRows == 0)
    {
        [self.ruleEditor addRow:nil];
    }

    [self.fTableView.window beginSheet:self.groupRulesSheetWindow completionHandler:nil];
}

- (IBAction)cancelRules:(id)sender
{
    [self.fTableView.window endSheet:self.groupRulesSheetWindow];

    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    if (![GroupsController.groups autoAssignRulesForIndex:index])
    {
        [GroupsController.groups setUsesAutoAssignRules:NO forIndex:index];
        self.fAutoAssignRulesEnableCheck.state = NO;
        self.fAutoAssignRulesEditButton.enabled = NO;
    }
}

- (IBAction)saveRules:(id)sender
{
    [self.fTableView.window endSheet:self.groupRulesSheetWindow];

    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    [GroupsController.groups setUsesAutoAssignRules:YES forIndex:index];

    NSPredicate* predicate = self.ruleEditor.objectValue;
    [GroupsController.groups setAutoAssignRules:predicate forIndex:index];

    self.fAutoAssignRulesEnableCheck.state = [GroupsController.groups usesAutoAssignRulesForIndex:index];
    self.fAutoAssignRulesEditButton.enabled = self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn;
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

#pragma mark - Private

- (void)updateSelectedGroup
{
    [self.fAddRemoveControl setEnabled:self.fTableView.numberOfSelectedRows > 0 forSegment:SegmentTagRemove];
    if (self.fTableView.numberOfSelectedRows == 1)
    {
        NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        self.fSelectedColorView.color = [GroupsController.groups colorForIndex:index];
        self.fSelectedColorView.enabled = YES;
        self.fSelectedColorNameField.stringValue = [GroupsController.groups nameForIndex:index];
        self.fSelectedColorNameField.enabled = YES;

        [self refreshCustomLocationWithSingleGroup];

        self.fAutoAssignRulesEnableCheck.state = [GroupsController.groups usesAutoAssignRulesForIndex:index];
        self.fAutoAssignRulesEnableCheck.enabled = YES;
        self.fAutoAssignRulesEditButton.enabled = (self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn);
    }
    else
    {
        self.fSelectedColorView.color = NSColor.whiteColor;
        self.fSelectedColorView.enabled = NO;
        self.fSelectedColorNameField.stringValue = @"";
        self.fSelectedColorNameField.enabled = NO;
        self.fCustomLocationEnableCheck.enabled = NO;
        self.fCustomLocationPopUp.enabled = NO;
        self.fAutoAssignRulesEnableCheck.enabled = NO;
        self.fAutoAssignRulesEditButton.enabled = NO;
    }
}

- (void)refreshCustomLocationWithSingleGroup
{
    NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];

    BOOL const hasCustomLocation = [GroupsController.groups usesCustomDownloadLocationForIndex:index];
    self.fCustomLocationEnableCheck.state = hasCustomLocation;
    self.fCustomLocationEnableCheck.enabled = YES;
    self.fCustomLocationPopUp.enabled = hasCustomLocation;

    NSString* location = [GroupsController.groups customDownloadLocationForIndex:index];
    if (location)
    {
        ExpandedPathToPathTransformer* pathTransformer = [[ExpandedPathToPathTransformer alloc] init];
        [self.fCustomLocationPopUp itemAtIndex:0].title = [pathTransformer transformedValue:location];
        ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
        [self.fCustomLocationPopUp itemAtIndex:0].image = [iconTransformer transformedValue:location];
    }
    else
    {
        [self.fCustomLocationPopUp itemAtIndex:0].title = @"";
        [self.fCustomLocationPopUp itemAtIndex:0].image = nil;
    }
}

@end
