/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2008 Transmission authors and contributors
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
#import "CTGradient.h"

@implementation ProgressGradients

+ (CTGradient *) progressGradientForRed: (CGFloat) redComponent green: (CGFloat) greenComponent blue: (CGFloat) blueComponent
{
    CTGradientElement color1;
    color1.red = redComponent;
    color1.green = greenComponent;
    color1.blue = blueComponent;
    color1.alpha = 1.0f;
    color1.position = 0.0f;
    
    CTGradientElement color2;
    color2.red = redComponent * 0.95f;
    color2.green = greenComponent * 0.95f;
    color2.blue = blueComponent * 0.95f;
    color2.alpha = 1.0f;
    color2.position = 0.5f;
    
    CTGradientElement color3;
    color3.red = redComponent * 0.85f;
    color3.green = greenComponent * 0.85f;
    color3.blue = blueComponent * 0.85f;
    color3.alpha = 1.0f;
    color3.position = 0.5f;
    
    CTGradientElement color4;
    color4.red = redComponent;
    color4.green = greenComponent;
    color4.blue = blueComponent;
    color4.alpha = 1.0f;
    color4.position = 1.0f;
    
    CTGradient * newInstance = [[[self class] alloc] init];
    [newInstance addElement: &color1];
    [newInstance addElement: &color2];
    [newInstance addElement: &color3];
    [newInstance addElement: &color4];
    
    return [newInstance autorelease];
}

CTGradient * fProgressWhiteGradient = nil;
+ (CTGradient *) progressWhiteGradient
{
    if (!fProgressWhiteGradient)
        fProgressWhiteGradient = [[[self class] progressGradientForRed: 0.95f green: 0.95f blue: 0.95f] retain];
    return fProgressWhiteGradient;
}

CTGradient * fProgressGrayGradient = nil;
+ (CTGradient *) progressGrayGradient
{
    if (!fProgressGrayGradient)
        fProgressGrayGradient = [[[self class] progressGradientForRed: 0.7f green: 0.7f blue: 0.7f] retain];
    return fProgressGrayGradient;
}

CTGradient * fProgressLightGrayGradient = nil;
+ (CTGradient *) progressLightGrayGradient
{
    if (!fProgressLightGrayGradient)
        fProgressLightGrayGradient = [[[self class] progressGradientForRed: 0.87f green: 0.87f blue: 0.87f] retain];
    return fProgressLightGrayGradient;
}

CTGradient * fProgressBlueGradient = nil;
+ (CTGradient *) progressBlueGradient
{
    if (!fProgressBlueGradient)
        fProgressBlueGradient = [[[self class] progressGradientForRed: 0.35f green: 0.67f blue: 0.98f] retain];
    return fProgressBlueGradient;
}

CTGradient * fProgressDarkBlueGradient = nil;
+ (CTGradient *) progressDarkBlueGradient
{
    if (!fProgressDarkBlueGradient)
        fProgressDarkBlueGradient = [[[self class] progressGradientForRed: 0.616f green: 0.722f blue: 0.776f] retain];
    return fProgressDarkBlueGradient;
}

CTGradient * fProgressGreenGradient = nil;
+ (CTGradient *) progressGreenGradient
{
    if (!fProgressGreenGradient)
        fProgressGreenGradient = [[[self class] progressGradientForRed: 0.44f green: 0.89f blue: 0.40f] retain];
    return fProgressGreenGradient;
}

CTGradient * fProgressLightGreenGradient = nil;
+ (CTGradient *) progressLightGreenGradient
{
    if (!fProgressLightGreenGradient)
        fProgressLightGreenGradient = [[[self class] progressGradientForRed: 0.62f green: 0.99f blue: 0.58f] retain];
    return fProgressLightGreenGradient;
}

CTGradient * fProgressDarkGreenGradient = nil;
+ (CTGradient *) progressDarkGreenGradient
{
    if (!fProgressDarkGreenGradient)
        fProgressDarkGreenGradient = [[[self class] progressGradientForRed: 0.627f green: 0.714f blue: 0.639f] retain];
    return fProgressDarkGreenGradient;
}

CTGradient * fProgressRedGradient = nil;
+ (CTGradient *) progressRedGradient
{
    if (!fProgressRedGradient)
        fProgressRedGradient = [[[self class] progressGradientForRed: 0.902f green: 0.439f blue: 0.451f] retain];
    return fProgressRedGradient;
}

CTGradient * fProgressYellowGradient = nil;
+ (CTGradient *) progressYellowGradient
{
    if (!fProgressYellowGradient)
        fProgressYellowGradient = [[[self class] progressGradientForRed: 0.933f green: 0.890f blue: 0.243f] retain];
    return fProgressYellowGradient;
}

@end
