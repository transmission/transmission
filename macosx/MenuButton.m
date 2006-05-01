#import "MenuButton.h"

@implementation MenuButton

- (void) mouseDown: (NSEvent *) event
{
   [self setState: NSOnState];
   [self highlight: YES];
   
   [NSMenu popUpContextMenu: [self menu] withEvent: event forView: self];
   
   [self setState: NSOffState];
   [self highlight: NO];
}

- (NSMenu *) menuForEvent: (NSEvent *) e
{
    return nil;
}

@end
