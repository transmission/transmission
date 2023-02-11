// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface TorrentCell : NSActionCell

@property(nonatomic) BOOL hover;
@property(nonatomic) BOOL hoverControl;
@property(nonatomic) BOOL hoverReveal;
@property(nonatomic) BOOL hoverAction;

- (NSRect)iconRectForBounds:(NSRect)bounds;
- (NSRect)actionRectForBounds:(NSRect)bounds;

- (void)addTrackingAreasForView:(NSView*)controlView
                         inRect:(NSRect)cellFrame
                   withUserInfo:(NSDictionary*)userInfo
                  mouseLocation:(NSPoint)mouseLocation;

@end
