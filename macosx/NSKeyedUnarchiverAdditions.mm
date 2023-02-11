// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "NSKeyedUnarchiverAdditions.h"

@implementation NSKeyedUnarchiver (NSUnarchiverAdditions)

+ (nullable id)deprecatedUnarchiveObjectWithData:(NSData*)data
{
    // ignoring deprecation warning on NSUnarchiver:
    // there are no compatible alternatives to handle the old data when migrating from Transmission 3,
    // so we'll use NSUnarchiver as long as Apple supports it
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [NSUnarchiver unarchiveObjectWithData:data];
#pragma clang diagnostic pop
}

@end
