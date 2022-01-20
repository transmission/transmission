// This file Copyright © 2006-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface TorrentCell : NSActionCell
{
    NSUserDefaults* fDefaults;

    NSMutableDictionary* fTitleAttributes;
    NSMutableDictionary* fStatusAttributes;

    BOOL fTracking;
    BOOL fMouseDownControlButton;
    BOOL fMouseDownRevealButton;
    BOOL fMouseDownActionButton;
    BOOL fHover;
    BOOL fHoverControl;
    BOOL fHoverReveal;
    BOOL fHoverAction;

    NSColor* fBarBorderColor;
    NSColor* fBluePieceColor;
    NSColor* fBarMinimalBorderColor;
}

- (NSRect)iconRectForBounds:(NSRect)bounds;

- (void)addTrackingAreasForView:(NSView*)controlView
                         inRect:(NSRect)cellFrame
                   withUserInfo:(NSDictionary*)userInfo
                  mouseLocation:(NSPoint)mouseLocation;
- (void)setHover:(BOOL)hover;
- (void)setControlHover:(BOOL)hover;
- (void)setRevealHover:(BOOL)hover;
- (void)setActionHover:(BOOL)hover;
- (void)setActionPushed:(BOOL)pushed;

@end
