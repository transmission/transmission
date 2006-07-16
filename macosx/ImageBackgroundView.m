#import "ImageBackgroundView.h"

@implementation ImageBackgroundView

- (void) setBackgroundImage: (NSImage *) image
{
    if (fBackgroundColor)
        [fBackgroundColor release];
    fBackgroundColor = [[NSColor colorWithPatternImage: image] retain];
}

- (void) dealloc
{
    [fBackgroundColor release];
    [super dealloc];
}

- (void) drawRect: (NSRect) rect
{
    if (fBackgroundColor)
    {
        [fBackgroundColor set];
        [[NSGraphicsContext currentContext] setPatternPhase: [self frame].origin];
        NSRectFill([self bounds]);
    }
}

@end
