//
//  StringAdditions.m
//  Transmission
//
//  Created by Mitchell Livingston on 1/16/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import "StringAdditions.h"
#import "Utils.h"

@implementation NSString (StringAdditions)

+ (NSString *) stringForFileSize: (uint64_t) size
{
    if (size < 1024)
        return [NSString stringWithFormat: @"%lld bytes", size];
    else if (size < 1048576)
        return [NSString stringWithFormat: @"%lld.%lld KB",
                size / 1024, ( size % 1024 ) / 103];
    else if (size < 1073741824)
        return [NSString stringWithFormat: @"%lld.%lld MB",
                size / 1048576, ( size % 1048576 ) / 104858];
    else
        return [NSString stringWithFormat: @"%lld.%lld GB",
                size / 1073741824, ( size % 1073741824 ) / 107374183];
}

+ (NSString *) stringForSpeed: (float) speed
{
    return [[self stringForSpeedAbbrev: speed]
        stringByAppendingString: @"B/s"];
}

+ (NSString *) stringForSpeedAbbrev: (float) speed
{
    if (speed < 1000)         /* 0.0 K to 999.9 K */
        return [NSString stringWithFormat: @"%.1f K", speed];
    else if (speed < 102400)  /* 0.98 M to 99.99 M */
        return [NSString stringWithFormat: @"%.2f M", speed / 1024];
    else if (speed < 1024000) /* 100.0 M to 999.9 M */
        return [NSString stringWithFormat: @"%.1f M", speed / 1024];
    else                      /* Insane speeds */
        return [NSString stringWithFormat: @"%.2f G", speed / 1048576];
}

+ (NSString *) stringForRatio: (uint64_t) down upload: (uint64_t) up
{
    if( !down && !up )
        return @"N/A";
    if( !down )
        return [NSString stringWithUTF8String: "\xE2\x88\x9E"];

    float ratio = (float) up / (float) down;
    if( ratio < 10.0 )
        return [NSString stringWithFormat: @"%.2f", ratio];
    else if( ratio < 100.0 )
        return [NSString stringWithFormat: @"%.1f", ratio];
    else
        return [NSString stringWithFormat: @"%.0f", ratio];
}

- (NSString *) stringFittingInWidth: (float) width
                    withAttributes: (NSDictionary *) attributes
{
    float w;
    int i;
    NSString * newString;

    w = [self sizeWithAttributes: attributes].width;
    if( w <= width )
        /* The whole string fits */
        return self;

    /* Approximate how many characters we'll need to drop... */
    i = [self length] * width / w - 1;

    /* ... then refine it */
    newString = [[self substringToIndex: i]
        stringByAppendingString: NS_ELLIPSIS];
    w = [newString sizeWithAttributes: attributes].width;

    if( w < width )
    {
        NSString * bakString;
        for( ;; )
        {
            bakString = newString;
            newString = [[self substringToIndex: ++i]
                stringByAppendingString: NS_ELLIPSIS];
            if( [newString sizeWithAttributes: attributes].width > width )
                return bakString;
        }

    }
    else if( w > width )
    {
        for( ;; )
        {
            newString = [[self substringToIndex: --i]
                stringByAppendingString: NS_ELLIPSIS];
            if( [newString sizeWithAttributes: attributes].width <= width )
                return newString;
        }
    }
    else
    {
        return newString;
    }
}

@end
