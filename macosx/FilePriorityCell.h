/* FilePriorityCell */

#import <Cocoa/Cocoa.h>
#import "FileOutlineView.h"

@interface FilePriorityCell : NSSegmentedCell
{
    FileOutlineView * fParentView;
    NSMutableDictionary * fItem;
}

- (void) setItem: (NSMutableDictionary *) item;

@end
