#import "InfoWindow.h"
#import "InfoWindowController.h"

@implementation InfoWindow

- (void) awakeFromNib
{
    [self setBecomesKeyOnlyIfNeeded: YES];
    [self setAcceptsMouseMovedEvents: YES];
}

- (void) mouseMoved: (NSEvent *) event
{
    [(InfoWindowController *)[self windowController] setFileOutlineHoverRowForEvent: event];
}

@end
