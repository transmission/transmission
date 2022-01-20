// This file Copyright (c) 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class Torrent;

@interface FileOutlineView : NSOutlineView
{
    NSInteger fMouseRow;
}

- (NSRect)iconRectForRow:(int)row;

@property(nonatomic, readonly) NSInteger hoveredRow;

@end
