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
#import "GroupsController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "NSBezierPathAdditions.h"
#import "ProgressGradients.h"
#import "CTGradient.h"

#define BAR_HEIGHT 12.0f

#define IMAGE_SIZE_REG 32.0f
#define IMAGE_SIZE_MIN 16.0f

#define NORMAL_BUTTON_WIDTH 14.0f
#define ACTION_BUTTON_WIDTH 16.0f

//ends up being larger than font height
#define HEIGHT_TITLE 16.0f
#define HEIGHT_STATUS 12.0f

#define PADDING_HORIZONTAL 3.0f
#define PADDING_BETWEEN_IMAGE_AND_TITLE 5.0f
#define PADDING_BETWEEN_IMAGE_AND_BAR 7.0f
#define PADDING_ABOVE_TITLE 4.0f
#define PADDING_ABOVE_MIN_STATUS 4.0f
#define PADDING_BETWEEN_TITLE_AND_MIN_STATUS 2.0f
#define PADDING_BETWEEN_TITLE_AND_PROGRESS 1.0f
#define PADDING_BETWEEN_PROGRESS_AND_BAR 2.0f
#define PADDING_BETWEEN_TITLE_AND_BAR_MIN 3.0f
#define PADDING_BETWEEN_BAR_AND_STATUS 2.0f

#define PIECES_TOTAL_PERCENT 0.6f

#define MAX_PIECES (18*18)

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
                                [NSFont messageFontOfSize: 12.0f], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0f], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        [paragraphStyle release];
        
        fBluePieceColor = [[NSColor colorWithCalibratedRed: 0.0f green: 0.4f blue: 0.8f alpha: 1.0f] retain];
        fBarBorderColor = [[NSColor colorWithCalibratedWhite: 0.0f alpha: 0.2f] retain];
    }
	return self;
}

- (NSRect) iconRectForBounds: (NSRect) bounds
{
    CGFloat imageSize = [fDefaults boolForKey: @"SmallView"] ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG;
    
    NSRect result = bounds;
    result.origin.x += PADDING_HORIZONTAL;
    result.origin.y += floorf((result.size.height - imageSize) * 0.5f);
    result.size = NSMakeSize(imageSize, imageSize);
    
    return result;
}

