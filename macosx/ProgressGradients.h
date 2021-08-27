/******************************************************************************
 * Copyright (c) 2007-2012 Transmission authors and contributors
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

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

@interface ProgressGradients : NSObject

@property(nonatomic, class, readonly) NSGradient* progressWhiteGradient;
@property(nonatomic, class, readonly) NSGradient* progressGrayGradient;
@property(nonatomic, class, readonly) NSGradient* progressLightGrayGradient;
@property(nonatomic, class, readonly) NSGradient* progressBlueGradient;
@property(nonatomic, class, readonly) NSGradient* progressDarkBlueGradient;
@property(nonatomic, class, readonly) NSGradient* progressGreenGradient;
@property(nonatomic, class, readonly) NSGradient* progressLightGreenGradient;
@property(nonatomic, class, readonly) NSGradient* progressDarkGreenGradient;
@property(nonatomic, class, readonly) NSGradient* progressRedGradient;
@property(nonatomic, class, readonly) NSGradient* progressYellowGradient;

@end
