#import "SmoothAquaView.h"

@implementation SmoothAquaView

- (id) initWithFrame: (NSRect) frameRect
{
	if ((self = [super initWithFrame:frameRect]))
    {
		fBackgroundColor = [NSColor colorWithPatternImage:
                            [NSImage imageNamed: @"StatusBorder"]];
        [fBackgroundColor retain];
	}
	return self;
}

- (void) dealloc
{
    [fBackgroundColor release];
    [super dealloc];
}

- (void) drawRect: (NSRect) rect
{
    [fBackgroundColor set];
    [[NSGraphicsContext currentContext] setPatternPhase: [self frame].origin];
    NSRectFill([self bounds]);
}

@end
