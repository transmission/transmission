/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2009 Transmission authors and contributors
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

#import "ProgressGradients.h"

@implementation ProgressGradients

+ (NSGradient *) progressGradientForRed: (CGFloat) redComponent green: (CGFloat) greenComponent blue: (CGFloat) blueComponent
{
    NSColor * baseColor = [NSColor colorWithCalibratedRed: redComponent green: greenComponent blue: blueComponent alpha: 1.0];
    
    NSColor * color2 = [NSColor colorWithCalibratedRed: redComponent * 0.95 green: greenComponent * 0.95 blue: blueComponent * 0.95
                        alpha: 1.0];
    
    NSColor * color3 = [NSColor colorWithCalibratedRed: redComponent * 0.85 green: greenComponent * 0.85 blue: blueComponent * 0.85
                        alpha: 1.0];
    
    NSGradient * progressGradient = [[NSGradient alloc] initWithColorsAndLocations: baseColor, 0.0, color2, 0.5, color3, 0.5,
                                        baseColor, 1.0, nil];
    return [progressGradient autorelease];
}

NSGradient * fProgressWhiteGradient = nil;
+ (NSGradient *) progressWhiteGradient
{
    if (!fProgressWhiteGradient)
        fProgressWhiteGradient = [[[self class] progressGradientForRed: 0.95f green: 0.95f blue: 0.95f] retain];
    return fProgressWhiteGradient;
}

NSGradient * fProgressGrayGradient = nil;
+ (NSGradient *) progressGrayGradient
{
    if (!fProgressGrayGradient)
        fProgressGrayGradient = [[[self class] progressGradientForRed: 0.7f green: 0.7f blue: 0.7f] retain];
    return fProgressGrayGradient;
}

NSGradient * fProgressLightGrayGradient = nil;
+ (NSGradient *) progressLightGrayGradient
{
    if (!fProgressLightGrayGradient)
        fProgressLightGrayGradient = [[[self class] progressGradientForRed: 0.87f green: 0.87f blue: 0.87f] retain];
    return fProgressLightGrayGradient;
}

NSGradient * fProgressBlueGradient = nil;
+ (NSGradient *) progressBlueGradient
{
    if (!fProgressBlueGradient)
        fProgressBlueGradient = [[[self class] progressGradientForRed: 0.35f green: 0.67f blue: 0.98f] retain];
    return fProgressBlueGradient;
}

NSGradient * fProgressDarkBlueGradient = nil;
+ (NSGradient *) progressDarkBlueGradient
{
    if (!fProgressDarkBlueGradient)
        fProgressDarkBlueGradient = [[[self class] progressGradientForRed: 0.616f green: 0.722f blue: 0.776f] retain];
    return fProgressDarkBlueGradient;
}

NSGradient * fProgressGreenGradient = nil;
+ (NSGradient *) progressGreenGradient
{
    if (!fProgressGreenGradient)
        fProgressGreenGradient = [[[self class] progressGradientForRed: 0.44f green: 0.89f blue: 0.40f] retain];
    return fProgressGreenGradient;
}

NSGradient * fProgressLightGreenGradient = nil;
+ (NSGradient *) progressLightGreenGradient
{
    if (!fProgressLightGreenGradient)
        fProgressLightGreenGradient = [[[self class] progressGradientForRed: 0.62f green: 0.99f blue: 0.58f] retain];
    return fProgressLightGreenGradient;
}

NSGradient * fProgressDarkGreenGradient = nil;
+ (NSGradient *) progressDarkGreenGradient
{
    if (!fProgressDarkGreenGradient)
        fProgressDarkGreenGradient = [[[self class] progressGradientForRed: 0.627f green: 0.714f blue: 0.639f] retain];
    return fProgressDarkGreenGradient;
}

NSGradient * fProgressRedGradient = nil;
+ (NSGradient *) progressRedGradient
{
    if (!fProgressRedGradient)
        fProgressRedGradient = [[[self class] progressGradientForRed: 0.902f green: 0.439f blue: 0.451f] retain];
    return fProgressRedGradient;
}

NSGradient * fProgressYellowGradient = nil;
+ (NSGradient *) progressYellowGradient
{
    if (!fProgressYellowGradient)
        fProgressYellowGradient = [[[self class] progressGradientForRed: 0.933f green: 0.890f blue: 0.243f] retain];
    return fProgressYellowGradient;
}

@end
