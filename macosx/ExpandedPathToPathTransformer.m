
#import "ExpandedPathToPathTransformer.h"

@implementation ExpandedPathToPathTransformer

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
    return value == nil ?  nil : [value lastPathComponent];
}

@end