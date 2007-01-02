#import "ActionPopUpButton.h"

@implementation ActionPopUpButton

- (id) initWithCoder: (NSCoder *) coder
{
	if ((self = [super initWithCoder: coder]))
    {
        fImage = [NSImage imageNamed: @"ActionButton.png"];
        [fImage setFlipped: YES];
        fImagePressed = [NSImage imageNamed: @"ActionButtonPressed.png"];
        [fImagePressed setFlipped: YES];
	}
	return self;
}

- (void) drawRect: (NSRect) rect
{
    NSImage * image = [[self cell] isHighlighted] ? fImagePressed : fImage;
	[image drawInRect: rect fromRect: NSMakeRect(0.0, 0.0, [image size].width, [image size].height)
            operation: NSCompositeSourceOver fraction: 1.0];
}

@end