- (NSRect) titleRectForBounds: (NSRect) bounds
{
    return [self rectForTitleWithString: [self attributedTitleWithColor: nil]
            basedOnMinimalStatusRect: [self minimalStatusRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) minimalStatusRectForBounds: (NSRect) bounds
{
    if (![fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    return [self rectForMinimalStatusWithString: [self attributedStatusString: [self minimalStatusString] withColor: nil]
            inBounds: bounds];
}

- (NSRect) progressRectForBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    return [self rectForProgressWithString: [self attributedStatusString: [[self representedObject] progressString] withColor: nil]
            inBounds: bounds];
}

- (NSRect) barRectForBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.size.height = BAR_HEIGHT;
    result.origin.x += (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_BAR;
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE;
    if (minimal)
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    result.size.width = round(NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL - 2.0f * (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH));
    
    return result;
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    return [self rectForStatusWithString: [self attributedStatusString: [self statusString] withColor: nil] inBounds: bounds];
}

- (NSRect) controlButtonRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - 2.0f * (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH);
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5f;
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
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5f;
    if ([fDefaults boolForKey: @"SmallView"])
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    return result;
}

- (NSRect) actionButtonRectForBounds: (NSRect) bounds
{
    NSRect result = [self iconRectForBounds: bounds];
    if (![fDefaults boolForKey: @"SmallView"])
    {
        result.origin.x += (result.size.width - ACTION_BUTTON_WIDTH) * 0.5f;
        result.origin.y += (result.size.height - ACTION_BUTTON_WIDTH) * 0.5f;
        result.size.width = ACTION_BUTTON_WIDTH;
        result.size.height = ACTION_BUTTON_WIDTH;
    }
    
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
    
    NSRect controlRect= [self controlButtonRectForBounds: cellFrame];
    BOOL checkControl = NSMouseInRect(point, controlRect, [controlView isFlipped]);
    
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
        
        [(TorrentTableView *)controlView toggleControlForTorrent: [self representedObject]];
    }
    else if (fMouseDownRevealButton)
    {
        fMouseDownRevealButton = NO;
        [controlView setNeedsDisplayInRect: cellFrame];
        
        [[self representedObject] revealData];
    }
    else;
    
    if ([NSApp isOnLeopardOrBetter])
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
    
    //action button
    NSRect actionButtonRect = [self iconRectForBounds: cellFrame]; //use the whole icon
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
    fHoverControl = hover;
}

- (void) setRevealHover: (BOOL) hover
{
    fHoverReveal = hover;
}

- (void) setActionHover: (BOOL) hover
{
    fHoverAction = hover;
}

- (void) setActionPushed: (BOOL) pushed
{
    fMouseDownActionButton = pushed;
}

- (void) drawInteriorWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    Torrent * torrent = [self representedObject];
    
    const BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    //group coloring
    NSRect iconRect = [self iconRectForBounds: cellFrame];
    
    NSInteger groupValue = [torrent groupValue];
    if (groupValue != -1)
    {
        NSRect groupRect = NSInsetRect(iconRect, -1.0f, -2.0f);
        if (!minimal)
        {
            groupRect.size.height--;
            groupRect.origin.y--;
        }
        const CGFloat radius = minimal ? 3.0f : 6.0f;
        
        NSColor * groupColor = [[GroupsController groups] colorForIndex: groupValue],
                * darkGroupColor = [groupColor blendedColorWithFraction: 0.2f ofColor: [NSColor whiteColor]];
        
        //border
        NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: groupRect radius: radius];
        [darkGroupColor set];
        [bp setLineWidth: 2.0f];
        [bp stroke];
        
        //inside
        bp = [NSBezierPath bezierPathWithRoundedRect: groupRect radius: radius];
        CTGradient * gradient = [CTGradient gradientWithBeginningColor: [groupColor blendedColorWithFraction: 0.7f
                                    ofColor: [NSColor whiteColor]] endingColor: darkGroupColor];
        [gradient fillBezierPath: bp angle: 90.0f];
    }
    
    //error image
    const BOOL error = [torrent isError];
    if (error && !fErrorImage)
    {
        fErrorImage = [NSImage imageNamed: @"Error.png"];
        [fErrorImage setFlipped: YES];
    }
    
    //icon
    if (!minimal || !(!fTracking && fHoverAction)) //don't show in minimal mode when hovered over
    {
        NSImage * icon = (minimal && error) ? fErrorImage : [torrent icon];
        [icon drawInRect: iconRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0f];
    }
    
    if (error && !minimal)
    {
        NSRect errorRect = NSMakeRect(NSMaxX(iconRect) - IMAGE_SIZE_MIN, NSMaxY(iconRect) - IMAGE_SIZE_MIN,
                                        IMAGE_SIZE_MIN, IMAGE_SIZE_MIN);
        [fErrorImage drawInRect: errorRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0f];
    }
    
    //text color
    NSColor * titleColor, * statusColor;
    if ([self isHighlighted]
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
        fraction: 1.0f];
    
    //reveal button
    NSString * revealImageString;
    if (fMouseDownRevealButton)
        revealImageString = @"RevealOn.png";
    else if (!fTracking && fHoverReveal)
        revealImageString = @"RevealHover.png";
    else
        revealImageString = @"RevealOff.png";
    
    NSImage * revealImage = [NSImage imageNamed: revealImageString];
    [revealImage setFlipped: YES];
    [revealImage drawInRect: [self revealButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver
        fraction: 1.0f];
    
    //action button
    NSString * actionImageString;
    if (fMouseDownActionButton)
        actionImageString = @"ActionOn.png";
    else if (!fTracking && fHoverAction)
        actionImageString = @"ActionHover.png";
    else
        actionImageString = nil;
    
    if (actionImageString)
    {
        NSImage * actionImage = [NSImage imageNamed: actionImageString];
        [actionImage setFlipped: YES];
        [actionImage drawInRect: [self actionButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver
            fraction: 1.0f];
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
    CGFloat piecesBarPercent = [(TorrentTableView *)[self controlView] piecesBarPercent];
    if (piecesBarPercent > 0.0f)
    {
        NSRect regularBarRect = barRect, piecesBarRect = barRect;
        piecesBarRect.size.height *= PIECES_TOTAL_PERCENT * piecesBarPercent;
        regularBarRect.size.height -= piecesBarRect.size.height;
        piecesBarRect.origin.y += regularBarRect.size.height;
        
        [self drawRegularBar: regularBarRect];
        [self drawPiecesBar: piecesBarRect];
    }
    else
    {
        [[self representedObject] setPreviousFinishedPieces: nil];
        
        [self drawRegularBar: barRect];
    }
    
    [fBarBorderColor set];
    [NSBezierPath strokeRect: NSInsetRect(barRect, 0.5f, 0.5f)];
}

- (void) drawRegularBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];
    
    NSInteger leftWidth = barRect.size.width;
    CGFloat progress = [torrent progress];
    
    if (progress < 1.0f)
    {
        CGFloat rightProgress = 1.0f - progress, progressLeft = [torrent progressLeft];
        NSInteger rightWidth = leftWidth * rightProgress;
        leftWidth -= rightWidth;
        
        if (progressLeft < rightProgress)
        {
            NSInteger rightNoIncludeWidth = rightWidth * ((rightProgress - progressLeft) / rightProgress);
            rightWidth -= rightNoIncludeWidth;
            
            NSRect noIncludeRect = barRect;
            noIncludeRect.origin.x += barRect.size.width - rightNoIncludeWidth;
            noIncludeRect.size.width = rightNoIncludeWidth;
            
            [[ProgressGradients progressLightGrayGradient] fillRect: noIncludeRect angle: 90];
        }
        
        if (rightWidth > 0)
        {
            if ([torrent isActive] && ![torrent allDownloaded] && ![torrent isChecking]
                && [fDefaults boolForKey: @"DisplayProgressBarAvailable"])
            {
                NSInteger notAvailableWidth = ceil(rightWidth * [torrent notAvailableDesired]);
                if (notAvailableWidth > 0)
                {
                    rightWidth -= notAvailableWidth;
                    
                    NSRect notAvailableRect = barRect;
                    notAvailableRect.origin.x += leftWidth + rightWidth;
                    notAvailableRect.size.width = notAvailableWidth;
                    
                    [[ProgressGradients progressRedGradient] fillRect: notAvailableRect angle: 90];
                }
            }
            
            if (rightWidth > 0)
            {
                NSRect includeRect = barRect;
                includeRect.origin.x += leftWidth;
                includeRect.size.width = rightWidth;
                
                [[ProgressGradients progressWhiteGradient] fillRect: includeRect angle: 90];
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
                [[ProgressGradients progressYellowGradient] fillRect: completeRect angle: 90];
            else if ([torrent isSeeding])
            {
                NSInteger ratioLeftWidth = leftWidth * (1.0f - [torrent progressStopRatio]);
                leftWidth -= ratioLeftWidth;
                
                if (ratioLeftWidth > 0)
                {
                    NSRect ratioLeftRect = barRect;
                    ratioLeftRect.origin.x += leftWidth;
                    ratioLeftRect.size.width = ratioLeftWidth;
                    
                    [[ProgressGradients progressLightGreenGradient] fillRect: ratioLeftRect angle: 90];
                }
                
                if (leftWidth > 0)
                {
                    completeRect.size.width = leftWidth;
                    
                    [[ProgressGradients progressGreenGradient] fillRect: completeRect angle: 90];
                }
            }
            else
                [[ProgressGradients progressBlueGradient] fillRect: completeRect angle: 90];
        }
        else
        {
            if ([torrent waitingToStart])
            {
                if ([torrent progressLeft] <= 0.0f)
                    [[ProgressGradients progressDarkGreenGradient] fillRect: completeRect angle: 90];
                else
                    [[ProgressGradients progressDarkBlueGradient] fillRect: completeRect angle: 90];
            }
            else
                [[ProgressGradients progressGrayGradient] fillRect: completeRect angle: 90];
        }
    }
}

- (void) drawPiecesBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];
    
    NSInteger pieceCount = MIN([torrent pieceCount], MAX_PIECES);
    float * piecesPercent = malloc(pieceCount * sizeof(float));
    [torrent getAmountFinished: piecesPercent size: pieceCount];
    
    NSBitmapImageRep * bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
                                    pixelsWide: pieceCount pixelsHigh: 1 bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
                                    isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    NSIndexSet * previousFinishedIndexes = [torrent previousFinishedPieces];
    NSMutableIndexSet * finishedIndexes = [NSMutableIndexSet indexSet];
    
    for (NSInteger i = 0; i < pieceCount; i++)
    {
        NSColor * pieceColor;
        if (piecesPercent[i] == 1.0f)
        {
            if (previousFinishedIndexes && ![previousFinishedIndexes containsIndex: i])
                pieceColor = [NSColor orangeColor];
            else
                pieceColor = fBluePieceColor;
            [finishedIndexes addIndex: i];
        }
        else
            pieceColor = [[NSColor whiteColor] blendedColorWithFraction: piecesPercent[i] ofColor: fBluePieceColor];
        
        //it's faster to just set color instead of checking previous color
        [bitmap setColor: pieceColor atX: i y: 0];
    }
    
    free(piecesPercent);
    
    [torrent setPreviousFinishedPieces: [finishedIndexes count] > 0 ? finishedIndexes : nil]; //don't bother saving if none are complete
    
    //actually draw image
    [bitmap drawInRect: barRect];
    [bitmap release];
}

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
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
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x += PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL);
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
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
            if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
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
