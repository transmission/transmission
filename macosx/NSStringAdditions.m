/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#import "NSStringAdditions.h"
#import <transmission.h>

@implementation NSString (NSStringAdditions)

+ (NSString *) ellipsis
{
	return [NSString stringWithUTF8String: "\xE2\x80\xA6"];
}

- (NSString *) stringByAppendingEllipsis
{
	return [self stringByAppendingString: [NSString ellipsis]];
}

+ (NSString *) stringForFileSize: (uint64_t) size
{
    if (size < 1024)
        return [NSString stringWithFormat: @"%lld %@", size, NSLocalizedString(@"bytes", "File size - bytes")];

    CGFloat convertedSize;
    NSString * unit;
    if (size < pow(1024, 2))
    {
        convertedSize = size / 1024.0f;
        unit = NSLocalizedString(@"KB", "File size - kilobytes");
    }
    else if (size < pow(1024, 3))
    {
        convertedSize = size / powf(1024.0f, 2.0f);
        unit = NSLocalizedString(@"MB", "File size - megabytes");
    }
    else if (size < pow(1024, 4))
    {
        convertedSize = size / powf(1024.0f, 3.0f);
        unit = NSLocalizedString(@"GB", "File size - gigabytes");
    }
    else
    {
        convertedSize = size / powf(1024.0f, 4.0f);
        unit = NSLocalizedString(@"TB", "File size - terabytes");
    }
    
    //attempt to have minimum of 3 digits with at least 1 decimal
    return convertedSize <= 9.995f ? [NSString localizedStringWithFormat: @"%.2f %@", convertedSize, unit]
                                : [NSString localizedStringWithFormat: @"%.1f %@", convertedSize, unit];
}

+ (NSString *) stringForSpeed: (CGFloat) speed
{
    return [[self stringForSpeedAbbrev: speed] stringByAppendingString: NSLocalizedString(@"B/s", "Transfer speed (Bytes per second)")];
}

+ (NSString *) stringForSpeedAbbrev: (CGFloat) speed
{
    if (speed <= 999.95f) //0.0 K to 999.9 K
        return [NSString localizedStringWithFormat: @"%.1f K", speed];
    
    speed /= 1024.0f;
    
    if (speed <= 99.995f) //0.98 M to 99.99 M
        return [NSString localizedStringWithFormat: @"%.2f M", speed];
    else if (speed <= 999.95f) //100.0 M to 999.9 M
        return [NSString localizedStringWithFormat: @"%.1f M", speed];
    else //insane speeds
        return [NSString localizedStringWithFormat: @"%.2f G", (speed / 1024.0f)];
}

+ (NSString *) stringForRatio: (CGFloat) ratio
{
    if (ratio == TR_RATIO_NA)
        return NSLocalizedString(@"N/A", "No Ratio");
    else if (ratio == TR_RATIO_INF)
        return [NSString stringWithUTF8String: "\xE2\x88\x9E"];
    else;
    
    if (ratio <= 9.995f) //0.00 to 9.99
        return [NSString localizedStringWithFormat: @"%.2f", ratio];
    else if (ratio <= 99.95f) //10.0 to 99.9
        return [NSString localizedStringWithFormat: @"%.1f", ratio];
    else //rest are single digit
        return [NSString localizedStringWithFormat: @"%.0f", ratio];
}

+ (NSString *) timeString: (uint64_t) seconds showSeconds: (BOOL) showSeconds
{
    return [NSString timeString: seconds showSeconds: showSeconds maxFields: NSUIntegerMax];
}

+ (NSString *) timeString: (uint64_t) seconds showSeconds: (BOOL) showSeconds maxFields: (NSUInteger) max
{
    NSMutableArray * timeArray = [NSMutableArray arrayWithCapacity: MIN(max, 4)];
    NSUInteger remaining = seconds;
    
    if (max > 0 && seconds >= (24 * 60 * 60))
    {
        NSUInteger days = remaining / (24 * 60 * 60);
        if (days == 1)
            [timeArray addObject: NSLocalizedString(@"1 day", "time string")];
        else
            [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u days", "time string"), days]];
        remaining %= (24 * 60 * 60);
        max--;
    }
    if (max > 0 && seconds >= (60 * 60))
    {
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u hr", "time string"), remaining / (60 * 60)]];
        remaining %= (60 * 60);
        max--;
    }
    if (max > 0 && (!showSeconds || seconds >= 60))
    {
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u min", "time string"), remaining / 60]];
        remaining %= 60;
        max--;
    }
    if (max > 0 && showSeconds)
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u sec", "time string"), remaining]];
    
    return [timeArray componentsJoinedByString: @" "];
}

- (NSComparisonResult) compareFinder: (NSString *) string
{
    const NSInteger comparisonOptions = NSCaseInsensitiveSearch | NSNumericSearch | NSWidthInsensitiveSearch | NSForcedOrderingSearch;
    return [self compare: string options: comparisonOptions range: NSMakeRange(0, [self length]) locale: [NSLocale currentLocale]];
}

- (NSComparisonResult) compareNumeric: (NSString *) string
{
    const NSInteger comparisonOptions = NSNumericSearch | NSForcedOrderingSearch;
    return [self compare: string options: comparisonOptions range: NSMakeRange(0, [self length]) locale: [NSLocale currentLocale]];
}

@end
