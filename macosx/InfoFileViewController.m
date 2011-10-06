/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2010-2011 Transmission authors and contributors
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

#import "InfoFileViewController.h"
#import "FileListNode.h"
#import "FileOutlineController.h"
#import "FileOutlineView.h"
#import "Torrent.h"

@interface InfoFileViewController (Private)

- (void) setupInfo;

- (BOOL) canQuickLookFile: (FileListNode *) item;

@end

@implementation InfoFileViewController

- (id) init
{
    if ((self = [super initWithNibName: @"InfoFileView" bundle: nil]))
    {
        [self setTitle: NSLocalizedString(@"Files", "Inspector view -> title")];
    }
    
    return self;
}

- (void) awakeFromNib
{
    const CGFloat height = [[NSUserDefaults standardUserDefaults] floatForKey: @"InspectorContentHeightFiles"];
    if (height != 0.0)
    {
        NSRect viewRect = [[self view] frame];
        viewRect.size.height = height;
        [[self view] setFrame: viewRect];
    }
    
    [[fFileFilterField cell] setPlaceholderString: NSLocalizedString(@"Filter", "inspector -> file filter")];
}

- (void) dealloc
{
    [fTorrents release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    //don't check if it's the same in case the metadata changed
    [fTorrents release];
    fTorrents = [torrents retain];
    
    fSet = NO;
}

- (void) updateInfo
{
    if (!fSet)
        [self setupInfo];
    
    if ([fTorrents count] == 1)
        [fFileController reloadData];
}

- (void) saveViewSize
{
    [[NSUserDefaults standardUserDefaults] setFloat: NSHeight([[self view] frame]) forKey: @"InspectorContentHeightFiles"];
}

- (void) setFileFilterText: (id) sender
{
    [fFileController setFilterText: [sender stringValue]];
}

- (NSArray *) quickLookURLs
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSIndexSet * indexes = [fileOutlineView selectedRowIndexes];
    NSMutableArray * urlArray = [NSMutableArray arrayWithCapacity: [indexes count]];
    
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        FileListNode * item = [fileOutlineView itemAtRow: i];
        if ([self canQuickLookFile: item])
            [urlArray addObject: [NSURL fileURLWithPath: [torrent fileLocation: item]]];
    }
    
    return urlArray;
}

- (BOOL) canQuickLook
{
    if ([fTorrents count] != 1)
        return NO;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    if (![torrent isFolder])
        return NO;
    
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    NSIndexSet * indexes = [fileOutlineView selectedRowIndexes];
    
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
        if ([self canQuickLookFile: [fileOutlineView itemAtRow: i]])
            return YES;
    
    return NO;
}

- (NSRect) quickLookSourceFrameForPreviewItem: (id <QLPreviewItem>) item
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    
    NSString * fullPath = [(NSURL *)item path];
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSRange visibleRows = [fileOutlineView rowsInRect: [fileOutlineView bounds]];
    
    for (NSUInteger row = visibleRows.location; row < NSMaxRange(visibleRows); row++)
    {
        FileListNode * rowItem = [fileOutlineView itemAtRow: row];
        if ([[torrent fileLocation: rowItem] isEqualToString: fullPath])
        {
            NSRect frame = [fileOutlineView iconRectForRow: row];
            
            if (!NSIntersectsRect([fileOutlineView visibleRect], frame))
                return NSZeroRect;
            
            frame.origin = [fileOutlineView convertPoint: frame.origin toView: nil];
            frame.origin = [[[self view] window] convertBaseToScreen: frame.origin];
            frame.origin.y -= frame.size.height;
            return frame;
        }
    }
    
    return NSZeroRect;
}

@end

@implementation InfoFileViewController (Private)

- (void) setupInfo
{
    [fFileFilterField setStringValue: @""];
    
    if ([fTorrents count] == 1)
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fFileController setTorrent: torrent];
        [fFileFilterField setEnabled: [torrent isFolder]];
    }
    else
    {
        [fFileController setTorrent: nil];
        [fFileFilterField setEnabled: NO];
    }
    
    fSet = YES;
}

- (BOOL) canQuickLookFile: (FileListNode *) item
{
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    return ([item isFolder] || [torrent fileProgress: item] >= 1.0) && [torrent fileLocation: item];
}

@end
