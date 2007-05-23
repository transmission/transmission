/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007 Transmission authors and contributors
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

@implementation CTGradient (ActionBar)

+ (CTGradient *)actionNormalGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];

    CTGradientElement color1;
    color1.red = color1.green = color1.blue  = 0.9;
    color1.alpha = 1.00;
    color1.position = 0;

    CTGradientElement color2;
    color2.red = color2.green = color2.blue  = 0.9;
    color2.alpha = 1.00;
    color2.position = 0.5;

    CTGradientElement color3;
    color3.red = color3.green = color3.blue  = 0.95;
    color3.alpha = 1.00;
    color3.position = 0.5;

    CTGradientElement color4;
    color4.red = color4.green = color4.blue  = 1.0;
    color4.alpha = 1.00;
    color4.position = 1;

    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];

    return [newInstance autorelease];
}

+ (CTGradient *)actionPressedGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];

    CTGradientElement color1;
    color1.red = color1.green = color1.blue  = 0.80;
    color1.alpha = 1.00;
    color1.position = 0;

    CTGradientElement color2;
    color2.red = color2.green = color2.blue  = 0.64;
    color2.alpha = 1.00;
    color2.position = 11.5/23;

    CTGradientElement color3;
    color3.red = color3.green = color3.blue  = 0.80;
    color3.alpha = 1.00;
    color3.position = 11.5/23;

    CTGradientElement color4;
    color4.red = color4.green = color4.blue  = 0.77;
    color4.alpha = 1.00;
    color4.position = 1;

    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];

    return [newInstance autorelease];
}

@end

@implementation CTGradient (ProgressBar)

+ (CTGradient *)progressWhiteGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];
    
    CTGradientElement color1;
    color1.red = color1.green = color1.blue  = 0.95;
    color1.alpha = 1.00;
    color1.position = 0;
    
    CTGradientElement color2;
    color2.red = color2.green = color2.blue  = 0.8;
    color2.alpha = 1.00;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = color3.green = color3.blue  = 0.95;
    color3.alpha = 1.00;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = color4.green = color4.blue  = 0.95;
    color4.alpha = 1.00;
    color4.position = 1;
    
    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];
    
    return [newInstance autorelease];
}

+ (CTGradient *)progressGreyGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];
    
    CTGradientElement color1;
    color1.red = color1.green = color1.blue  = 0.7;
    color1.alpha = 1.00;
    color1.position = 0;
    
    CTGradientElement color2;
    color2.red = color2.green = color2.blue  = 0.6;
    color2.alpha = 1.00;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = color3.green = color3.blue  = 0.7;
    color3.alpha = 1.00;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = color4.green = color4.blue  = 0.7;
    color4.alpha = 1.00;
    color4.position = 1;
    
    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];
    
    return [newInstance autorelease];
}

+ (CTGradient *)progressBlueGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];
    
    CTGradientElement color1;
    color1.red = 0.416;
    color1.green = 0.788;
    color1.blue  = 0.97;
    color1.alpha = 1.00;
    color1.position = 0;
    
    CTGradientElement color2;
    color2.red = 0.274;
    color2.green = 0.52;
    color2.blue  = 0.94;
    color2.alpha = 1.00;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = 0.372;
    color3.green = 0.635;
    color3.blue  = 0.98;
    color3.alpha = 1.00;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = 0.396;
    color4.green = 0.66;
    color4.blue  = 1.00;
    color4.alpha = 1.00;
    color4.position = 1;
    
    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];
    
    return [newInstance autorelease];
}

+ (CTGradient *)progressGreenGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];
    
    CTGradientElement color1;
    color1.red = 0.270;
    color1.green = 0.89;
    color1.blue  = 0.35;
    color1.alpha = 1.00;
    color1.position = 0;
    
    CTGradientElement color2;
    color2.red = 0.180;
    color2.green = 0.71;
    color2.blue  = 0.23;
    color2.alpha = 1.00;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = 0.420;
    color3.green = 0.86;
    color3.blue  = 0.32;
    color3.alpha = 1.00;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = 0.466;
    color4.green = 0.89;
    color4.blue  = 0.34;
    color4.alpha = 1.00;
    color4.position = 1;
    
    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];
    
    return [newInstance autorelease];
}

