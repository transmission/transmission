
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
    
    NSString * path = [value stringByExpandingTildeInPath];
    NSImage * icon;
    //show a folder icon if the folder doesn't exist
    if (![[NSFileManager defaultManager] fileExistsAtPath: path] && [[path pathExtension] isEqualToString: @""])
        icon = [[NSWorkspace sharedWorkspace] iconForFileType: NSFileTypeForHFSTypeCode('fldr')];
    else
        icon = [[NSWorkspace sharedWorkspace] iconForFile: [value stringByExpandingTildeInPath]];
    
    [icon setScalesWhenResized: YES];
    [icon setSize: NSMakeSize(16.0, 16.0)];
    
    return icon;
}

@end