#import "ActionPopUpButtonCell.h"

@implementation ActionPopUpButtonCell

- (id) initTextCell: (NSString *) string pullsDown: (BOOL) pullDown
{
	if (self = [super initTextCell: string pullsDown: YES])
    {
        fImage = [NSImage imageNamed: @"ActionButton.png"];
        [fImage setFlipped: YES];
        fImagePressed = [NSImage imageNamed: @"ActionButtonPressed.png"];
        [fImagePressed setFlipped: YES];
    }
	return self;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    NSImage * image = [self isHighlighted] ? fImagePressed : fImage;
	[image drawInRect: cellFrame fromRect: NSMakeRect(0.0, 0.0, [image size].width, [image size].height)
            operation: NSCompositeSourceOver fraction: 1.0];
}

@end
