#import "FileBrowserCell.h"

#define SPACE 2.0

@implementation FileBrowserCell

- (void) awakeFromNib
{
    [self setLeaf: YES];
}

- (void) setImage: (NSImage *) image
{
    [image setFlipped: YES];
    [image setScalesWhenResized: YES];
    [super setImage: image];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    //image
    float imageHeight = cellFrame.size.height - 2.0;
    
    NSImage * image = [self image];
    [image setSize: NSMakeSize(imageHeight, imageHeight)];
    NSRect imageRect = NSMakeRect(cellFrame.origin.x + 2.0 * SPACE,
                                    cellFrame.origin.y + (cellFrame.size.height - imageHeight) / 2.0,
                                    imageHeight, imageHeight);
    
    [image drawInRect: imageRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    //text
    NSArray * strings = [[self stringValue] componentsSeparatedByString: @"\n"];
    
    NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                    isEqual: [NSColor alternateSelectedControlColor]];
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor controlTextColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
    
    float textHeight = [(NSTableView *)controlView rowHeight];
    NSRect textRect = NSMakeRect(NSMaxX(imageRect) + SPACE, cellFrame.origin.y + 2.0,
                                    NSMaxX(cellFrame) - NSMaxX(imageRect) - 2.0 * SPACE, textHeight);
    
    NSAttributedString * text = [[NSAttributedString alloc] initWithString: [strings objectAtIndex: 0]
                                                                attributes: nameAttributes];
    [text drawInRect: textRect];
    [text release];
    
    //bottomText
    if ([strings count] > 1)
    {
        NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        NSRect bottomTextRect = textRect;
        bottomTextRect.origin.y += textHeight;
        bottomTextRect.size.height = cellFrame.size.height - textHeight;
        
        NSMutableAttributedString * bottomText = [[NSMutableAttributedString alloc] initWithString: [strings objectAtIndex: 1]
                                                                                        attributes: statusAttributes];
        
        [bottomText drawInRect: bottomTextRect];
        [bottomText release];
        
        [statusAttributes release];
    }
    
    [paragraphStyle release];
    [nameAttributes release];
}

@end
