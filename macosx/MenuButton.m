#import "MenuButton.h"

@implementation MenuButton

- (void) mouseDown: (NSEvent *) event
{
   [self setState: NSOnState];
   [self highlight: YES];
   
   NSPoint location = NSMakePoint(3, -2);
   
   NSEvent * theEvent= [NSEvent mouseEventWithType: [event type]
                        location: location
                        modifierFlags: [event modifierFlags]
                        timestamp: [event timestamp]
                        windowNumber: [event windowNumber]
                        context: [event context]
                        eventNumber: [event eventNumber]
                        clickCount: [event clickCount]
                        pressure: [event pressure]];

   [NSMenu popUpContextMenu: [self menu] withEvent: theEvent forView: self];
   
   [self setState: NSOffState];
   [self highlight: NO];
}

- (NSMenu *) menuForEvent: (NSEvent *) e
{
    return nil;
}

@end
