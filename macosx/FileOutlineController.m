/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008-2012 Transmission authors and contributors
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
#import "FileListNode.h"
#import "FileOutlineView.h"
#import "FilePriorityCell.h"
#import "FileRenameSheetController.h"
#import "NSApplicationAdditions.h"
#import "NSMutableArrayAdditions.h"
#import "NSStringAdditions.h"
#import <Quartz/Quartz.h>

#define ROW_SMALL_HEIGHT 18.0

typedef enum
{
    FILE_CHECK_TAG,
    FILE_UNCHECK_TAG
} fileCheckMenuTag;

typedef enum
{
    FILE_PRIORITY_HIGH_TAG,
    FILE_PRIORITY_NORMAL_TAG,
    FILE_PRIORITY_LOW_TAG
} filePriorityMenuTag;

@interface FileOutlineController (Private)

- (NSMenu *) menu;

- (NSUInteger) findFileNode: (FileListNode *) node inList: (NSArray *) list atIndexes: (NSIndexSet *) range currentParent: (FileListNode *) currentParent finalParent: (FileListNode **) parent;

@end

@implementation FileOutlineController

- (void) awakeFromNib
{
    fFileList = [[NSMutableArray alloc] init];
    
    [fOutline setDoubleAction: @selector(revealFile:)];
    [fOutline setTarget: self];
    
    //set table header tool tips
    [[fOutline tableColumnWithIdentifier: @"Check"] setHeaderToolTip: NSLocalizedString(@"Download", "file table -> header tool tip")];
    [[fOutline tableColumnWithIdentifier: @"Priority"] setHeaderToolTip: NSLocalizedString(@"Priority", "file table -> header tool tip")];
    
    [fOutline setMenu: [self menu]];
    
    [self setTorrent: nil];
}

- (void) dealloc
{
    [fFileList release];
    [fFilterText release];
    
    [super dealloc];
}

- (FileOutlineView *) outlineView
{
    return fOutline;
}

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
    
    [fFileList setArray: [fTorrent fileList]];
    
    [fFilterText release];
    fFilterText = nil;
    
    [fOutline reloadData];
    [fOutline deselectAll: nil]; //do this after reloading the data #4575
}

- (void) setFilterText: (NSString *) text
{
    NSArray * components = [text betterComponentsSeparatedByCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (!components || [components count] == 0)
    {
        text = nil;
        components = nil;
    }
    
    if ((!text && !fFilterText) || (text && fFilterText && [text isEqualToString: fFilterText]))
        return;

    [fOutline beginUpdates];
    
    NSUInteger currentIndex = 0, totalCount = 0;
    NSMutableArray * itemsToAdd = [NSMutableArray array];
    NSMutableIndexSet * itemsToAddIndexes = [NSMutableIndexSet indexSet];
    
    NSMutableDictionary * removedIndexesForParents = nil; //ugly, but we can't modify the actual file nodes
    
    NSArray * tempList = !text ? [fTorrent fileList] : [fTorrent flatFileList];
    for (FileListNode * item in tempList)
    {
        __block BOOL filter = NO;
        if (components)
        {
            [components enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(id obj, NSUInteger idx, BOOL * stop) {
                if ([[item name] rangeOfString: (NSString *)obj options: (NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)].location == NSNotFound)
                {
                    filter = YES;
                    *stop = YES;
                }
            }];
        }
        
        if (!filter)
        {
            FileListNode * parent = nil;
            NSUInteger previousIndex = ![item isFolder] ? [self findFileNode: item inList: fFileList atIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(currentIndex, [fFileList count]-currentIndex)] currentParent: nil finalParent: &parent] : NSNotFound;
            
            if (previousIndex == NSNotFound)
            {
                [itemsToAdd addObject: item];
                [itemsToAddIndexes addIndex: totalCount];
            }
            else
            {
                BOOL move = YES;
                if (!parent)
                {
                    if (previousIndex != currentIndex)
                        [fFileList moveObjectAtIndex: previousIndex toIndex: currentIndex];
                    else
                        move = NO;
                }
                else
                {
                    [fFileList insertObject: item atIndex: currentIndex];
                    
                    //figure out the index within the semi-edited table - UGLY
                    if (!removedIndexesForParents)
                        removedIndexesForParents = [NSMutableDictionary dictionary];
                    
                    NSMutableIndexSet * removedIndexes = [removedIndexesForParents objectForKey: parent];
                    if (!removedIndexes)
                    {
                        removedIndexes = [NSMutableIndexSet indexSetWithIndex: previousIndex];
                        [removedIndexesForParents setObject: removedIndexes forKey: parent];
                    }
                    else
                    {
                        [removedIndexes addIndex: previousIndex];
                        previousIndex -= [removedIndexes countOfIndexesInRange: NSMakeRange(0, previousIndex)];
                    }
                }
                
                if (move)
                    [fOutline moveItemAtIndex: previousIndex inParent: parent toIndex: currentIndex inParent: nil];
                
                ++currentIndex;
            }
            
            ++totalCount;
        }
    }
    
    //remove trailing items - those are the unused
    if (currentIndex  < [fFileList count])
    {
        const NSRange removeRange = NSMakeRange(currentIndex, [fFileList count]-currentIndex);
        [fFileList removeObjectsInRange: removeRange];
        [fOutline removeItemsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: removeRange] inParent: nil withAnimation: NSTableViewAnimationSlideDown];
    }
    
    //add new items
    [fFileList insertObjects: itemsToAdd atIndexes: itemsToAddIndexes];
    [fOutline insertItemsAtIndexes: itemsToAddIndexes inParent: nil withAnimation: NSTableViewAnimationSlideUp];

    [fOutline endUpdates];
    
    [fFilterText release];
    fFilterText = [text retain];
}

