// This file Copyright © 2010-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoTrackersViewController.h"
#import "Torrent.h"
#import "TrackerCell.h"
#import "TrackerNode.h"
#import "TrackerTableView.h"

static CGFloat const kTrackerGroupSeparatorHeight = 14.0;

typedef NS_ENUM(NSInteger, TrackerSegmentTag) {
    TrackerSegmentTagAdd = 0,
    TrackerSegmentTagRemove = 1,
};

@interface InfoTrackersViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) NSMutableArray* fTrackers;

@property(nonatomic) IBOutlet TrackerTableView* fTrackerTable;
@property(nonatomic, readonly) TrackerCell* fTrackerCell;

@property(nonatomic) IBOutlet NSSegmentedControl* fTrackerAddRemoveControl;

@end

@implementation InfoTrackersViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoTrackersView" bundle:nil]))
    {
        self.title = NSLocalizedString(@"Trackers", "Inspector view -> title");

        _fTrackerCell = [[TrackerCell alloc] init];
    }

    return self;
}

- (void)awakeFromNib
{
    [self.fTrackerAddRemoveControl.cell setToolTip:NSLocalizedString(@"Add a tracker", "Inspector view -> tracker buttons")
                                        forSegment:TrackerSegmentTagAdd];
    [self.fTrackerAddRemoveControl.cell setToolTip:NSLocalizedString(@"Remove selected trackers", "Inspector view -> tracker buttons")
                                        forSegment:TrackerSegmentTagRemove];

    CGFloat const height = [NSUserDefaults.standardUserDefaults floatForKey:@"InspectorContentHeightTracker"];
    if (height != 0.0)
    {
        NSRect viewRect = self.view.frame;
        viewRect.size.height = height;
        self.view.frame = viewRect;
    }
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet)
    {
        [self setupInfo];
    }

    if (self.fTorrents.count == 0)
    {
        return;
    }

    //get updated tracker stats
    if (self.fTrackerTable.editedRow == -1)
    {
        NSArray* oldTrackers = self.fTrackers;

        if (self.fTorrents.count == 1)
        {
            self.fTrackers = self.fTorrents[0].allTrackerStats;
        }
        else
        {
            self.fTrackers = [[NSMutableArray alloc] init];
            for (Torrent* torrent in self.fTorrents)
            {
                [self.fTrackers addObjectsFromArray:torrent.allTrackerStats];
            }
        }

        self.fTrackerTable.trackers = self.fTrackers;

        if (oldTrackers && [self.fTrackers isEqualToArray:oldTrackers])
        {
            self.fTrackerTable.needsDisplay = YES;
        }
        else
        {
            [self.fTrackerTable reloadData];
        }
    }
    else
    {
        NSAssert1(self.fTorrents.count == 1, @"Attempting to add tracker with %ld transfers selected", self.fTorrents.count);

        NSIndexSet* addedIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(self.fTrackers.count - 2, 2)];
        NSArray* tierAndTrackerBeingAdded = [self.fTrackers objectsAtIndexes:addedIndexes];

        self.fTrackers = self.fTorrents[0].allTrackerStats;
        [self.fTrackers addObjectsFromArray:tierAndTrackerBeingAdded];

        self.fTrackerTable.trackers = self.fTrackers;

        NSIndexSet *updateIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fTrackers.count - 2)],
                   *columnIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fTrackerTable.tableColumns.count)];
        [self.fTrackerTable reloadDataForRowIndexes:updateIndexes columnIndexes:columnIndexes];
    }
}

- (void)saveViewSize
{
    [NSUserDefaults.standardUserDefaults setFloat:NSHeight(self.view.frame) forKey:@"InspectorContentHeightTracker"];
}

- (void)clearView
{
    self.fTrackers = nil;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fTrackers ? self.fTrackers.count : 0;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row
{
    id item = self.fTrackers[row];

    if ([item isKindOfClass:[NSDictionary class]])
    {
        NSInteger const tier = [item[@"Tier"] integerValue];
        NSString* tierString = tier == -1 ?
            NSLocalizedString(@"New Tier", "Inspector -> tracker table") :
            [NSString stringWithFormat:NSLocalizedString(@"Tier %ld", "Inspector -> tracker table"), tier];

        if (self.fTorrents.count > 1)
        {
            tierString = [tierString stringByAppendingFormat:@" - %@", item[@"Name"]];
        }
        return tierString;
    }
    else
    {
        return item; //TrackerNode or NSString
    }
}

- (NSCell*)tableView:(NSTableView*)tableView dataCellForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    BOOL const tracker = [self.fTrackers[row] isKindOfClass:[TrackerNode class]];
    return tracker ? self.fTrackerCell : [tableColumn dataCellForRow:row];
}

