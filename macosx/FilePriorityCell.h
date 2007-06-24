/* FilePriorityCell */

#import <Cocoa/Cocoa.h>
#import "FileOutlineView.h"

@interface FilePriorityCell : NSSegmentedCell
{
    NSMutableDictionary * fItem;
}

- (void) setItem: (NSMutableDictionary *) item;

@end
