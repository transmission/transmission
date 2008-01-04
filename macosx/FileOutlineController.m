/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008 Transmission authors and contributors
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

#import "FileOutlineController.h"
#import "Torrent.h"
#import "FileOutlineView.h"
#import "NSApplicationAdditions.h"

#define ROW_SMALL_HEIGHT 18.0

@implementation FileOutlineController

- (void) awakeFromNib
{
    [fOutline setDoubleAction: @selector(revealFile:)];
    
    //set table header tool tips
    if ([NSApp isOnLeopardOrBetter])
    {
        [[fOutline tableColumnWithIdentifier: @"Check"] setHeaderToolTip: NSLocalizedString(@"Download",
                                                                            "file table -> header tool tip")];
        [[fOutline tableColumnWithIdentifier: @"Priority"] setHeaderToolTip: NSLocalizedString(@"Priority",
                                                                            "file table -> header tool tip")];                                                               
    }
    
    #warning generate menu in code?
    //set priority item images
    [fFilePriorityNormal setImage: [NSImage imageNamed: @"PriorityNormal.png"]];
    [fFilePriorityLow setImage: [NSImage imageNamed: @"PriorityLow.png"]];
    [fFilePriorityHigh setImage: [NSImage imageNamed: @"PriorityHigh.png"]];
    
    [self setTorrent: nil];
}

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
    [fOutline setTorrent: fTorrent];
    
    [fOutline deselectAll: nil];
    [fOutline reloadData];
}

- (void) reloadData
{
    [fOutline reloadData];
}

- (void) setHoverRowForEvent: (NSEvent *) event
{
    [fOutline setHoverRowForEvent: event];
}

- (int) outlineView: (NSOutlineView *) outlineView numberOfChildrenOfItem: (id) item
{
    if (!item)
        return fTorrent ? [[fTorrent fileList] count] : 0;
    else
        return [[item objectForKey: @"IsFolder"] boolValue] ? [[item objectForKey: @"Children"] count] : 0;
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isItemExpandable: (id) item 
{
    return [[item objectForKey: @"IsFolder"] boolValue];
}

- (id) outlineView: (NSOutlineView *) outlineView child: (int) index ofItem: (id) item
{
    return [(item ? [item objectForKey: @"Children"] : [fTorrent fileList]) objectAtIndex: index];
}

- (id) outlineView: (NSOutlineView *) outlineView objectValueForTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    if ([[tableColumn identifier] isEqualToString: @"Check"])
        return [NSNumber numberWithInt: [fTorrent checkForFiles: [item objectForKey: @"Indexes"]]];
    else
        return item;
}

- (void) outlineView: (NSOutlineView *) outlineView willDisplayCell: (id) cell
            forTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Check"])
        [cell setEnabled: [fTorrent canChangeDownloadCheckForFiles: [item objectForKey: @"Indexes"]]];
    else if ([identifier isEqualToString: @"Priority"])
        [cell setRepresentedObject: item];
    else;
}

- (void) outlineView: (NSOutlineView *) outlineView setObjectValue: (id) object
        forTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Check"])
    {
        NSIndexSet * indexSet;
        if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
            indexSet = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])];
        else
            indexSet = [item objectForKey: @"Indexes"];
        
        [fTorrent setFileCheckState: [object intValue] != NSOffState ? NSOnState : NSOffState forIndexes: indexSet];
        [fOutline reloadData];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
    }
}

- (NSString *) outlineView: (NSOutlineView *) outlineView typeSelectStringForTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    return [item objectForKey: @"Name"];
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
        tableColumn: (NSTableColumn *) tableColumn item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Name"])
        return [[fTorrent downloadFolder] stringByAppendingPathComponent: [item objectForKey: @"Path"]];
    else if ([ident isEqualToString: @"Check"])
    {
        switch ([cell state])
        {
            case NSOffState:
                return NSLocalizedString(@"Don't Download", "files tab -> tooltip");
            case NSOnState:
                return NSLocalizedString(@"Download", "files tab -> tooltip");
            case NSMixedState:
                return NSLocalizedString(@"Download Some", "files tab -> tooltip");
        }
    }
    else if ([ident isEqualToString: @"Priority"])
    {
        NSSet * priorities = [fTorrent filePrioritiesForIndexes: [item objectForKey: @"Indexes"]];
        switch ([priorities count])
        {
            case 0:
                return NSLocalizedString(@"Priority Not Available", "files tab -> tooltip");
            case 1:
                switch ([[priorities anyObject] intValue])
                {
                    case TR_PRI_LOW:
                        return NSLocalizedString(@"Low Priority", "files tab -> tooltip");
                    case TR_PRI_HIGH:
                        return NSLocalizedString(@"High Priority", "files tab -> tooltip");
                    case TR_PRI_NORMAL:
                        return NSLocalizedString(@"Normal Priority", "files tab -> tooltip");
                }
                break;
            default:
                return NSLocalizedString(@"Multiple Priorities", "files tab -> tooltip");
        }
    }
    else;
    
    return nil;
}

