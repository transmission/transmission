// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ProgressGradients.h"
#import "NSApplicationAdditions.h"

@implementation ProgressGradients

+ (NSGradient*)progressWhiteGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.1 green:0.1 blue:0.1];
    }
    else
    {
        return [[self class] progressGradientForRed:0.95 green:0.95 blue:0.95];
    }
}

+ (NSGradient*)progressGrayGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.35 green:0.35 blue:0.35];
    }
    else
    {
        return [[self class] progressGradientForRed:0.7 green:0.7 blue:0.7];
    }
}

+ (NSGradient*)progressLightGrayGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.2 green:0.2 blue:0.2];
    }
    else
    {
        return [[self class] progressGradientForRed:0.87 green:0.87 blue:0.87];
    }
}

+ (NSGradient*)progressBlueGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.35 * 2.0 / 3.0 green:0.67 * 2.0 / 3.0 blue:0.98 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.35 green:0.67 blue:0.98];
    }
}

+ (NSGradient*)progressDarkBlueGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.616 * 2.0 / 3.0 green:0.722 * 2.0 / 3.0 blue:0.776 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.616 green:0.722 blue:0.776];
    }
}

+ (NSGradient*)progressGreenGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.44 * 2.0 / 3.0 green:0.89 * 2.0 / 3.0 blue:0.40 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.44 green:0.89 blue:0.40];
    }
}

+ (NSGradient*)progressLightGreenGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.62 * 3.0 / 4.0 green:0.99 * 3.0 / 4.0 blue:0.58 * 3.0 / 4.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.62 green:0.99 blue:0.58];
    }
}

+ (NSGradient*)progressDarkGreenGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.627 * 2.0 / 3.0 green:0.714 * 2.0 / 3.0 blue:0.639 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.627 green:0.714 blue:0.639];
    }
}

+ (NSGradient*)progressRedGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.902 * 2.0 / 3.0 green:0.439 * 2.0 / 3.0 blue:0.451 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.902 green:0.439 blue:0.451];
    }
}

+ (NSGradient*)progressYellowGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.933 * 0.8 green:0.890 * 0.8 blue:0.243 * 0.8];
    }
    else
    {
        return [[self class] progressGradientForRed:0.933 green:0.890 blue:0.243];
    }
}

#pragma mark - Private

+ (NSGradient*)progressGradientForRed:(CGFloat)redComponent green:(CGFloat)greenComponent blue:(CGFloat)blueComponent
{
    CGFloat const alpha = [NSUserDefaults.standardUserDefaults boolForKey:@"SmallView"] ? 0.27 : 1.0;

    NSColor* baseColor = [NSColor colorWithCalibratedRed:redComponent green:greenComponent blue:blueComponent alpha:alpha];

    NSColor* color2 = [NSColor colorWithCalibratedRed:redComponent * 0.95 green:greenComponent * 0.95 blue:blueComponent * 0.95
                                                alpha:alpha];

    NSColor* color3 = [NSColor colorWithCalibratedRed:redComponent * 0.85 green:greenComponent * 0.85 blue:blueComponent * 0.85
                                                alpha:alpha];

    return [[NSGradient alloc] initWithColorsAndLocations:baseColor, 0.0, color2, 0.5, color3, 0.5, baseColor, 1.0, nil];
}

@end

static NSArray<NSNumber*>* kLocations = @[@(0.0), @(0.5), @(0.5), @(1.0)];

@implementation ModernProgressGradients

#pragma mark - Locations
+ (NSArray<NSNumber*>*)locations
{
    return kLocations;
}

#pragma mark - Gradients
+ (NSArray<NSColor*>*)progressWhiteGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.1 green:0.1 blue:0.1];
    }
    else
    {
        return [[self class] progressGradientForRed:0.95 green:0.95 blue:0.95];
    }
}

+ (NSArray<NSColor*>*)progressGrayGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.35 green:0.35 blue:0.35];
    }
    else
    {
        return [[self class] progressGradientForRed:0.7 green:0.7 blue:0.7];
    }
}

+ (NSArray<NSColor*>*)progressLightGrayGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.2 green:0.2 blue:0.2];
    }
    else
    {
        return [[self class] progressGradientForRed:0.87 green:0.87 blue:0.87];
    }
}

+ (NSArray<NSColor*>*)progressBlueGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.35 * 2.0 / 3.0 green:0.67 * 2.0 / 3.0 blue:0.98 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.35 green:0.67 blue:0.98];
    }
}

+ (NSArray<NSColor*>*)progressDarkBlueGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.616 * 2.0 / 3.0 green:0.722 * 2.0 / 3.0 blue:0.776 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.616 green:0.722 blue:0.776];
    }
}

+ (NSArray<NSColor*>*)progressGreenGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.44 * 2.0 / 3.0 green:0.89 * 2.0 / 3.0 blue:0.40 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.44 green:0.89 blue:0.40];
    }
}

+ (NSArray<NSColor*>*)progressLightGreenGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.62 * 3.0 / 4.0 green:0.99 * 3.0 / 4.0 blue:0.58 * 3.0 / 4.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.62 green:0.99 blue:0.58];
    }
}

+ (NSArray<NSColor*>*)progressDarkGreenGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.627 * 2.0 / 3.0 green:0.714 * 2.0 / 3.0 blue:0.639 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.627 green:0.714 blue:0.639];
    }
}

+ (NSArray<NSColor*>*)progressRedGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.902 * 2.0 / 3.0 green:0.439 * 2.0 / 3.0 blue:0.451 * 2.0 / 3.0];
    }
    else
    {
        return [[self class] progressGradientForRed:0.902 green:0.439 blue:0.451];
    }
}

+ (NSArray<NSColor*>*)progressYellowGradient
{
    if (NSApp.darkMode)
    {
        return [[self class] progressGradientForRed:0.933 * 0.8 green:0.890 * 0.8 blue:0.243 * 0.8];
    }
    else
    {
        return [[self class] progressGradientForRed:0.933 green:0.890 blue:0.243];
    }
}

#pragma mark - Private
+ (NSArray<NSColor*>*)progressGradientForRed:(CGFloat)redComponent green:(CGFloat)greenComponent blue:(CGFloat)blueComponent
{
    CGFloat const alpha = [NSUserDefaults.standardUserDefaults boolForKey:@"SmallView"] ? 0.27 : 1.0;
    NSColor* baseColor = [NSColor colorWithCalibratedRed:redComponent green:greenComponent blue:blueComponent alpha:alpha];

    NSColor* color2 = [NSColor colorWithCalibratedRed:redComponent * 0.95 green:greenComponent * 0.95 blue:blueComponent * 0.95
                                                alpha:alpha];

    NSColor* color3 = [NSColor colorWithCalibratedRed:redComponent * 0.85 green:greenComponent * 0.85 blue:blueComponent * 0.85
                                                alpha:alpha];

    return @[baseColor, color2, color3, baseColor];
}

@end

@implementation NSColor (KVC_CGColor)
- (id)cgColorKVC {
    return (id)self.CGColor;
}
@end
