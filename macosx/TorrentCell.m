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

#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "GroupsWindowController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "NSBezierPathAdditions.h"
#import "CTGradientAdditions.h"

#define BAR_HEIGHT 12.0

#define IMAGE_SIZE_REG 32.0
#define IMAGE_SIZE_MIN 16.0

#define NORMAL_BUTTON_WIDTH 14.0
/*#define ACTION_BUTTON_HEIGHT 14.0
#define ACTION_BUTTON_WIDTH 32.0*/
#define ACTION_BUTTON_WIDTH 16.0

//ends up being larger than font height
#define HEIGHT_TITLE 16.0
#define HEIGHT_STATUS 12.0

#define PADDING_HORIZONTAL 3.0
//#define PADDING_ABOVE_IMAGE_REG 9.0
//#define PADDING_BETWEEN_IMAGE_AND_ACTION_BUTTON 3.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 5.0
#define PADDING_BETWEEN_IMAGE_AND_BAR 7.0
#define PADDING_ABOVE_TITLE 3.0
#define PADDING_ABOVE_MIN_STATUS 4.0
#define PADDING_BETWEEN_TITLE_AND_MIN_STATUS 2.0
#define PADDING_BETWEEN_TITLE_AND_PROGRESS 1.0
#define PADDING_BETWEEN_PROGRESS_AND_BAR 2.0
#define PADDING_BETWEEN_TITLE_AND_BAR_MIN 3.0
#define PADDING_BETWEEN_BAR_AND_STATUS 2.0

#define WIDTH_GROUP 40.0
#define WIDTH_GROUP_MIN 24.0

#define MAX_PIECES 324

@interface TorrentCell (Private)

- (void) drawBar: (NSRect) barRect;
- (void) drawRegularBar: (NSRect) barRect;
- (void) drawPiecesBar: (NSRect) barRect;

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds;
- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color;
- (NSAttributedString *) attributedStatusString: (NSString *) string withColor: (NSColor *) color;

- (NSString *) buttonString;
- (NSString *) statusString;
- (NSString *) minimalStatusString;

@end

@implementation TorrentCell

//only called once and the main table is always needed, so don't worry about releasing
- (id) init
{
    if ((self = [super init]))
	{
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
        fTitleAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        [paragraphStyle release];
        
        //store box colors
        fGrayColor = [[NSColor colorWithCalibratedRed: 0.9 green: 0.9 blue: 0.9 alpha: 1.0] retain];
        fBlue1Color = [[NSColor colorWithCalibratedRed: 0.8 green: 1.0 blue: 1.0 alpha: 1.0] retain];
        fBlue2Color = [[NSColor colorWithCalibratedRed: 0.6 green: 1.0 blue: 1.0 alpha: 1.0] retain];
        fBlue3Color = [[NSColor colorWithCalibratedRed: 0.6 green: 0.8 blue: 1.0 alpha: 1.0] retain];
        fBlue4Color = [[NSColor colorWithCalibratedRed: 0.4 green: 0.6 blue: 1.0 alpha: 1.0] retain];
        fBlueColor = [[NSColor colorWithCalibratedRed: 0.0 green: 0.4 blue: 0.8 alpha: 1.0] retain];
        fOrangeColor = [[NSColor orangeColor] retain];
        
        fBarOverlayColor = [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] retain];
    }
	return self;
}

- (id) copyWithZone: (NSZone *) zone
{
    TorrentCell * copy = [super copyWithZone: zone];
    
    copy->fBitmap = nil;
    
    copy->fGrayGradient = [fGrayGradient retain];
    copy->fLightGrayGradient = [fLightGrayGradient retain];
    copy->fBlueGradient = [fBlueGradient retain];
    copy->fDarkBlueGradient = [fDarkBlueGradient retain];
    copy->fGreenGradient = [fGreenGradient retain];
    copy->fLightGreenGradient = [fLightGreenGradient retain];
    copy->fDarkGreenGradient = [fDarkGreenGradient retain];
    copy->fYellowGradient = [fYellowGradient retain];
    copy->fRedGradient = [fRedGradient retain];
    copy->fTransparentGradient = [fTransparentGradient retain];
    
    return copy;
}