- (void) refresh
{
    [fTorrent updateFileStat];
    
    [fOutline setNeedsDisplay: YES];
}

- (void) outlineViewSelectionDidChange: (NSNotification *) notification
{
    if ([QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible])
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
}

- (NSInteger) outlineView: (NSOutlineView *) outlineView numberOfChildrenOfItem: (id) item
{
    if (!item)
        return fFileList ? [fFileList count] : 0;
    else
    {
        FileListNode * node = (FileListNode *)item;
        return [node isFolder] ? [[node children] count] : 0;
    }
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isItemExpandable: (id) item 
{
    return [(FileListNode *)item isFolder];
}

- (id) outlineView: (NSOutlineView *) outlineView child: (NSInteger) index ofItem: (id) item
{
    return [(item ? [(FileListNode *)item children] : fFileList) objectAtIndex: index];
}

- (id) outlineView: (NSOutlineView *) outlineView objectValueForTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    if ([[tableColumn identifier] isEqualToString: @"Check"])
        return [NSNumber numberWithInteger: [fTorrent checkForFiles: [(FileListNode *)item indexes]]];
    else
        return item;
}

- (void) outlineView: (NSOutlineView *) outlineView willDisplayCell: (id) cell forTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Check"])
        [cell setEnabled: [fTorrent canChangeDownloadCheckForFiles: [(FileListNode *)item indexes]]];
    else if ([identifier isEqualToString: @"Priority"])
    {
        [cell setRepresentedObject: item];
        
        NSInteger hoveredRow = [fOutline hoveredRow];
        [(FilePriorityCell *)cell setHovered: hoveredRow != -1 && hoveredRow == [fOutline rowForItem: item]];
    }
    else;
}

- (void) outlineView: (NSOutlineView *) outlineView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Check"])
    {
        NSIndexSet * indexSet;
        if ([NSEvent modifierFlags] & NSAlternateKeyMask)
            indexSet = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])];
        else
            indexSet = [(FileListNode *)item indexes];
        
        [fTorrent setFileCheckState: [object intValue] != NSOffState ? NSOnState : NSOffState forIndexes: indexSet];
        [fOutline setNeedsDisplay: YES];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
    }
}

- (NSString *) outlineView: (NSOutlineView *) outlineView typeSelectStringForTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    return [(FileListNode *)item name];
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
        tableColumn: (NSTableColumn *) tableColumn item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Name"])
    {
        NSString * path = [fTorrent fileLocation: item];
        if (!path)
            path = [[(FileListNode *)item path] stringByAppendingPathComponent: [(FileListNode *)item name]];
        return path;
    }
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
        NSSet * priorities = [fTorrent filePrioritiesForIndexes: [(FileListNode *)item indexes]];
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

- (CGFloat) outlineView: (NSOutlineView *) outlineView heightOfRowByItem: (id) item
{
    if ([(FileListNode *)item isFolder])
        return ROW_SMALL_HEIGHT;
    else
        return [outlineView rowHeight];
}

- (void) setCheck: (id) sender
{
    NSInteger state = [sender tag] == FILE_UNCHECK_TAG ? NSOffState : NSOnState;
    
    NSIndexSet * indexSet = [fOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fOutline itemAtRow: i] indexes]];
    
    [fTorrent setFileCheckState: state forIndexes: itemIndexes];
    [fOutline setNeedsDisplay: YES];
}

- (void) setOnlySelectedCheck: (id) sender
{
    NSIndexSet * indexSet = [fOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fOutline itemAtRow: i] indexes]];
    
    [fTorrent setFileCheckState: NSOnState forIndexes: itemIndexes];
    
    NSMutableIndexSet * remainingItemIndexes = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])];
    [remainingItemIndexes removeIndexes: itemIndexes];
    [fTorrent setFileCheckState: NSOffState forIndexes: remainingItemIndexes];
    
    [fOutline setNeedsDisplay: YES];
}

