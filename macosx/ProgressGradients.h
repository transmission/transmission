// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

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

@interface ModernProgressGradients

@property(nonatomic, class, readonly) NSArray<NSNumber*>* locations;

@property(nonatomic, class, readonly) NSArray<NSColor*>* progressWhiteGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressGrayGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressLightGrayGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressBlueGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressDarkBlueGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressGreenGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressLightGreenGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressDarkGreenGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressRedGradient;
@property(nonatomic, class, readonly) NSArray<NSColor*>* progressYellowGradient;

@end

@interface NSColor (KVC_CGColor)
@property (readonly) id cgColorKVC;
@end
