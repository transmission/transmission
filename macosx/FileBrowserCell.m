#import "FileBrowserCell.h"
#import "StringAdditions.h"

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
    NSMutableDictionary * item = [self objectValue];
    
    NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                    isEqual: [NSColor alternateSelectedControlColor]];
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor controlTextColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
    
    NSString * nameString = [item objectForKey: @"Name"];
    NSRect nameTextRect = NSMakeRect(NSMaxX(imageRect) + SPACE, cellFrame.origin.y + 1.0,
            NSMaxX(cellFrame) - NSMaxX(imageRect) - 2.0 * SPACE, [nameString sizeWithAttributes: nameAttributes].height);
    
    [nameString drawInRect: nameTextRect withAttributes: nameAttributes];
    [nameAttributes release];
    
    //status text
    if (![[item objectForKey: @"IsFolder"] boolValue])
    {
        NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        NSString * statusString = [NSString stringForFileSize: [[item objectForKey: @"Size"] unsignedLongLongValue]];
        
        NSRect statusTextRect = nameTextRect;
        statusTextRect.size.height = [statusString sizeWithAttributes: statusAttributes].height;
        statusTextRect.origin.y += (cellFrame.size.height + nameTextRect.size.height - statusTextRect.size.height) * 0.5 - 1.0;
        
        [statusString drawInRect: statusTextRect withAttributes: statusAttributes];
        [statusAttributes release];
    }
    
    [paragraphStyle release];
}

@end