- (void) checkAll
{
    NSIndexSet * indexSet = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])];
    [fTorrent setFileCheckState: NSOnState forIndexes: indexSet];
    [fOutline setNeedsDisplay: YES];
}

- (void) uncheckAll
{
    NSIndexSet * indexSet = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])];
    [fTorrent setFileCheckState: NSOffState forIndexes: indexSet];
    [fOutline setNeedsDisplay: YES];
}

- (void) setPriority: (id) sender
{
    tr_priority_t priority;
    switch ([sender tag])
    {
        case FILE_PRIORITY_HIGH_TAG:
            priority = TR_PRI_HIGH;
            break;
        case FILE_PRIORITY_NORMAL_TAG:
            priority = TR_PRI_NORMAL;
            break;
        case FILE_PRIORITY_LOW_TAG:
            priority = TR_PRI_LOW;
    }
    
    NSIndexSet * indexSet = [fOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fOutline itemAtRow: i] indexes]];
    
    [fTorrent setFilePriority: priority forIndexes: itemIndexes];
    [fOutline setNeedsDisplay: YES];
}

- (void) revealFile: (id) sender
{
    NSIndexSet * indexes = [fOutline selectedRowIndexes];
    NSMutableArray * paths = [NSMutableArray arrayWithCapacity: [indexes count]];
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        NSString * path = [fTorrent fileLocation: [fOutline itemAtRow: i]];
        if (path)
            [paths addObject: [NSURL fileURLWithPath: path]];
    }
    
    if ([paths count] > 0)
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: paths];
}

- (void) renameSelected: (id) sender
{
    NSIndexSet * indexes = [fOutline selectedRowIndexes];
    NSAssert([indexes count] == 1, @"1 file needs to be selected to rename, but %ld are selected", [indexes count]);
    
    FileListNode * node = [fOutline itemAtRow: [indexes firstIndex]];
    Torrent * torrent = [node torrent];
    if (![torrent isFolder])
    {
        [FileRenameSheetController presentSheetForTorrent: torrent modalForWindow: [fOutline window] completionHandler: ^(BOOL didRename) {
            if (didRename)
            {
                [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
                [[NSNotificationCenter defaultCenter] postNotificationName: @"ResetInspector" object: self userInfo: @{ @"Torrent" : torrent }];
            }
        }];
    }
    else
    {
        [FileRenameSheetController presentSheetForFileListNode: node modalForWindow: [fOutline window] completionHandler: ^(BOOL didRename) {
            #warning instead of calling reset inspector, just resort?
            if (didRename)
                [[NSNotificationCenter defaultCenter] postNotificationName: @"ResetInspector" object: self userInfo: @{ @"Torrent" : torrent }];
        }];
    }
}

#warning make real view controller (Leopard-only) so that Command-R will work
- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    if (!fTorrent)
        return NO;
    
    SEL action = [menuItem action];
    
    if (action == @selector(revealFile:))
    {
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([fTorrent fileLocation: [fOutline itemAtRow: i]] != nil)
                return YES;
        return NO;
    }
    
    if (action == @selector(setCheck:))
    {
        if ([fOutline numberOfSelectedRows] == 0)
            return NO;
        
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
        for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [itemIndexes addIndexes: [[fOutline itemAtRow: i] indexes]];
        
        NSInteger state = ([menuItem tag] == FILE_CHECK_TAG) ? NSOnState : NSOffState;
        return [fTorrent checkForFiles: itemIndexes] != state && [fTorrent canChangeDownloadCheckForFiles: itemIndexes];
    }
    
    if (action == @selector(setOnlySelectedCheck:))
    {
        if ([fOutline numberOfSelectedRows] == 0)
            return NO;
        
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
        for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [itemIndexes addIndexes: [[fOutline itemAtRow: i] indexes]];
        
        return [fTorrent canChangeDownloadCheckForFiles: itemIndexes];
    }
    
    if (action == @selector(setPriority:))
    {
        if ([fOutline numberOfSelectedRows] == 0)
        {
            [menuItem setState: NSOffState];
            return NO;
        }
        
        //determine which priorities are checked
        NSIndexSet * indexSet = [fOutline selectedRowIndexes];
        tr_priority_t priority;
        switch ([menuItem tag])
        {
            case FILE_PRIORITY_HIGH_TAG:
                priority = TR_PRI_HIGH;
                break;
            case FILE_PRIORITY_NORMAL_TAG:
                priority = TR_PRI_NORMAL;
                break;
            case FILE_PRIORITY_LOW_TAG:
                priority = TR_PRI_LOW;
                break;
        }
        
        BOOL current = NO, canChange = NO;
        for (NSInteger i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            NSIndexSet * fileIndexSet = [[fOutline itemAtRow: i] indexes];
            if (![fTorrent canChangeDownloadCheckForFiles: fileIndexSet])
                continue;
            
            canChange = YES;
            if ([fTorrent hasFilePriority: priority forIndexes: fileIndexSet])
            {
                current = YES;
                break;
            }
        }
        
        [menuItem setState: current ? NSOnState : NSOffState];
        return canChange;
    }
    
    if (action == @selector(renameSelected:))
    {
        return [fOutline numberOfSelectedRows] == 1;
    }
    
    return YES;
}

