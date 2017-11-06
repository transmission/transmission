/******************************************************************************
 * Copyright (c) 2010-2012 Transmission authors and contributors
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

#import "InfoTrackersViewController.h"
#import "NSApplicationAdditions.h"
#import "Torrent.h"
#import "TrackerCell.h"
#import "TrackerNode.h"
#import "TrackerTableView.h"

#define TRACKER_GROUP_SEPARATOR_HEIGHT 14.0

#define TRACKER_ADD_TAG 0
#define TRACKER_REMOVE_TAG 1


@interface InfoTrackersViewController (Private)

- (void) setupInfo;

- (void) addTrackers;
- (void) removeTrackers;

@end

@implementation InfoTrackersViewController

- (id) init
{
    if ((self = [super initWithNibName: @"InfoTrackersView" bundle: nil]))
    {
        [self setTitle: NSLocalizedString(@"Trackers", "Inspector view -> title")];

        fTrackerCell = [[TrackerCell alloc] init];
    }

    return self;
}

- (void) awakeFromNib
{
    [[fTrackerAddRemoveControl cell] setToolTip: NSLocalizedString(@"Add a tracker", "Inspector view -> tracker buttons")
        forSegment: TRACKER_ADD_TAG];
    [[fTrackerAddRemoveControl cell] setToolTip: NSLocalizedString(@"Remove selected trackers", "Inspector view -> tracker buttons")
        forSegment: TRACKER_REMOVE_TAG];

    const CGFloat height = [[NSUserDefaults standardUserDefaults] floatForKey: @"InspectorContentHeightTracker"];
    if (height != 0.0)
    {
        NSRect viewRect = [[self view] frame];
        viewRect.size.height = height;
        [[self view] setFrame: viewRect];
    }
}


- (void) setInfoForTorrents: (NSArray *) torrents
{
    //don't check if it's the same in case the metadata changed
    fTorrents = torrents;

    fSet = NO;
}

- (void) updateInfo
{
    if (!fSet)
        [self setupInfo];

    if ([fTorrents count] == 0)
        return;

    //get updated tracker stats
    if ([fTrackerTable editedRow] == -1)
    {
        NSArray * oldTrackers = fTrackers;

        if ([fTorrents count] == 1)
            fTrackers = [fTorrents[0] allTrackerStats];
        else
        {
            fTrackers = [[NSMutableArray alloc] init];
            for (Torrent * torrent in fTorrents)
                [fTrackers addObjectsFromArray: [torrent allTrackerStats]];
        }

        [fTrackerTable setTrackers: fTrackers];

        if (oldTrackers && [fTrackers isEqualToArray: oldTrackers])
            [fTrackerTable setNeedsDisplay: YES];
        else
            [fTrackerTable reloadData];

    }
    else
    {
        NSAssert1([fTorrents count] == 1, @"Attempting to add tracker with %ld transfers selected", [fTorrents count]);

        NSIndexSet * addedIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange([fTrackers count]-2, 2)];
        NSArray * tierAndTrackerBeingAdded = [fTrackers objectsAtIndexes: addedIndexes];

        fTrackers = [fTorrents[0] allTrackerStats];
        [fTrackers addObjectsFromArray: tierAndTrackerBeingAdded];

        [fTrackerTable setTrackers: fTrackers];

        NSIndexSet * updateIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTrackers count]-2)],
                * columnIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [[fTrackerTable tableColumns] count])];
        [fTrackerTable reloadDataForRowIndexes: updateIndexes columnIndexes: columnIndexes];
    }
}

- (void) saveViewSize
{
    [[NSUserDefaults standardUserDefaults] setFloat: NSHeight([[self view] frame]) forKey: @"InspectorContentHeightTracker"];
}

- (void) clearView
{
    fTrackers = nil;
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    return fTrackers ? [fTrackers count] : 0;
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (NSInteger) row
{
    id item = fTrackers[row];

    if ([item isKindOfClass: [NSDictionary class]])
    {
        const NSInteger tier = [item[@"Tier"] integerValue];
        NSString * tierString = tier == -1 ? NSLocalizedString(@"New Tier", "Inspector -> tracker table")
                                : [NSString stringWithFormat: NSLocalizedString(@"Tier %d", "Inspector -> tracker table"), tier];

        if ([fTorrents count] > 1)
            tierString = [tierString stringByAppendingFormat: @" - %@", item[@"Name"]];
        return tierString;
    }
    else
        return item; //TrackerNode or NSString
}

- (NSCell *) tableView: (NSTableView *) tableView dataCellForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    const BOOL tracker = [fTrackers[row] isKindOfClass: [TrackerNode class]];
    return tracker ? fTrackerCell : [tableColumn dataCellForRow: row];
}

- (CGFloat) tableView: (NSTableView *) tableView heightOfRow: (NSInteger) row
{
    //check for NSDictionay instead of TrackerNode because of display issue when adding a row
    if ([fTrackers[row] isKindOfClass: [NSDictionary class]])
        return TRACKER_GROUP_SEPARATOR_HEIGHT;
    else
        return [tableView rowHeight];
}

- (BOOL) tableView: (NSTableView *) tableView shouldEditTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    //don't allow tier row to be edited by double-click
    return NO;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fTrackerAddRemoveControl setEnabled: [fTrackerTable numberOfSelectedRows] > 0 forSegment: TRACKER_REMOVE_TAG];
}

- (BOOL) tableView: (NSTableView *) tableView isGroupRow: (NSInteger) row
{
    return ![fTrackers[row] isKindOfClass: [TrackerNode class]] && [tableView editedRow] != row;
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (NSInteger) row mouseLocation: (NSPoint) mouseLocation
{
    id node = fTrackers[row];
    if ([node isKindOfClass: [TrackerNode class]])
        return [(TrackerNode *)node fullAnnounceAddress];
    else
        return nil;
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    Torrent * torrent= fTorrents[0];

    BOOL added = NO;
    for (NSString * tracker in [object componentsSeparatedByString: @"\n"])
        if ([torrent addTrackerToNewTier: tracker])
            added = YES;

    if (!added)
        NSBeep();

    //reset table with either new or old value
    fTrackers = [torrent allTrackerStats];

    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
    [fTrackerTable deselectAll: self];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil]; //incase sort by tracker
}

- (void) addRemoveTracker: (id) sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if ([fTrackerTable editedRow] != -1)
        return;

    [self updateInfo];

    if ([[sender cell] tagForSegment: [sender selectedSegment]] == TRACKER_REMOVE_TAG)
        [self removeTrackers];
    else
        [self addTrackers];
}

@end

@implementation InfoTrackersViewController (Private)

- (void) setupInfo
{
    const NSUInteger numberSelected = [fTorrents count];
    if (numberSelected != 1)
    {
        if (numberSelected == 0)
        {
            fTrackers = nil;

            [fTrackerTable setTrackers: nil];
            [fTrackerTable reloadData];
        }

        [fTrackerTable setTorrent: nil];

        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_REMOVE_TAG];
    }
    else
    {
        [fTrackerTable setTorrent: fTorrents[0]];

        [fTrackerAddRemoveControl setEnabled: YES forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_REMOVE_TAG];
    }

    [fTrackerTable deselectAll: self];

    fSet = YES;
}

#warning doesn't like blank addresses
- (void) addTrackers
{
    [[[self view] window] makeKeyWindow];

    NSAssert1([fTorrents count] == 1, @"Attempting to add tracker with %ld transfers selected", [fTorrents count]);

    [fTrackers addObject: @{@"Tier": @-1}];
    [fTrackers addObject: @""];

    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
    [fTrackerTable selectRowIndexes: [NSIndexSet indexSetWithIndex: [fTrackers count]-1] byExtendingSelection: NO];
    [fTrackerTable editColumn: [fTrackerTable columnWithIdentifier: @"Tracker"] row: [fTrackers count]-1 withEvent: nil select: YES];
}

- (void) removeTrackers
{
    NSMutableDictionary * removeIdentifiers = [NSMutableDictionary dictionaryWithCapacity: [fTorrents count]];
    NSUInteger removeTrackerCount = 0;

    NSIndexSet * selectedIndexes = [fTrackerTable selectedRowIndexes];
    BOOL groupSelected = NO;
    NSUInteger groupRowIndex = NSNotFound;
    NSMutableIndexSet * removeIndexes = [NSMutableIndexSet indexSet];
    for (NSUInteger i = 0; i < [fTrackers count]; ++i)
    {
        id object = fTrackers[i];
        if ([object isKindOfClass: [TrackerNode class]])
        {
            if (groupSelected || [selectedIndexes containsIndex: i])
            {
                Torrent * torrent = [(TrackerNode *)object torrent];
                NSMutableSet * removeSet;
                if (!(removeSet = removeIdentifiers[torrent]))
                {
                    removeSet = [NSMutableSet set];
                    removeIdentifiers[torrent] = removeSet;
                }

                [removeSet addObject: [(TrackerNode *)object fullAnnounceAddress]];
                ++removeTrackerCount;

                [removeIndexes addIndex: i];
            }
            else
                groupRowIndex = NSNotFound; //don't remove the group row
        }
        else
        {
            //mark the previous group row for removal, if necessary
            if (groupRowIndex != NSNotFound)
                [removeIndexes addIndex: groupRowIndex];

            groupSelected = [selectedIndexes containsIndex: i];
            if (!groupSelected && i > [selectedIndexes lastIndex])
            {
                groupRowIndex = NSNotFound;
                break;
            }

            groupRowIndex = i;
        }
    }

    //mark the last group for removal, too
    if (groupRowIndex != NSNotFound)
        [removeIndexes addIndex: groupRowIndex];

    NSAssert2(removeTrackerCount <= [removeIndexes count], @"Marked %ld trackers to remove, but only removing %ld rows", removeTrackerCount, [removeIndexes count]);

    //we might have no trackers if remove right after a failed add (race condition ftw)
    #warning look into having a failed add apply right away, so that this can become an assert
    if (removeTrackerCount == 0)
        return;

    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"WarningRemoveTrackers"])
    {
        NSAlert * alert = [[NSAlert alloc] init];

        if (removeTrackerCount > 1)
        {
            [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Are you sure you want to remove %d trackers?",
                                                                "Remove trackers alert -> title"), removeTrackerCount]];
            [alert setInformativeText: NSLocalizedString(@"Once removed, Transmission will no longer attempt to contact them."
                                        " This cannot be undone.", "Remove trackers alert -> message")];
        }
        else
        {
            [alert setMessageText: NSLocalizedString(@"Are you sure you want to remove this tracker?", "Remove trackers alert -> title")];
            [alert setInformativeText: NSLocalizedString(@"Once removed, Transmission will no longer attempt to contact it."
                                        " This cannot be undone.", "Remove trackers alert -> message")];
        }

        [alert addButtonWithTitle: NSLocalizedString(@"Remove", "Remove trackers alert -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Remove trackers alert -> button")];

        [alert setShowsSuppressionButton: YES];

        NSInteger result = [alert runModal];
        if ([[alert suppressionButton] state] == NSOnState)
            [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningRemoveTrackers"];

        if (result != NSAlertFirstButtonReturn)
            return;
    }


    [fTrackerTable beginUpdates];

    for (Torrent * torrent in removeIdentifiers)
        [torrent removeTrackers: removeIdentifiers[torrent]];

    //reset table with either new or old value
    fTrackers = [[NSMutableArray alloc] init];
    for (Torrent * torrent in fTorrents)
        [fTrackers addObjectsFromArray: [torrent allTrackerStats]];

    [fTrackerTable removeRowsAtIndexes: removeIndexes withAnimation: NSTableViewAnimationSlideLeft];

    [fTrackerTable setTrackers: fTrackers];

    [fTrackerTable endUpdates];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil]; //incase sort by tracker
}

@end
