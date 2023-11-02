// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

typedef NS_ENUM(NSInteger, AttributesStyle) {
    AttributesStyleNormal,
    AttributesStyleEmphasized,
    AttributesStyleDisabled,
};

@interface FileNameCell : NSActionCell

- (NSRect)imageRectForBounds:(NSRect)bounds;

@end
