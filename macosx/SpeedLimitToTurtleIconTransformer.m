
#import "SpeedLimitToTurtleIconTransformer.h"

@implementation SpeedLimitToTurtleIconTransformer

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
    
    return [value boolValue] ? ([NSColor currentControlTint] == NSBlueControlTint
            ? [NSImage imageNamed: @"SpeedLimitButtonBlue.png"] : [NSImage imageNamed: @"SpeedLimitButtonGraphite.png"])
            : [NSImage imageNamed: @"SpeedLimitButton.png"];
}

@end