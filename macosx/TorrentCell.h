/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

@class CTGradient;

@interface TorrentCell : NSActionCell
{
    NSUserDefaults * fDefaults;
    NSImage * fErrorImage;
    
    NSMutableDictionary * fTitleAttributes, * fStatusAttributes;
    
    BOOL fTracking, fMouseDownControlButton, fMouseDownRevealButton, fMouseDownActionButton,
            fHoverControl, fHoverReveal, fHoverAction, fHoverActionIcon;
    
    NSColor * fBarOverlayColor;
    CTGradient * fWhiteGradient, * fGrayGradient, * fLightGrayGradient, * fBlueGradient, * fDarkBlueGradient, 
                * fGreenGradient, * fLightGreenGradient, * fDarkGreenGradient, * fYellowGradient, * fRedGradient,
                * fTransparentGradient;
    
    NSColor * fGrayColor, * fBlueColor, * fBlue1Color, * fBlue2Color, * fBlue3Color, * fBlue4Color, * fOrangeColor;
    
    NSBitmapImageRep * fBitmap;
}

- (NSRect) iconRectForBounds: (NSRect) bounds;
- (NSRect) titleRectForBounds: (NSRect) bounds;
- (NSRect) progressRectForBounds: (NSRect) bounds;
- (NSRect) barRectForBounds: (NSRect) bounds;
- (NSRect) statusRectForBounds: (NSRect) bounds;
- (NSRect) minimalStatusRectForBounds: (NSRect) bounds;

- (NSRect) controlButtonRectForBounds: (NSRect) bounds;
- (NSRect) revealButtonRectForBounds: (NSRect) bounds;
- (NSRect) actionButtonRectForBounds: (NSRect) bounds;

- (void) addTrackingAreasForView: (NSView *) controlView inRect: (NSRect) cellFrame withUserInfo: (NSDictionary *) userInfo
            mouseLocation: (NSPoint) mouseLocation;
- (void) setControlHover: (BOOL) hover;
- (void) setRevealHover: (BOOL) hover;
- (void) setActionHover: (BOOL) hover;
- (void) setActionIconHover: (BOOL) hover;
- (void) setActionPushed: (BOOL) pushed;

@end