- (void) dealloc
{
    [fBitmap release];
    
    [fGrayGradient release];
    [fLightGrayGradient release];
    [fBlueGradient release];
    [fDarkBlueGradient release];
    [fGreenGradient release];
    [fLightGreenGradient release];
    [fDarkGreenGradient release];
    [fYellowGradient release];
    [fRedGradient release];
    [fTransparentGradient release];
    
    [super dealloc];
}

- (NSRect) iconRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    
    float imageSize = [fDefaults boolForKey: @"SmallView"] ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG;
    
    result.origin.x += PADDING_HORIZONTAL;
    result.origin.y += floorf((result.size.height - imageSize) * 0.5);
    result.size = NSMakeSize(imageSize, imageSize);
    
    /*float imageSize;
    if ([fDefaults boolForKey: @"SmallView"])
    {
        imageSize = IMAGE_SIZE_MIN;
        result.origin.y += (result.size.height - imageSize) * 0.5;
    }
    else
    {
        imageSize = IMAGE_SIZE_REG;
        result.origin.y += PADDING_ABOVE_IMAGE_REG;
    }*/
    
    return result;
}

- (NSRect) titleRectForBounds: (NSRect) bounds
{
    return [self rectForTitleWithString: [self attributedTitleWithColor: nil]
            basedOnMinimalStatusRect: [self minimalStatusRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) minimalStatusRectForBounds: (NSRect) bounds
{
    return [self rectForMinimalStatusWithString: [self attributedStatusString: [self minimalStatusString] withColor: nil]
            inBounds: bounds];
}

- (NSRect) progressRectForBounds: (NSRect) bounds
{
    return [self rectForProgressWithString: [self attributedStatusString: [[self representedObject] progressString] withColor: nil]
                    inBounds: bounds];
}

- (NSRect) barRectForBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.size.height = BAR_HEIGHT;
    result.origin.x = (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_BAR;
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE;
    if (minimal)
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    result.size.width = round(NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL - 2.0 * (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH));
    
    return result;
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    return [self rectForStatusWithString: [self attributedStatusString: [self statusString] withColor: nil] inBounds: bounds];
}

- (NSRect) controlButtonRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - 2.0 * (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH);
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5;
    if ([fDefaults boolForKey: @"SmallView"])
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    return result;
}

- (NSRect) revealButtonRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH);
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5;
    if ([fDefaults boolForKey: @"SmallView"])
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    return result;
}

- (NSRect) actionButtonRectForBounds: (NSRect) bounds
{
    NSRect iconRect = [self iconRectForBounds: bounds];
    if ([fDefaults boolForKey: @"SmallView"])
        return iconRect;
    
    /*NSRect result = iconRect;
    result.origin.x += (iconRect.size.width - ACTION_BUTTON_WIDTH) * 0.5;
    result.origin.y += iconRect.size.height + PADDING_BETWEEN_IMAGE_AND_ACTION_BUTTON;
    result.size.width = ACTION_BUTTON_WIDTH;
    result.size.height = ACTION_BUTTON_HEIGHT;*/
    NSRect result = iconRect;
    result.origin.x += (iconRect.size.width - ACTION_BUTTON_WIDTH) * 0.5;
    result.origin.y += (iconRect.size.height - ACTION_BUTTON_WIDTH) * 0.5;
    result.size.width = ACTION_BUTTON_WIDTH;
    result.size.height = ACTION_BUTTON_WIDTH;
    
    
    return result;
}

- (NSUInteger) hitTestForEvent: (NSEvent *) event inRect: (NSRect) cellFrame ofView: (NSView *) controlView
{
    NSPoint point = [controlView convertPoint: [event locationInWindow] fromView: nil];
    
    if (NSMouseInRect(point, [self controlButtonRectForBounds: cellFrame], [controlView isFlipped])
        || NSMouseInRect(point, [self revealButtonRectForBounds: cellFrame], [controlView isFlipped]))
        return NSCellHitContentArea | NSCellHitTrackableArea;
    
    return NSCellHitContentArea;
}

+ (BOOL) prefersTrackingUntilMouseUp
{
    return YES;
}

