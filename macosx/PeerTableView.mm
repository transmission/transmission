// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PeerTableView.h"

@implementation PeerTableView

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if ([self rowAtPoint:point] != -1 && [self columnAtPoint:point] == [self columnWithIdentifier:@"Progress"])
    {
        [NSUserDefaults.standardUserDefaults setBool:![NSUserDefaults.standardUserDefaults boolForKey:@"DisplayPeerProgressBarNumber"]
                                              forKey:@"DisplayPeerProgressBarNumber"];

        NSIndexSet *rowIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.numberOfRows)],
                   *columnIndexes = [NSIndexSet indexSetWithIndex:[self columnAtPoint:point]];
        [self reloadDataForRowIndexes:rowIndexes columnIndexes:columnIndexes];
    }
}

@end
