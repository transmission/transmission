/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2011 Transmission authors and contributors
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

+ (NSString *) stringForFileSize: (uint64_t) size showUnitUnless: (NSString *) notAllowedUnit
    unitsUsed: (NSString **) unitUsed;

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

+ (NSString *) formattedUInteger: (NSUInteger) value
{
    NSNumberFormatter * numberFormatter = [[[NSNumberFormatter alloc] init] autorelease];
    [numberFormatter setNumberStyle: NSNumberFormatterDecimalStyle];
    [numberFormatter setMaximumFractionDigits: 0];
    
    return [numberFormatter stringFromNumber: [NSNumber numberWithUnsignedInteger: value]];
}

+ (NSString *) stringForFileSize: (uint64_t) size
{
    return [self stringForFileSize: size showUnitUnless: nil unitsUsed: nil];
}

+ (NSString *) stringForFilePartialSize: (uint64_t) partialSize fullSize: (uint64_t) fullSize
{
    NSString * units;
    NSString * fullString = [self stringForFileSize: fullSize showUnitUnless: nil unitsUsed: &units];
    NSString * partialString = [self stringForFileSize: partialSize showUnitUnless: units unitsUsed: nil];
    
    return [NSString stringWithFormat: NSLocalizedString(@"%@ of %@", "file size string"), partialString, fullString];
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
    
    NSMutableArray * timeArray = [NSMutableArray arrayWithCapacity: MIN(max, 5)];
    NSUInteger remaining = seconds; //causes problems for some users when it's a uint64_t
    
    if (seconds >= 31557600) //official amount of seconds in one year
    {
        const NSUInteger years = remaining / 31557600;
        if (years == 1)
            [timeArray addObject: NSLocalizedString(@"1 year", "time string")];
        else
            [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u years", "time string"), years]];
        remaining %= 31557600;
        --max;
    }
    if (max > 0 && seconds >= (24 * 60 * 60))
    {
        const NSUInteger days = remaining / (24 * 60 * 60);
        if (days == 1)
            [timeArray addObject: NSLocalizedString(@"1 day", "time string")];
        else
            [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u days", "time string"), days]];
        remaining %= (24 * 60 * 60);
        --max;
    }
    if (max > 0 && seconds >= (60 * 60))
    {
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u hr", "time string"), remaining / (60 * 60)]];
        remaining %= (60 * 60);
        --max;
    }
    if (max > 0 && (!showSeconds || seconds >= 60))
    {
        [timeArray addObject: [NSString stringWithFormat: NSLocalizedString(@"%u min", "time string"), remaining / 60]];
        remaining %= 60;
        --max;
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

- (NSArray *) betterComponentsSeparatedByCharactersInSet: (NSCharacterSet *) separator
{
    NSMutableArray * components = [NSMutableArray array];
    
    for (NSUInteger i = 0; i < [self length];)
    {
        const NSRange range = [self rangeOfCharacterFromSet: separator options: 0 range: NSMakeRange(i, [self length]-i)];
        
        if (range.location != i)
        {
            NSUInteger length;
            if (range.location == NSNotFound)
                length = [self length] - i;
            else
                length = range.location - i;
            [components addObject: [self substringWithRange: NSMakeRange(i, length)]];
            
            if (range.location == NSNotFound)
                break;
            i += length + range.length;
        }
    }
    
    return components;
}

@end

@implementation NSString (Private)

+ (NSString *) stringForFileSize: (uint64_t) size showUnitUnless: (NSString *) notAllowedUnit
    unitsUsed: (NSString **) unitUsed
{
    const float baseFloat = [NSApp isOnSnowLeopardOrBetter] ? 1000.0 : 1024.0;
    const NSUInteger baseInt = [NSApp isOnSnowLeopardOrBetter] ? 1000 : 1024;
    
    double convertedSize;
    NSString * unit;
    NSUInteger decimals;
    if (size < pow(baseInt, 2))
    {
        convertedSize = size / baseFloat;
        unit = NSLocalizedString(@"KB", "File size - kilobytes");
        decimals = convertedSize >= 10.0 ? 0 : 1;
    }
    else if (size < pow(baseInt, 3))
    {
        convertedSize = size / powf(baseFloat, 2);
        unit = NSLocalizedString(@"MB", "File size - megabytes");
        decimals = 1;
    }
    else if (size < pow(baseInt, 4))
    {
        convertedSize = size / powf(baseFloat, 3);
        unit = NSLocalizedString(@"GB", "File size - gigabytes");
        decimals = 2;
    }
    else
    {
        convertedSize = size / powf(baseFloat, 4);
        unit = NSLocalizedString(@"TB", "File size - terabytes");
        decimals = 3; //guessing on this one
    }
    
    //match Finder's behavior
    NSNumberFormatter * numberFormatter = [[NSNumberFormatter alloc] init];
    [numberFormatter setNumberStyle: NSNumberFormatterDecimalStyle];
    [numberFormatter setMinimumFractionDigits: 0];
    [numberFormatter setMaximumFractionDigits: decimals];
    
    NSString * fileSizeString = [numberFormatter stringFromNumber: [NSNumber numberWithFloat: convertedSize]];
    [numberFormatter release];
    
    if (!notAllowedUnit || ![unit isEqualToString: notAllowedUnit])
        fileSizeString = [fileSizeString stringByAppendingFormat: @" %@", unit];
    
    if (unitUsed)
        *unitUsed = unit;
    
    return fileSizeString;
}

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
