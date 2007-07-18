/* FilePriorityCell */

#import <Cocoa/Cocoa.h>
#import "FileOutlineView.h"

@interface FilePriorityCell : NSSegmentedCell
{
    NSMutableDictionary * fItem;
    
    NSImage * fLowImage, * fHighImage, * fNormalImage, * fMixedImage, * fNoneImage;
}

- (void) setItem: (NSMutableDictionary *) item;

@end
