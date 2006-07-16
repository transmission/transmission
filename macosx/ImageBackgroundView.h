/* ImageBackgroundView */

#import <Cocoa/Cocoa.h>

@interface ImageBackgroundView : NSView
{
    NSColor * fBackgroundColor;
}

- (void) setBackgroundImage: (NSImage *) image;

@end
