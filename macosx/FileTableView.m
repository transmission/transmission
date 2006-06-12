#import "FileTableView.h"

@implementation FileTableView

- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

@end
