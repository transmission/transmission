/* FilePriorityCell */

#import <Cocoa/Cocoa.h>
#import "FileOutlineView.h"

@interface FilePriorityCell : NSSegmentedCell
{
    FileOutlineView * fParentView;
    NSMutableDictionary * fItem;
}

- (id) initForParentView: (FileOutlineView *) parentView;
- (void) setItem: (NSMutableDictionary *) item;

@end
