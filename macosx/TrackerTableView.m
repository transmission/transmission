/******************************************************************************
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

#import "TrackerTableView.h"
#import "Torrent.h"
#import "TrackerNode.h"

@implementation TrackerTableView

- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
}

- (void) setTrackers: (NSArray *) trackers
{
    fTrackers = trackers;
}

- (void) copy: (id) sender
{
    NSMutableArray * addresses = [NSMutableArray arrayWithCapacity: [fTrackers count]];
    NSIndexSet * indexes = [self selectedRowIndexes];
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        id item = fTrackers[i];
        if (![item isKindOfClass: [TrackerNode class]])
        {
            for (++i; i < [fTrackers count] && [fTrackers[i] isKindOfClass: [TrackerNode class]]; ++i)
                [addresses addObject: [(TrackerNode *)fTrackers[i] fullAnnounceAddress]];
            --i;
        }
        else
            [addresses addObject: [(TrackerNode *)item fullAnnounceAddress]];
    }

    NSString * text = [addresses componentsJoinedByString: @"\n"];

    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb writeObjects: @[text]];
}

- (void) paste: (id) sender
{
    NSAssert(fTorrent != nil, @"no torrent but trying to paste; should not be able to call this method");

    BOOL added = NO;

    NSArray * items = [[NSPasteboard generalPasteboard] readObjectsForClasses: @[[NSString class]] options: nil];
    NSAssert(items != nil, @"no string items to paste; should not be able to call this method");

    for (NSString * pbItem in items)
    {
        for (NSString * item in [pbItem componentsSeparatedByString: @"\n"])
            if ([fTorrent addTrackerToNewTier: item])
                added = YES;
    }

    //none added
    if (!added)
        NSBeep();
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    const SEL action = [menuItem action];

    if (action == @selector(copy:))
        return [self numberOfSelectedRows] > 0;

    if (action == @selector(paste:))
        return fTorrent && [[NSPasteboard generalPasteboard] canReadObjectForClasses: @[[NSString class]] options: nil];

    return YES;
}

@end