- (BOOL) trackMouse: (NSEvent *) event inRect: (NSRect) cellFrame ofView: (NSView *) controlView untilMouseUp: (BOOL) flag
{
    fTracking = YES;
    
    [self setControlView: controlView];
    
    NSPoint point = [controlView convertPoint: [event locationInWindow] fromView: nil];
    
    fMouseDownControlButton = NO;
    NSRect controlRect= [self controlButtonRectForBounds: cellFrame];
    BOOL checkControl = NSMouseInRect(point, controlRect, [controlView isFlipped]);
    
    fMouseDownRevealButton = NO;
    NSRect revealRect = [self revealButtonRectForBounds: cellFrame];
    BOOL checkReveal = NSMouseInRect(point, revealRect, [controlView isFlipped]);
    
    [(TorrentTableView *)controlView removeButtonTrackingAreas];
    
    while ([event type] != NSLeftMouseUp)
    {
        point = [controlView convertPoint: [event locationInWindow] fromView: nil];
        
        if (checkControl)
        {
            BOOL inControlButton = NSMouseInRect(point, controlRect, [controlView isFlipped]);
            if (fMouseDownControlButton != inControlButton)
            {
                fMouseDownControlButton = inControlButton;
                [controlView setNeedsDisplayInRect: cellFrame];
            }
        }
        else if (checkReveal)
        {
            BOOL inRevealButton = NSMouseInRect(point, revealRect, [controlView isFlipped]);
            if (fMouseDownRevealButton != inRevealButton)
            {
                fMouseDownRevealButton = inRevealButton;
                [controlView setNeedsDisplayInRect: cellFrame];
            }
        }
        else;
        
        //send events to where necessary
        if ([event type] == NSMouseEntered || [event type] == NSMouseExited)
            [NSApp sendEvent: event];
        event = [[controlView window] nextEventMatchingMask:
                    (NSLeftMouseUpMask | NSLeftMouseDraggedMask | NSMouseEnteredMask | NSMouseExitedMask)];
    }
    
    fTracking = NO;

    if (fMouseDownControlButton)
    {
        fMouseDownControlButton = NO;
        [controlView setNeedsDisplayInRect: cellFrame];
        
        [(TorrentTableView *)controlView toggleControlForTorrent: [self representedObject]];
    }
    else if (fMouseDownRevealButton)
    {
        fMouseDownRevealButton = NO;
        [controlView setNeedsDisplayInRect: cellFrame];
        
        [[self representedObject] revealData];
    }
    else;
    
    [controlView updateTrackingAreas];
    
    return YES;
}

- (void) addTrackingAreasForView: (NSView *) controlView inRect: (NSRect) cellFrame withUserInfo: (NSDictionary *) userInfo
            mouseLocation: (NSPoint) mouseLocation
{
    NSTrackingAreaOptions options = NSTrackingEnabledDuringMouseDrag | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways;
    
    //control button
    NSRect controlButtonRect = [self controlButtonRectForBounds: cellFrame];
    NSTrackingAreaOptions controlOptions = options;
    if (NSMouseInRect(mouseLocation, controlButtonRect, [controlView isFlipped]))
    {
        controlOptions |= NSTrackingAssumeInside;
        [(TorrentTableView *)controlView setControlButtonHover: [[userInfo objectForKey: @"Row"] intValue]];
    }
    
    NSMutableDictionary * controlInfo = [userInfo mutableCopy];
    [controlInfo setObject: @"Control" forKey: @"Type"];
    NSTrackingArea * area = [[NSTrackingArea alloc] initWithRect: controlButtonRect options: controlOptions owner: controlView
                                userInfo: controlInfo];
    [controlView addTrackingArea: area];
    [controlInfo release];
    [area release];
    
    //reveal button
    NSRect revealButtonRect = [self revealButtonRectForBounds: cellFrame];
    NSTrackingAreaOptions revealOptions = options;
    if (NSMouseInRect(mouseLocation, revealButtonRect, [controlView isFlipped]))
    {
        revealOptions |= NSTrackingAssumeInside;
        [(TorrentTableView *)controlView setRevealButtonHover: [[userInfo objectForKey: @"Row"] intValue]];
    }
    
    NSMutableDictionary * revealInfo = [userInfo mutableCopy];
    [revealInfo setObject: @"Reveal" forKey: @"Type"];
    area = [[NSTrackingArea alloc] initWithRect: revealButtonRect options: revealOptions owner: controlView userInfo: revealInfo];
    [controlView addTrackingArea: area];
    [revealInfo release];
    [area release];
    
    //action button (needed even in minimal mode to display status string)
    NSRect actionButtonRect = [self actionButtonRectForBounds: cellFrame];
    NSTrackingAreaOptions actionOptions = options;
    if (NSMouseInRect(mouseLocation, actionButtonRect, [controlView isFlipped]))
    {
        actionOptions |= NSTrackingAssumeInside;
        [(TorrentTableView *)controlView setActionButtonHover: [[userInfo objectForKey: @"Row"] intValue]];
    }
    
    NSMutableDictionary * actionInfo = [userInfo mutableCopy];
    [actionInfo setObject: @"Action" forKey: @"Type"];
    area = [[NSTrackingArea alloc] initWithRect: actionButtonRect options: actionOptions owner: controlView userInfo: actionInfo];
    [controlView addTrackingArea: area];
    [actionInfo release];
    [area release];
}

