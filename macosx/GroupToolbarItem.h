// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@interface GroupToolbarItem : NSToolbarItem

@property(nonatomic, copy) NSArray* identifiers;

- (void)createMenu:(NSArray*)labels;

@end