- (CGFloat)tableView:(NSTableView*)tableView heightOfRow:(NSInteger)row
{
    //check for NSDictionary instead of TrackerNode because of display issue when adding a row
    if ([self.fTrackers[row] isKindOfClass:[NSDictionary class]])
    {
        return kTrackerGroupSeparatorHeight;
    }
    else
    {
        return tableView.rowHeight;
    }
}

- (BOOL)tableView:(NSTableView*)tableView shouldEditTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    //don't allow tier row to be edited by double-click
    return NO;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self.fTrackerAddRemoveControl setEnabled:self.fTrackerTable.numberOfSelectedRows > 0 forSegment:TrackerSegmentTagRemove];
}

- (BOOL)tableView:(NSTableView*)tableView isGroupRow:(NSInteger)row
{
    return NO;
}

- (BOOL)tableView:(NSTableView*)tableView shouldSelectRow:(NSInteger)row
{
    id node = self.fTrackers[row];
    if ([node isKindOfClass:[TrackerNode class]])
    {
        return YES;
    }
    return NO;
}

- (NSString*)tableView:(NSTableView*)tableView
        toolTipForCell:(NSCell*)cell
                  rect:(NSRectPointer)rect
           tableColumn:(NSTableColumn*)column
                   row:(NSInteger)row
         mouseLocation:(NSPoint)mouseLocation
{
    id node = self.fTrackers[row];
    if ([node isKindOfClass:[TrackerNode class]])
    {
        return ((TrackerNode*)node).fullAnnounceAddress;
    }
    else
    {
        return nil;
    }
}

- (void)tableView:(NSTableView*)tableView
    setObjectValue:(id)object
    forTableColumn:(NSTableColumn*)tableColumn
               row:(NSInteger)row
{
    Torrent* torrent = self.fTorrents[0];

    BOOL added = NO;
    for (NSString* tracker in [object componentsSeparatedByString:@"\n"])
    {
        if ([torrent addTrackerToNewTier:tracker])
        {
            added = YES;
        }
    }

    if (!added)
    {
        NSBeep();
    }

    //reset table with either new or old value
    self.fTrackers = torrent.allTrackerStats;

    self.fTrackerTable.trackers = self.fTrackers;
    [self.fTrackerTable reloadData];
    [self.fTrackerTable deselectAll:self];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil]; //incase sort by tracker
}

- (void)addRemoveTracker:(id)sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if (self.fTrackerTable.editedRow != -1)
    {
        return;
    }

    [self updateInfo];

    if ([[sender cell] tagForSegment:[sender selectedSegment]] == TrackerSegmentTagRemove)
    {
        [self removeTrackers];
    }
    else
    {
        [self addTrackers];
    }
}

#pragma mark - Private

- (void)setupInfo
{
    NSUInteger const numberSelected = self.fTorrents.count;
    if (numberSelected != 1)
    {
        if (numberSelected == 0)
        {
            self.fTrackers = nil;

            self.fTrackerTable.trackers = nil;
            [self.fTrackerTable reloadData];
        }

        self.fTrackerTable.torrent = nil;

        [self.fTrackerAddRemoveControl setEnabled:NO forSegment:TrackerSegmentTagAdd];
        [self.fTrackerAddRemoveControl setEnabled:NO forSegment:TrackerSegmentTagRemove];
    }
    else
    {
        self.fTrackerTable.torrent = self.fTorrents[0];

        [self.fTrackerAddRemoveControl setEnabled:YES forSegment:TrackerSegmentTagAdd];
        [self.fTrackerAddRemoveControl setEnabled:NO forSegment:TrackerSegmentTagRemove];
    }

    [self.fTrackerTable deselectAll:self];

    self.fSet = YES;
}

#warning doesn't like blank addresses
- (void)addTrackers
{
    [self.view.window makeKeyWindow];

    NSAssert1(self.fTorrents.count == 1, @"Attempting to add tracker with %ld transfers selected", self.fTorrents.count);

    [self.fTrackers addObject:@{ @"Tier" : @-1 }];
    [self.fTrackers addObject:@""];

    self.fTrackerTable.trackers = self.fTrackers;
    [self.fTrackerTable reloadData];
    [self.fTrackerTable selectRowIndexes:[NSIndexSet indexSetWithIndex:self.fTrackers.count - 1] byExtendingSelection:NO];
    [self.fTrackerTable editColumn:[self.fTrackerTable columnWithIdentifier:@"Tracker"] row:self.fTrackers.count - 1
                         withEvent:nil
                            select:YES];
}

