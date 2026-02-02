// This file Copyright © 2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "VersionComparator.h"

@implementation VersionComparator
- (NSComparisonResult)compareVersion:(NSString*)versionA toVersion:(NSString*)versionB
{
    // Transmission version format follows:
    // 14714+major.minor.patch&beta
    // 5.0.1-dev     -> 14719.0.100
    // 5.0.1-beta.1  -> 14719.0.101
    // 5.0.1         -> 14719.0.199
    NSArray<NSString*>* versionBComponents = [versionB componentsSeparatedByString:@"."];
    if (versionBComponents.count > 2 && versionBComponents[2].integerValue % 100 != 99 &&
        ![NSUserDefaults.standardUserDefaults boolForKey:@"AutoUpdateBeta"] &&
        ![[[NSBundle mainBundle] objectForInfoDictionaryKey:(NSString*)kCFBundleVersionKey] isEqualToString:versionB])
    {
        // pre-releases are ignored
        return NSOrderedDescending;
    }
    NSArray<NSString*>* versionAComponents = [versionA componentsSeparatedByString:@"."];
    for (NSUInteger idx = 0; versionAComponents.count > idx || versionBComponents.count > idx; idx++)
    {
        NSInteger vA = versionAComponents.count > idx ? versionAComponents[idx].integerValue : 0;
        NSInteger vB = versionBComponents.count > idx ? versionBComponents[idx].integerValue : 0;
        if (vA < vB)
        {
            return NSOrderedAscending;
        }
        if (vA > vB)
        {
            return NSOrderedDescending;
        }
    }
    return NSOrderedSame;
}
@end