- (void) setControlHover: (BOOL) hover
{
    fHoverControl = [NSApp isOnLeopardOrBetter] ? hover : NO;
}

- (void) setRevealHover: (BOOL) hover
{
    fHoverReveal = [NSApp isOnLeopardOrBetter] ? hover : NO;
}

- (void) setActionHover: (BOOL) hover
{
    fHoverAction = [NSApp isOnLeopardOrBetter] ? hover : NO;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    [super drawWithFrame: cellFrame inView: controlView];
    
    Torrent * torrent = [self representedObject];
    
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    //group coloring
    NSRect iconRect = [self iconRectForBounds: cellFrame];
    
    int groupValue = [torrent groupValue];
    if (groupValue != -1)
    {
        NSRect groupRect = NSInsetRect(iconRect, -2.0, -3.0);
        if (!minimal)
        {
            groupRect.size.height--;
            groupRect.origin.y--;
        }
        
        [[[GroupsWindowController groups] gradientForIndex: groupValue] fillBezierPath:
            [NSBezierPath bezierPathWithRoundedRect: groupRect radius: 6.0] angle: 90.0];
    }
    
    //error image
    BOOL error = [torrent isError];
    if (error && !fErrorImage)
    {
        fErrorImage = [NSImage imageNamed: @"Error.png"];
        [fErrorImage setFlipped: YES];
    }
    
    //icon
    if (!minimal || !(!fTracking && fHoverAction)) //don't show in minimal mode when hovered over
    {
        NSImage * icon = minimal && error ? fErrorImage : [torrent icon];
        [icon drawInRect: iconRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }
    
    if (error && !minimal)
    {
        NSRect errorRect = NSMakeRect(NSMaxX(iconRect) - IMAGE_SIZE_MIN, NSMaxY(iconRect) - IMAGE_SIZE_MIN,
                                        IMAGE_SIZE_MIN, IMAGE_SIZE_MIN);
        [fErrorImage drawInRect: errorRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }
    
    //text color
    NSColor * titleColor, * statusColor;
    if ([NSApp isOnLeopardOrBetter] ? [self backgroundStyle] == NSBackgroundStyleDark : [self isHighlighted]
        && [[self highlightColorWithFrame: cellFrame inView: controlView] isEqual: [NSColor alternateSelectedControlColor]])
    {
        titleColor = [NSColor whiteColor];
        statusColor = [NSColor whiteColor];
    }
    else
    {
        titleColor = [NSColor controlTextColor];
        statusColor = [NSColor darkGrayColor];
    }
    
    //minimal status
    NSRect minimalStatusRect;
    if (minimal)
    {
        NSAttributedString * minimalString = [self attributedStatusString: [self minimalStatusString] withColor: statusColor];
        minimalStatusRect = [self rectForMinimalStatusWithString: minimalString inBounds: cellFrame];
        
        [minimalString drawInRect: minimalStatusRect];
    }
    
    //title
    NSAttributedString * titleString = [self attributedTitleWithColor: titleColor];
    NSRect titleRect = [self rectForTitleWithString: titleString basedOnMinimalStatusRect: minimalStatusRect inBounds: cellFrame];
    [titleString drawInRect: titleRect];
    
    //progress
    if (!minimal)
    {
        NSAttributedString * progressString = [self attributedStatusString: [torrent progressString] withColor: statusColor];
        NSRect progressRect = [self rectForProgressWithString: progressString inBounds: cellFrame];
        [progressString drawInRect: progressRect];
    }
    
    //bar
    [self drawBar: [self barRectForBounds: cellFrame]];
    
    //control button
    NSString * controlImageSuffix;
    if (fMouseDownControlButton)
        controlImageSuffix = @"On.png";
    else if (!fTracking && fHoverControl)
        controlImageSuffix = @"Hover.png";
    else
        controlImageSuffix = @"Off.png";
    
    NSImage * controlImage;
    if ([torrent isActive])
        controlImage = [NSImage imageNamed: [@"Pause" stringByAppendingString: controlImageSuffix]];
    else
    {
        if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
            controlImage = [NSImage imageNamed: [@"ResumeNoWait" stringByAppendingString: controlImageSuffix]];
        else if ([torrent waitingToStart])
            controlImage = [NSImage imageNamed: [@"Pause" stringByAppendingString: controlImageSuffix]];
        else
            controlImage = [NSImage imageNamed: [@"Resume" stringByAppendingString: controlImageSuffix]];
    }
    
    [controlImage setFlipped: YES];
    [controlImage drawInRect: [self controlButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver
        fraction: 1.0];
    
    //reveal button
    NSString * revealImageSuffix;
    if (fMouseDownRevealButton)
        revealImageSuffix = @"On.png";
    else if (!fTracking && fHoverReveal)
        revealImageSuffix = @"Hover.png";
    else
        revealImageSuffix = @"Off.png";
    
    NSImage * revealImage = [NSImage imageNamed: [@"Reveal" stringByAppendingString: revealImageSuffix]];
    [revealImage setFlipped: YES];
    [revealImage drawInRect: [self revealButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver
        fraction: 1.0];
    
    //action button
    NSString * actionImageSuffix;
    if (!fTracking && fHoverAction)
        actionImageSuffix = @"Hover.png";
    else
        //actionImageSuffix = @"Off.png";
        actionImageSuffix = nil;
    
    if (actionImageSuffix)
    {
        NSImage * actionImage = [NSImage imageNamed: [@"Action" stringByAppendingString: actionImageSuffix]];
        [actionImage setFlipped: YES];
        [actionImage drawInRect: [self actionButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver
            fraction: 1.0];
    }
    
    //status
    if (!minimal)
    {
        NSAttributedString * statusString = [self attributedStatusString: [self statusString] withColor: statusColor];
        [statusString drawInRect: [self rectForStatusWithString: statusString inBounds: cellFrame]];
    }
}

@end

@implementation TorrentCell (Private)

- (void) drawBar: (NSRect) barRect
{
    if ([fDefaults boolForKey: @"PiecesBar"])
    {
        NSRect regularBarRect = barRect, piecesBarRect = barRect;
        regularBarRect.size.height /= 2.5;
        piecesBarRect.origin.y += regularBarRect.size.height;
        piecesBarRect.size.height -= regularBarRect.size.height;
        
        [self drawRegularBar: regularBarRect];
        [self drawPiecesBar: piecesBarRect];
    }
    else
    {
        [fBitmap release];
        fBitmap = nil;
        [[self representedObject] setPreviousAmountFinished: NULL];
        
        [self drawRegularBar: barRect];
    }
    
    [fBarOverlayColor set];
    [NSBezierPath strokeRect: NSInsetRect(barRect, 0.5, 0.5)];
}

- (void) drawRegularBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];
    
    int leftWidth = barRect.size.width;
    float progress = [torrent progress];
    
    if (progress < 1.0)
    {
        float rightProgress = 1.0 - progress, progressLeft = [torrent progressLeft];
        int rightWidth = leftWidth * rightProgress;
        leftWidth -= rightWidth;
        
        if (progressLeft < rightProgress)
        {
            int rightNoIncludeWidth = rightWidth * ((rightProgress - progressLeft) / rightProgress);
            rightWidth -= rightNoIncludeWidth;
            
            NSRect noIncludeRect = barRect;
            noIncludeRect.origin.x += barRect.size.width - rightNoIncludeWidth;
            noIncludeRect.size.width = rightNoIncludeWidth;
            
            if (!fLightGrayGradient)
                fLightGrayGradient = [[CTGradient progressLightGrayGradient] retain];
            [fLightGrayGradient fillRect: noIncludeRect angle: -90];
        }
        
        if (rightWidth > 0)
        {
            if ([torrent isActive] && ![torrent allDownloaded] && ![torrent isChecking]
                && [fDefaults boolForKey: @"DisplayProgressBarAvailable"])
            {
                int notAvailableWidth = MIN(ceil(barRect.size.width * [torrent notAvailableDesired]), rightWidth);
                rightWidth -= notAvailableWidth;
                
                if (notAvailableWidth > 0)
                {
                    NSRect notAvailableRect = barRect;
                    notAvailableRect.origin.x += leftWidth + rightWidth;
                    notAvailableRect.size.width = notAvailableWidth;
                    
                    if (!fRedGradient)
                        fRedGradient = [[CTGradient progressRedGradient] retain];
                    [fRedGradient fillRect: notAvailableRect angle: -90];
                }
            }
            
            if (rightWidth > 0)
            {
                NSRect includeRect = barRect;
                includeRect.origin.x += leftWidth;
                includeRect.size.width = rightWidth;
                
                if (!fWhiteGradient)
                    fWhiteGradient = [[CTGradient progressWhiteGradient] retain];
                [fWhiteGradient fillRect: includeRect angle: -90];
            }
        }
    }
    
    if (leftWidth > 0)
    {
        NSRect completeRect = barRect;
        completeRect.size.width = leftWidth;
        
        if ([torrent isActive])
        {
            if ([torrent isChecking])
            {
                if (!fYellowGradient)
                    fYellowGradient = [[CTGradient progressYellowGradient] retain];
                [fYellowGradient fillRect: completeRect angle: -90];
            }
            else if ([torrent isSeeding])
            {
                int ratioLeftWidth = leftWidth * (1.0 - [torrent progressStopRatio]);
                leftWidth -= ratioLeftWidth;
                
                if (ratioLeftWidth > 0)
                {
                    NSRect ratioLeftRect = barRect;
                    ratioLeftRect.origin.x += leftWidth;
                    ratioLeftRect.size.width = ratioLeftWidth;
                    
                    if (!fLightGreenGradient)
                        fLightGreenGradient = [[CTGradient progressLightGreenGradient] retain];
                    [fLightGreenGradient fillRect: ratioLeftRect angle: -90];
                }
                
                if (leftWidth > 0)
                {
                    completeRect.size.width = leftWidth;
                    
                    if (!fGreenGradient)
                        fGreenGradient = [[CTGradient progressGreenGradient] retain];
                    [fGreenGradient fillRect: completeRect angle: -90];
                }
            }
            else
            {
                if (!fBlueGradient)
                    fBlueGradient = [[CTGradient progressBlueGradient] retain];
                [fBlueGradient fillRect: completeRect angle: -90];
            }
        }
        else
        {
            if ([torrent waitingToStart])
            {
                if ([torrent progressLeft] <= 0.0)
                {
                    if (!fDarkGreenGradient)
                        fDarkGreenGradient = [[CTGradient progressDarkGreenGradient] retain];
                    [fDarkGreenGradient fillRect: completeRect angle: -90];
                }
                else
                {
                    if (!fDarkBlueGradient)
                        fDarkBlueGradient = [[CTGradient progressDarkBlueGradient] retain];
                    [fDarkBlueGradient fillRect: completeRect angle: -90];
                }
            }
            else
            {
                if (!fGrayGradient)
                    fGrayGradient = [[CTGradient progressGrayGradient] retain];
                [fGrayGradient fillRect: completeRect angle: -90];
            }
        }
    }
}

- (void) drawPiecesBar: (NSRect) barRect
{
    if (!fBitmap)
        fBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
            pixelsWide: MAX_PIECES pixelsHigh: barRect.size.height bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
            isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    Torrent * torrent = [self representedObject];
    
    int pieceCount = MIN([torrent pieceCount], MAX_PIECES);
    float * piecePercent = malloc(pieceCount * sizeof(float)),
        * previousPiecePercent = [torrent getPreviousAmountFinished];
    [torrent getAmountFinished: piecePercent size: pieceCount];
    
    int i, h, index;
    float increment = (float)pieceCount / MAX_PIECES;
    NSColor * pieceColor;
    for (i = 0; i < MAX_PIECES; i++)
    {
        index = i * increment;
        if (piecePercent[index] >= 1.0)
        {
            if (previousPiecePercent != NULL && previousPiecePercent[index] < 1.0)
                pieceColor = fOrangeColor;
            else
                pieceColor = fBlueColor;
        }
        else if (piecePercent[index] <= 0.0)
            pieceColor = fGrayColor;
        else if (piecePercent[index] <= 0.25)
            pieceColor = fBlue1Color;
        else if (piecePercent[index] <= 0.5)
            pieceColor = fBlue2Color;
        else if (piecePercent[index] <= 0.75)
            pieceColor = fBlue3Color;
        else
            pieceColor = fBlue4Color;
        
        if (![pieceColor isEqual: [fBitmap colorAtX: i y: 0]])
            for (h = 0; h < barRect.size.height; h++)
                [fBitmap setColor: pieceColor atX: i y: h];
    }
    
    [torrent setPreviousAmountFinished: piecePercent];
    
    //actually draw image
    [fBitmap drawInRect: barRect];
    
    if (!fTransparentGradient)
        fTransparentGradient = [[CTGradient progressTransparentGradient] retain];
    [fTransparentGradient fillRect: barRect angle: -90];
}

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if (![fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.size = [string size];
    
    result.origin.x += bounds.size.width - result.size.width - PADDING_HORIZONTAL;
    result.origin.y += PADDING_ABOVE_MIN_STATUS;
    
    return result;
}

- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE;
    result.origin.x += PADDING_HORIZONTAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL
                            - (minimal ? PADDING_BETWEEN_TITLE_AND_MIN_STATUS + statusRect.size.width : 0));
    
    return result;
}

- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x += PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL);
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS
                        + PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.origin.x += PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL);
    
    return result;
}

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color
{
    if (color)
        [fTitleAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    NSString * title = [[self representedObject] name];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusString: (NSString *) string withColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    return [[[NSAttributedString alloc] initWithString: string attributes: fStatusAttributes] autorelease];
}

- (NSString *) buttonString
{
    if (fMouseDownRevealButton || (!fTracking && fHoverReveal))
        return NSLocalizedString(@"Reveal the data file in Finder", "Torrent cell -> button info");
    else if (fMouseDownControlButton || (!fTracking && fHoverControl))
    {
        Torrent * torrent = [self representedObject];
        if ([torrent isActive])
            return NSLocalizedString(@"Pause the transfer", "Torrent Table -> tooltip");
        else
        {
            if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask && [fDefaults boolForKey: @"Queue"])
                return NSLocalizedString(@"Resume the transfer right away", "Torrent cell -> button info");
            else if ([torrent waitingToStart])
                return NSLocalizedString(@"Stop waiting to start", "Torrent cell -> button info");
            else
                return NSLocalizedString(@"Resume the transfer", "Torrent cell -> button info");
        }
    }
    else if (!fTracking && fHoverAction)
        return NSLocalizedString(@"Change transfer settings", "Torrent Table -> tooltip");
    else
        return nil;
}

- (NSString *) statusString
{
    NSString * buttonString;
    if ((buttonString = [self buttonString]))
        return buttonString;
    else
        return [[self representedObject] statusString];
}

- (NSString *) minimalStatusString
{
    NSString * buttonString;
    if ((buttonString = [self buttonString]))
        return buttonString;
    else
    {
        Torrent * torrent = [self representedObject];
        return [fDefaults boolForKey: @"DisplaySmallStatusRegular"] ? [torrent shortStatusString] : [torrent remainingTimeString];
    }
}

@end
