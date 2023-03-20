// This file Copyright © 2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "VersionComparator.h"

@implementation VersionComparator
- (NSComparisonResult)compareVersion:(NSString*)versionA toVersion:(NSString*)versionB
{
    // Transmission version format follows:
    // 14714.major.minor.patch.beta
    // caveat: "beta" is a pre-release number, so it comes before a release version.
    NSArray<NSString*>* versionBComponents = [versionB componentsSeparatedByString:@"."];
    if (versionBComponents.count > 4 && versionBComponents[4].integerValue > 0 &&
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
        if (idx == 4)
        {
            // releases are greater than pre-releases
            vA = vA == 0 ? NSIntegerMax : vA - 1;
            vB = vB == 0 ? NSIntegerMax : vB - 1;
        }
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
