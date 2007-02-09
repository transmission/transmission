/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "StringAdditions.h"
#import <transmission.h>

@implementation NSString (StringAdditions)

+ (NSString *) ellipsis
{
	return [NSString stringWithUTF8String: "\xE2\x80\xA6"];
}

- (NSString *) stringByAppendingEllipsis
{
	return [self stringByAppendingString: [NSString ellipsis]];
}

+ (NSString *) stringWithInt: (int) value
{
    return [NSString stringWithFormat: @"%d", value];
}

+ (NSString *) stringForFileSize: (uint64_t) size
{
    if (size < 1024)
        return [NSString stringWithFormat: NSLocalizedString(@"%lld bytes", "File size"), size];

    float convertedSize = (float) size;
    NSString * unit;
    if (size < 1048576)
    {
        convertedSize /= 1024.0;
        unit = NSLocalizedString(@" KB", "File size (beware of leading space)");
    }
    else if (size < 1073741824)
    {
        convertedSize /= 1048576.0;
        unit = NSLocalizedString(@" MB", "File size (beware of leading space)");
    }
    else
    {
        convertedSize /= 1073741824.0;
        unit = NSLocalizedString(@" GB", "File size (beware of leading space)");
    }

    NSString * sizeString;
    //attempt to have values with 3 digits
    if (convertedSize < 10.0)
        sizeString = [NSString stringWithFormat: @"%.2f", convertedSize];
    else if (convertedSize < 100.0)
        sizeString = [NSString stringWithFormat: @"%.1f", convertedSize];
    else
        sizeString = [NSString stringWithFormat: @"%.0f", convertedSize];

    return [sizeString stringByAppendingString: unit];
}

+ (NSString *) stringForSpeed: (float) speed
{
    return [[self stringForSpeedAbbrev: speed] stringByAppendingString:
                    NSLocalizedString(@"B/s", "Transfer speed (Bytes per second)")];
}

+ (NSString *) stringForSpeedAbbrev: (float) speed
{
    if (speed < 1000.0) //0.0 K to 999.9 K
        return [NSString stringWithFormat: @"%.1f K", speed];
    else if (speed < 102400.0) //0.98 M to 99.99 M
        return [NSString stringWithFormat: @"%.2f M", speed / 1024.0];
    else if (speed < 1024000.0) //100.0 M to 999.9 M
        return [NSString stringWithFormat: @"%.1f M", speed / 1024.0];
    else //insane speeds
        return [NSString stringWithFormat: @"%.2f G", speed / 1048576.0];
}

+ (NSString *) stringForRatio: (float) ratio
{
    if (ratio == TR_RATIO_NA)
        return NSLocalizedString(@"N/A", "No Ratio");
    else if (ratio == TR_RATIO_INF)
        return [NSString stringWithUTF8String: "\xE2\x88\x9E"];
    else if (ratio < 10.0)
        return [NSString stringWithFormat: @"%.2f", ratio];
    else if (ratio < 100.0)
        return [NSString stringWithFormat: @"%.1f", ratio];
    else
        return [NSString stringWithFormat: @"%.0f", ratio];
}

- (NSComparisonResult) compareIP: (NSString *) string
{
    NSArray * selfSections = [self componentsSeparatedByString: @"."],
            * newSections = [string componentsSeparatedByString: @"."];
    
    if ([selfSections count] != [newSections count])
        return [selfSections count] > [newSections count] ? NSOrderedDescending : NSOrderedAscending;

    NSEnumerator * selfSectionsEnum = [selfSections objectEnumerator], * newSectionsEnum = [newSections objectEnumerator];
    NSString * selfString, * newString;
    NSComparisonResult result;
    while ((selfString = [selfSectionsEnum nextObject]) && (newString = [newSectionsEnum nextObject]))
        if ((result = [selfString compare: newString options: NSNumericSearch]) != NSOrderedSame)
            return result;
    
    return NSOrderedSame;
}

@end
