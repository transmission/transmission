
#import "ActionMenuSpeedToDisplayLimitTransformer.h"

@implementation ActionMenuSpeedToDisplayLimitTransformer

+ (Class) transformedValueClass
{
    return [NSString class];
}

+ (BOOL) allowsReverseTransformation
{
    return NO;
}

- (id) transformedValue: (id) value
{
    return value == nil ?  nil : [NSString stringWithFormat: NSLocalizedString(@"Limit (%d KB/s)",
                    "Action context menu -> upload/download limit"), [value intValue]];
}


@end