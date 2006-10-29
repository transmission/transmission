
#import "ExpandedPathToIconTransformer.h"

@implementation ExpandedPathToIconTransformer

+ (Class) transformedValueClass
{
    return [NSImage class];
}

+ (BOOL) allowsReverseTransformation
{
    return NO;
}

- (id) transformedValue: (id) value
{
    if (!value)
        return nil;
    
    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFile: [value stringByExpandingTildeInPath]];
    [icon setScalesWhenResized: YES];
    [icon setSize: NSMakeSize(16.0, 16.0)];
    
    return icon;
}


@end