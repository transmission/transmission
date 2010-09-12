/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2010 Transmission authors and contributors
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
#import "NSApplicationAdditions.h"

#import <transmission.h>
#import "utils.h"

@interface NSString (Private)

+ (NSString *) stringForSpeed: (CGFloat) speed kb: (NSString *) kb mb: (NSString *) mb gb: (NSString *) gb;

@end

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
    const CGFloat baseFloat = [NSApp isOnSnowLeopardOrBetter] ? 1000.0 : 1024.0;
    const NSInteger baseInt = [NSApp isOnSnowLeopardOrBetter] ? 1000 : 1024;
    
    if (size < baseInt)
    {
        if (size != 1)
            return [NSString stringWithFormat: @"%lld %@", size, NSLocalizedString(@"bytes", "File size - bytes")];
        else
            return NSLocalizedString(@"1 byte", "File size - bytes");
    }

    CGFloat convertedSize;
    NSString * unit;
    if (size < pow(baseInt, 2))
    {
        convertedSize = size / baseFloat;
        unit = NSLocalizedString(@"KB", "File size - kilobytes");
    }
    else if (size < pow(baseInt, 3))
    {
        convertedSize = size / pow(baseFloat, 2);
        unit = NSLocalizedString(@"MB", "File size - megabytes");
    }
    else if (size < pow(baseInt, 4))
    {
        convertedSize = size / pow(baseFloat, 3);
        unit = NSLocalizedString(@"GB", "File size - gigabytes");
    }
    else
    {
        convertedSize = size / pow(baseFloat, 4);
        unit = NSLocalizedString(@"TB", "File size - terabytes");
    }
    
    //attempt to have minimum of 3 digits with at least 1 decimal
    return convertedSize <= 9.995 ? [NSString localizedStringWithFormat: @"%.2f %@", convertedSize, unit]
                                : [NSString localizedStringWithFormat: @"%.1f %@", convertedSize, unit];
}

+ (NSString *) stringForSpeed: (CGFloat) speed
{
    return [self stringForSpeed: speed
                kb: NSLocalizedString(@"KB/s", "Transfer speed (kilobytes per second)")
                mb: NSLocalizedString(@"MB/s", "Transfer speed (megabytes per second)")
                gb: NSLocalizedString(@"GB/s", "Transfer speed (gigabytes per second)")];
}

+ (NSString *) stringForSpeedAbbrev: (CGFloat) speed
{
    return [self stringForSpeed: speed kb: @"K" mb: @"M" gb: @"G"];
}

+ (NSString *) stringForRatio: (CGFloat) ratio
{
    //N/A is different than libtransmission's
    if ((int)ratio == TR_RATIO_NA)
        return NSLocalizedString(@"N/A", "No Ratio");
    else if ((int)ratio == TR_RATIO_INF)
        return [NSString stringWithUTF8String: "\xE2\x88\x9E"];
    else
    {
        if (ratio < 10.0)
            return [NSString localizedStringWithFormat: @"%.2f", tr_truncd(ratio, 2)];
        else if (ratio < 100.0)
            return [NSString localizedStringWithFormat: @"%.1f", tr_truncd(ratio, 1)];
        else
            return [NSString localizedStringWithFormat: @"%.0f", tr_truncd(ratio, 0)];
    }
}

+ (NSString *) percentString: (CGFloat) progress longDecimals: (BOOL) longDecimals
{
    if (progress >= 1.0)
        return @"100%";
    else if (longDecimals)
        return [NSString localizedStringWithFormat: @"%.2f%%", tr_truncd(progress * 100.0, 2)];
    else
        return [NSString localizedStringWithFormat: @"%.1f%%", tr_truncd(progress * 100.0, 1)];
}

+ (NSString *) timeString: (uint64_t) seconds showSeconds: (BOOL) showSeconds
{
    return [NSString timeString: seconds showSeconds: showSeconds maxFields: NSUIntegerMax];
}

+ (NSString *) timeString: (uint64_t) seconds showSeconds: (BOOL) showSeconds maxFields: (NSUInteger) max
{
    NSAssert(max > 0, @"Cannot generate a time string with no fields");
    
    NSMutableArray * timeArray = [NSMutableArray arrayWithCapacity: MIN(max, 4)];
    NSUInteger remaining = seconds; //causes problems for some users when it's a uint64_t
    
    if (seconds >= (24 * 60 * 60))
    {
        const NSUInteger days = remaining / (24 * 60 * 60);
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

//also used in InfoWindow.xib and MessageWindow.xib
- (NSComparisonResult) compareFinder: (NSString *) string
{
    if ([NSApp isOnSnowLeopardOrBetter])
        return [self localizedStandardCompare: string];
    else
    {
        const NSStringCompareOptions comparisonOptions = NSCaseInsensitiveSearch | NSNumericSearch | NSWidthInsensitiveSearch
                                                            | NSForcedOrderingSearch;
        return [self compare: string options: comparisonOptions range: NSMakeRange(0, [self length]) locale: [NSLocale currentLocale]];
    }
}

- (NSComparisonResult) compareNumeric: (NSString *) string
{
    const NSStringCompareOptions comparisonOptions = NSNumericSearch | NSForcedOrderingSearch;
    return [self compare: string options: comparisonOptions range: NSMakeRange(0, [self length]) locale: [NSLocale currentLocale]];
}

@end

@implementation NSString (Private)

+ (NSString *) stringForSpeed: (CGFloat) speed kb: (NSString *) kb mb: (NSString *) mb gb: (NSString *) gb
{
    const CGFloat baseFloat = [NSApp isOnSnowLeopardOrBetter] ? 1000.0 : 1024.0;
    
    if (speed <= 999.95) //0.0 KB/s to 999.9 KB/s
        return [NSString localizedStringWithFormat: @"%.1f %@", speed, kb];
    
    speed /= baseFloat;
    
    if (speed <= 99.995) //1.00 MB/s to 99.99 MB/s
        return [NSString localizedStringWithFormat: @"%.2f %@", speed, mb];
    else if (speed <= 999.95) //100.0 MB/s to 999.9 MB/s
        return [NSString localizedStringWithFormat: @"%.1f %@", speed, mb];
    else //insane speeds
        return [NSString localizedStringWithFormat: @"%.2f %@", (speed / baseFloat), gb];
}

@end