@end

@implementation FileOutlineController (Private)

- (NSMenu *) menu
{
    NSMenu * menu = [[NSMenu alloc] initWithTitle: @"File Outline Menu"];
    
    //check and uncheck
    NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Check Selected", "File Outline -> Menu")
            action: @selector(setCheck:) keyEquivalent: @""];
    [item setTarget: self];
    [item setTag: FILE_CHECK_TAG];
    [menu addItem: item];
    [item release];
    
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Uncheck Selected", "File Outline -> Menu")
            action: @selector(setCheck:) keyEquivalent: @""];
    [item setTarget: self];
    [item setTag: FILE_UNCHECK_TAG];
    [menu addItem: item];
    [item release];
    
    //only check selected
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Only Check Selected", "File Outline -> Menu")
            action: @selector(setOnlySelectedCheck:) keyEquivalent: @""];
    [item setTarget: self];
    [menu addItem: item];
    [item release];
    
    [menu addItem: [NSMenuItem separatorItem]];
    
    //priority
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Priority", "File Outline -> Menu") action: NULL keyEquivalent: @""];
    NSMenu * priorityMenu = [[NSMenu alloc] initWithTitle: @"File Priority Menu"];
    [item setSubmenu: priorityMenu];
    [menu addItem: item];
    [item release];
    
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"High", "File Outline -> Priority Menu")
            action: @selector(setPriority:) keyEquivalent: @""];
    [item setTarget: self];
    [item setTag: FILE_PRIORITY_HIGH_TAG];
    [item setImage: [NSImage imageNamed: @"PriorityHighTemplate"]];
    [priorityMenu addItem: item];
    [item release];
    
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Normal", "File Outline -> Priority Menu")
            action: @selector(setPriority:) keyEquivalent: @""];
    [item setTarget: self];
    [item setTag: FILE_PRIORITY_NORMAL_TAG];
    [item setImage: [NSImage imageNamed: @"PriorityNormalTemplate"]];
    [priorityMenu addItem: item];
    [item release];
    
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Low", "File Outline -> Priority Menu")
            action: @selector(setPriority:) keyEquivalent: @""];
    [item setTarget: self];
    [item setTag: FILE_PRIORITY_LOW_TAG];
    [item setImage: [NSImage imageNamed: @"PriorityLowTemplate"]];
    [priorityMenu addItem: item];
    [item release];
    
    [priorityMenu release];
    
    [menu addItem: [NSMenuItem separatorItem]];
    
    //reveal in finder
    item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"Show in Finder", "File Outline -> Menu")
                                      action: @selector(revealFile:) keyEquivalent: @""];
    [item setTarget: self];
    [menu addItem: item];
    [item release];
    
    [menu addItem: [NSMenuItem separatorItem]];
    
    //rename
    item = [[NSMenuItem alloc] initWithTitle: [NSLocalizedString(@"Rename File", "File Outline -> Menu") stringByAppendingEllipsis]
                                                   action: @selector(renameSelected:) keyEquivalent: @""];
    [item setTarget: self];
    [menu addItem: item];
    [item release];
    
    return [menu autorelease];
}

- (NSUInteger) findFileNode: (FileListNode *) node inList: (NSArray *) list atIndexes: (NSIndexSet *) indexes currentParent: (FileListNode *) currentParent finalParent: (FileListNode **) parent
{
    NSAssert(![node isFolder], @"Looking up folder node!");
    
    __block NSUInteger retIndex = NSNotFound;
    
    [list enumerateObjectsAtIndexes: indexes options: NSEnumerationConcurrent usingBlock: ^(id checkNode, NSUInteger index, BOOL * stop) {
        if ([[checkNode indexes] containsIndex: [[node indexes] firstIndex]])
        {
            if (![checkNode isFolder])
            {
                NSAssert2([checkNode isEqualTo: node], @"Expected file nodes to be equal: %@ %@", checkNode, node);
                
                *parent = currentParent;
                retIndex = index;
            }
            else
            {
                const NSUInteger subIndex = [self findFileNode: node inList: [checkNode children] atIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [[checkNode children] count])] currentParent: checkNode finalParent: parent];
                NSAssert(subIndex != NSNotFound, @"We didn't find an expected file node.");
                retIndex = subIndex;
            }
            
            *stop = YES;
        }
    }];
    
    return retIndex;
}

@end