- (void)removeTrackers
{
    NSMutableDictionary* removeIdentifiers = [NSMutableDictionary dictionaryWithCapacity:self.fTorrents.count];
    NSUInteger removeTrackerCount = 0;

    NSIndexSet* selectedIndexes = self.fTrackerTable.selectedRowIndexes;
    BOOL groupSelected = NO;
    NSUInteger groupRowIndex = NSNotFound;
    NSMutableIndexSet* removeIndexes = [NSMutableIndexSet indexSet];
    for (NSUInteger i = 0; i < self.fTrackers.count; ++i)
    {
        id object = self.fTrackers[i];
        if ([object isKindOfClass:[TrackerNode class]])
        {
            TrackerNode* node = (TrackerNode*)object;
            if (groupSelected || [selectedIndexes containsIndex:i])
            {
                Torrent* torrent = node.torrent;
                NSMutableSet* removeSet;
                if (!(removeSet = removeIdentifiers[torrent]))
                {
                    removeSet = [NSMutableSet set];
                    removeIdentifiers[torrent] = removeSet;
                }

                [removeSet addObject:node.fullAnnounceAddress];
                ++removeTrackerCount;

                [removeIndexes addIndex:i];
            }
            else
            {
                groupRowIndex = NSNotFound; //don't remove the group row
            }
        }
        else
        {
            //mark the previous group row for removal, if necessary
            if (groupRowIndex != NSNotFound)
            {
                [removeIndexes addIndex:groupRowIndex];
            }

            groupSelected = [selectedIndexes containsIndex:i];
            if (!groupSelected && i > selectedIndexes.lastIndex)
            {
                groupRowIndex = NSNotFound;
                break;
            }

            groupRowIndex = i;
        }
    }

    //mark the last group for removal, too
    if (groupRowIndex != NSNotFound)
    {
        [removeIndexes addIndex:groupRowIndex];
    }

    NSAssert2(
        removeTrackerCount <= removeIndexes.count,
        @"Marked %ld trackers to remove, but only removing %ld rows",
        removeTrackerCount,
        removeIndexes.count);

//we might have no trackers if remove right after a failed add (race condition ftw)
#warning look into having a failed add apply right away, so that this can become an assert
    if (removeTrackerCount == 0)
    {
        return;
    }

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"WarningRemoveTrackers"])
    {
        NSAlert* alert = [[NSAlert alloc] init];

        if (removeTrackerCount > 1)
        {
            alert.messageText = [NSString
                localizedStringWithFormat:NSLocalizedString(@"Are you sure you want to remove %lu trackers?", "Remove trackers alert -> title"),
                                          removeTrackerCount];
            alert.informativeText = NSLocalizedString(
                @"Once removed, Transmission will no longer attempt to contact them."
                 " This cannot be undone.",
                "Remove trackers alert -> message");
        }
        else
        {
            alert.messageText = NSLocalizedString(@"Are you sure you want to remove this tracker?", "Remove trackers alert -> title");
            alert.informativeText = NSLocalizedString(
                @"Once removed, Transmission will no longer attempt to contact it."
                 " This cannot be undone.",
                "Remove trackers alert -> message");
        }

        [alert addButtonWithTitle:NSLocalizedString(@"Remove", "Remove trackers alert -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Remove trackers alert -> button")];

        alert.showsSuppressionButton = YES;

        NSInteger result = [alert runModal];
        if (alert.suppressionButton.state == NSControlStateValueOn)
        {
            [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningRemoveTrackers"];
        }

        if (result != NSAlertFirstButtonReturn)
        {
            return;
        }
    }

    [self.fTrackerTable beginUpdates];

    for (Torrent* torrent in removeIdentifiers)
    {
        [torrent removeTrackers:removeIdentifiers[torrent]];
    }

    //reset table with either new or old value
    self.fTrackers = [[NSMutableArray alloc] init];
    for (Torrent* torrent in self.fTorrents)
    {
        [self.fTrackers addObjectsFromArray:torrent.allTrackerStats];
    }

    [self.fTrackerTable removeRowsAtIndexes:removeIndexes withAnimation:NSTableViewAnimationSlideLeft];

    self.fTrackerTable.trackers = self.fTrackers;

    [self.fTrackerTable endUpdates];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil]; //incase sort by tracker
}

@end
