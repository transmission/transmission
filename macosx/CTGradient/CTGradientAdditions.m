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

#import "CTGradientAdditions.h"

@implementation CTGradient (ProgressBar)

+ (CTGradient *) progressGradientForRed: (float) redComponent green: (float) greenComponent blue: (float) blueComponent
{
    CTGradientElement color1;
    color1.red = redComponent * 0.95;
    color1.green = greenComponent * 0.95;
    color1.blue = blueComponent * 0.95;
    color1.alpha = 1.0;
    color1.position = 0.0;
    
    CTGradientElement color2;
    color2.red = redComponent;
    color2.green = greenComponent;
    color2.blue = blueComponent;
    color2.alpha = 1.0;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = redComponent * 0.8;
    color3.green = greenComponent * 0.8;
    color3.blue = blueComponent * 0.8;
    color3.alpha = 1.0;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = redComponent;
    color4.green = greenComponent;
    color4.blue = blueComponent;
    color4.alpha = 1.0;
    color4.position = 1.0;
    
    CTGradient * newInstance = [[[self class] alloc] init];
    [newInstance addElement: &color1];
    [newInstance addElement: &color2];
    [newInstance addElement: &color3];
    [newInstance addElement: &color4];
    
    return [newInstance autorelease];
}

+ (CTGradient *)progressWhiteGradient
{
    return [[self class] progressGradientForRed: 0.95 green: 0.95 blue: 0.95];
}

+ (CTGradient *)progressGrayGradient
{
    return [[self class] progressGradientForRed: 0.7 green: 0.7 blue: 0.7];
}

+ (CTGradient *)progressLightGrayGradient
{
    return [[self class] progressGradientForRed: 0.87 green: 0.87 blue: 0.87];
}

+ (CTGradient *)progressBlueGradient
{
    return [[self class] progressGradientForRed: 0.373 green: 0.698 blue: 0.972];
}

+ (CTGradient *)progressDarkBlueGradient
{
    return [[self class] progressGradientForRed: 0.616 green: 0.722 blue: 0.776];
}

+ (CTGradient *)progressGreenGradient
{
    return [[self class] progressGradientForRed: 0.384 green: 0.847 blue: 0.310];
}

+ (CTGradient *)progressLightGreenGradient
{
    return [[self class] progressGradientForRed: 0.780 green: 0.894 blue: 0.729];
}

+ (CTGradient *)progressDarkGreenGradient
{
    return [[self class] progressGradientForRed: 0.627 green: 0.714 blue: 0.639];
}

+ (CTGradient *)progressRedGradient
{
    return [[self class] progressGradientForRed: 0.902 green: 0.439 blue: 0.451];
}

+ (CTGradient *)progressYellowGradient
{
    return [[self class] progressGradientForRed: 0.933 green: 0.890 blue: 0.243];
}

@end
