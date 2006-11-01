
#import "ActionMenuRatioToDisplayRatioTransformer.h"

@implementation ActionMenuRatioToDisplayRatioTransformer

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
    return value == nil ?  nil : [NSString stringWithFormat: NSLocalizedString(@"Stop at Ratio (%.2f)",
                                                "Action context menu -> ratio stop"), [value floatValue]];
}

@end