- (float) outlineView: (NSOutlineView *) outlineView heightOfRowByItem: (id) item
{
    if ([[item objectForKey: @"IsFolder"] boolValue])
        return ROW_SMALL_HEIGHT;
    else
        return [outlineView rowHeight];
}

- (void) setCheck: (id) sender
{
    int state = sender == fFileCheckItem ? NSOnState : NSOffState;
    
    NSIndexSet * indexSet = [fOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fOutline itemAtRow: i] objectForKey: @"Indexes"]];
    
    [fTorrent setFileCheckState: state forIndexes: itemIndexes];
    [fOutline reloadData];
}

- (void) setOnlySelectedCheck: (id) sender
{
    NSIndexSet * indexSet = [fOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fOutline itemAtRow: i] objectForKey: @"Indexes"]];
    
    [fTorrent setFileCheckState: NSOnState forIndexes: itemIndexes];
    
    NSMutableIndexSet * remainingItemIndexes = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])];
    [remainingItemIndexes removeIndexes: itemIndexes];
    [fTorrent setFileCheckState: NSOffState forIndexes: remainingItemIndexes];
    
    [fOutline reloadData];
}

- (void) setPriority: (id) sender
{
    int priority;
    if (sender == fFilePriorityHigh)
        priority = TR_PRI_HIGH;
    else if (sender == fFilePriorityLow)
        priority = TR_PRI_LOW;
    else
        priority = TR_PRI_NORMAL;
    
    NSIndexSet * indexSet = [fOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fOutline itemAtRow: i] objectForKey: @"Indexes"]];
    
    [fTorrent setFilePriority: priority forIndexes: itemIndexes];
    [fOutline reloadData];
}

- (void) revealFile: (id) sender
{
    NSString * folder = [fTorrent downloadFolder];
    NSIndexSet * indexes = [fOutline selectedRowIndexes];
    int i;
    for (i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
        [[NSWorkspace sharedWorkspace] selectFile: [folder stringByAppendingPathComponent:
                [[fOutline itemAtRow: i] objectForKey: @"Path"]] inFileViewerRootedAtPath: nil];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    if (!fTorrent)
        return NO;
    
    SEL action = [menuItem action];
    
    if (action == @selector(revealFile:))
    {
        NSString * downloadFolder = [fTorrent downloadFolder];
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[NSFileManager defaultManager] fileExistsAtPath:
                    [downloadFolder stringByAppendingPathComponent: [[[fTorrent fileList] objectAtIndex: i] objectForKey: @"Path"]]])
                return YES;
        return NO;
    }
    
    if (action == @selector(setCheck:))
    {
        if ([fOutline numberOfSelectedRows] <= 0)
            return NO;
        
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
        int i, state = (menuItem == fFileCheckItem) ? NSOnState : NSOffState;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [itemIndexes addIndexes: [[fOutline itemAtRow: i] objectForKey: @"Indexes"]];
        
        return [fTorrent checkForFiles: itemIndexes] != state && [fTorrent canChangeDownloadCheckForFiles: itemIndexes];
    }
    
    if (action == @selector(setOnlySelectedCheck:))
    {
        if ([fOutline numberOfSelectedRows] <= 0)
            return NO;
        
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
        int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [itemIndexes addIndexes: [[fOutline itemAtRow: i] objectForKey: @"Indexes"]];
            
        return [fTorrent canChangeDownloadCheckForFiles: itemIndexes];
    }
    
    if (action == @selector(setPriority:))
    {
        if ([fOutline numberOfSelectedRows] <= 0)
        {
            [menuItem setState: NSOffState];
            return NO;
        }
        
        //determine which priorities are checked
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        BOOL current = NO, other = NO;
        int i, priority;
        
        if (menuItem == fFilePriorityHigh)
            priority = TR_PRI_HIGH;
        else if (menuItem == fFilePriorityLow)
            priority = TR_PRI_LOW;
        else
            priority = TR_PRI_NORMAL;
        
        NSIndexSet * fileIndexSet;
        for (i = [indexSet firstIndex]; i != NSNotFound && (!current || !other); i = [indexSet indexGreaterThanIndex: i])
        {
            fileIndexSet = [[fOutline itemAtRow: i] objectForKey: @"Indexes"];
            if (![fTorrent canChangeDownloadCheckForFiles: fileIndexSet])
                continue;
            else if ([fTorrent hasFilePriority: priority forIndexes: fileIndexSet])
                current = YES;
            else
                other = YES;
        }
        
        [menuItem setState: current ? NSOnState : NSOffState];
        return current || other;
    }
    
    return YES;
}

@end
