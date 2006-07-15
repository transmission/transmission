#import "FilterBarView.h"

@implementation FilterBarView

- (id) initWithFrame: (NSRect) frameRect
{
	if ((self = [super initWithFrame: frameRect]))
    {
		fBackgroundColor = [[NSColor colorWithCalibratedRed: 0.7137 green: 0.7529 blue: 0.8118 alpha: 1.0] retain];
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
    NSRectFill([self bounds]);
}

@end