+ (CTGradient *)progressLightGreenGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];
    
    CTGradientElement color1;
    color1.red = 0.478;
    color1.green = 1.00;
    color1.blue  = 0.55;
    color1.alpha = 1.00;
    color1.position = 0;
    
    CTGradientElement color2;
    color2.red = 0.388;
    color2.green = 0.92;
    color2.blue  = 0.44;
    color2.alpha = 1.00;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = 0.627;
    color3.green = 1.00;
    color3.blue  = 0.53;
    color3.alpha = 1.00;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = 0.675;
    color4.green = 1.00;
    color4.blue  = 0.55;
    color4.alpha = 1.00;
    color4.position = 1;
    
    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];
    
    return [newInstance autorelease];
}

+ (CTGradient *)progressTransparentGradient
{
    CTGradient *newInstance = [[[self class] alloc] init];
    
    CTGradientElement color1;
    color1.red = color1.green = color1.blue = 1.0;
    color1.alpha = 0.65;
    color1.position = 0;
    
    CTGradientElement color2;
    color2.red = color2.green = color2.blue = 1.0;
    color2.alpha = 0.05;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = color3.green = color3.blue = 1.0;
    color3.alpha = 0.4;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = color4.green = color4.blue = 1.0;
    color4.alpha = 0.4;
    color4.position = 1;
    
    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];
    
    return [newInstance autorelease];
}

@end


@implementation CTGradient (MiddleColour)

+ (CTGradient *)gradientWithBeginningColor:(NSColor *)begin middleColor:(NSColor *)middle endingColor:(NSColor *)end
{
    CTGradient *newInstance = [[[self class] alloc] init];

    CTGradientElement color1;
    CTGradientElement color2;
    CTGradientElement color3;

    [[begin  colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color1.red
                                                                  green:&color1.green
                                                                   blue:&color1.blue
                                                                  alpha:&color1.alpha];
    [[middle colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color2.red
                                                                  green:&color2.green
                                                                   blue:&color2.blue
                                                                  alpha:&color2.alpha];  
    [[end    colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color3.red
                                                                  green:&color3.green
                                                                   blue:&color3.blue
                                                                  alpha:&color3.alpha];  
    color1.position = 0;
    color2.position = 0.5;
    color3.position = 1;

    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];

    return [newInstance autorelease];
}

+ (CTGradient *)gradientWithBeginningColor:(NSColor *)begin middleColor1:(NSColor *)middle1 middleColor2:(NSColor *)middle2 endingColor:(NSColor *)end
{
    CTGradient *newInstance = [[[self class] alloc] init];

    CTGradientElement color1;
    CTGradientElement color2;
    CTGradientElement color3;
    CTGradientElement color4;

    [[begin   colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color1.red
                                                                   green:&color1.green
                                                                    blue:&color1.blue
                                                                   alpha:&color1.alpha];
    [[middle1 colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color2.red
                                                                   green:&color2.green
                                                                    blue:&color2.blue
                                                                   alpha:&color2.alpha];  
    [[middle2 colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color3.red
                                                                   green:&color3.green
                                                                    blue:&color3.blue
                                                                   alpha:&color3.alpha];  
    [[end     colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color4.red
                                                                   green:&color4.green
                                                                    blue:&color4.blue
                                                                   alpha:&color4.alpha];  
    color1.position = 0;
    color2.position = 0.5;
    color3.position = 0.5;
    color4.position = 1;

    [newInstance addElement:&color1];
    [newInstance addElement:&color2];
    [newInstance addElement:&color3];
    [newInstance addElement:&color4];

    return [newInstance autorelease];
}

@end