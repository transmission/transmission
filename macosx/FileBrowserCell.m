#import "FileBrowserCell.h"

#define SPACE 2.0

@implementation FileBrowserCell

- (void) setImage: (NSImage *) image
{
    [image setFlipped: YES];
    [super setImage: image];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //image
    NSImage * icon = [self image];
    NSSize iconSize = [icon size];
    NSRect imageRect = NSMakeRect(cellFrame.origin.x + 2.0 * SPACE, cellFrame.origin.y, iconSize.width, iconSize.height);
    
    [icon drawInRect: imageRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    //text
    NSRect textRect = NSMakeRect(NSMaxX(imageRect) + SPACE, cellFrame.origin.y,
                                    cellFrame.size.width - 4.0 * SPACE, cellFrame.size.height);
    
    if ([self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                    isEqual: [NSColor alternateSelectedControlColor]])
    {
        NSMutableAttributedString * text = [[self attributedStringValue] mutableCopy];
        NSDictionary * attributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                                        [NSColor whiteColor], NSForegroundColorAttributeName, nil];
        [text addAttributes: attributes range: NSMakeRange(0, [text length])];
        [text drawInRect: textRect];
        
        [attributes release];
        [text release];
    }
    else
        [[self attributedStringValue] drawInRect: textRect];
        
}

@end
