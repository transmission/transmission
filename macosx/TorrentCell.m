/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2009 Transmission authors and contributors
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
#import "GroupsController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "ProgressGradients.h"
#import "Torrent.h"
#import "TorrentTableView.h"

#define BAR_HEIGHT 12.0f

#define IMAGE_SIZE_REG 32.0f
#define IMAGE_SIZE_MIN 16.0f
#define ERROR_IMAGE_SIZE 20.0f

#define NORMAL_BUTTON_WIDTH 14.0f
#define ACTION_BUTTON_WIDTH 16.0f

#define PRIORITY_ICON_WIDTH 14.0f
#define PRIORITY_ICON_HEIGHT 14.0f

//ends up being larger than font height
#define HEIGHT_TITLE 16.0f
#define HEIGHT_STATUS 12.0f

#define PADDING_HORIZONTAL 3.0f
#define PADDING_BETWEEN_IMAGE_AND_TITLE 5.0f
#define PADDING_BETWEEN_IMAGE_AND_BAR 7.0f
#define PADDING_BETWEEN_TITLE_AND_PRIORITY 4.0f
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

- (NSAttributedString *) attributedTitle;
- (NSAttributedString *) attributedStatusString: (NSString *) string;

- (NSString *) buttonString;
- (NSString *) statusString;
- (NSString *) minimalStatusString;

- (void) drawImage: (NSImage *) image inRect: (NSRect) rect; //use until 10.5 dropped

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
    return [self rectForTitleWithString: [self attributedTitle]
            basedOnMinimalStatusRect: [self minimalStatusRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) minimalStatusRectForBounds: (NSRect) bounds
{
    if (![fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    return [self rectForMinimalStatusWithString: [self attributedStatusString: [self minimalStatusString]]
            inBounds: bounds];
}

- (NSRect) progressRectForBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    return [self rectForProgressWithString: [self attributedStatusString: [[self representedObject] progressString]]
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
    
    return [self rectForStatusWithString: [self attributedStatusString: [self statusString]] inBounds: bounds];
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
        
        if ([NSApp isOnSnowLeopardOrBetter])
        {
            NSString * location = [[self representedObject] dataLocation];
            if (location)
            {
                NSURL * file = [NSURL fileURLWithPath: location];
                [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: [NSArray arrayWithObject: file]];
            }
        }
        else
        {
            NSString * location = [[self representedObject] dataLocation];
            if (location)
                [[NSWorkspace sharedWorkspace] selectFile: location inFileViewerRootedAtPath: nil];
        }
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
        NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: groupRect xRadius: radius yRadius: radius];
        [darkGroupColor set];
        [bp setLineWidth: 2.0f];
        [bp stroke];
        
        //inside
        bp = [NSBezierPath bezierPathWithRoundedRect: groupRect xRadius: radius yRadius: radius];
        NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: [groupColor blendedColorWithFraction: 0.7f
                                    ofColor: [NSColor whiteColor]] endingColor: darkGroupColor];
        [gradient drawInBezierPath: bp angle: 90.0f];
        [gradient release];
    }
    
    const BOOL error = [torrent isAnyErrorOrWarning];
    
    //icon
    if (!minimal || !(!fTracking && fHoverAction)) //don't show in minimal mode when hovered over
    {
        NSImage * icon = (minimal && error) ? [NSImage imageNamed: [NSApp isOnSnowLeopardOrBetter] ? NSImageNameCaution : @"Error.png"]
                                            : [torrent icon];
        [self drawImage: icon inRect: iconRect];
    }
    
    //error badge
    if (error && !minimal)
    {
        NSRect errorRect = NSMakeRect(NSMaxX(iconRect) - ERROR_IMAGE_SIZE, NSMaxY(iconRect) - ERROR_IMAGE_SIZE,
                                        ERROR_IMAGE_SIZE, ERROR_IMAGE_SIZE);
        [self drawImage: [NSImage imageNamed: [NSApp isOnSnowLeopardOrBetter] ? NSImageNameCaution : @"Error.png"] inRect: errorRect];
    }
    
    //text color
    NSColor * titleColor, * statusColor;
    if ([self backgroundStyle] == NSBackgroundStyleDark)
        titleColor = statusColor = [NSColor whiteColor];
    else
    {
        titleColor = [NSColor controlTextColor];
        statusColor = [NSColor darkGrayColor];
    }
    
    [fTitleAttributes setObject: titleColor forKey: NSForegroundColorAttributeName];
    [fStatusAttributes setObject: statusColor forKey: NSForegroundColorAttributeName];
    
    //minimal status
    NSRect minimalStatusRect;
    if (minimal)
    {
        NSAttributedString * minimalString = [self attributedStatusString: [self minimalStatusString]];
        minimalStatusRect = [self rectForMinimalStatusWithString: minimalString inBounds: cellFrame];
        
        [minimalString drawInRect: minimalStatusRect];
    }
    
    //title
    NSAttributedString * titleString = [self attributedTitle];
    NSRect titleRect = [self rectForTitleWithString: titleString basedOnMinimalStatusRect: minimalStatusRect inBounds: cellFrame];
    [titleString drawInRect: titleRect];
    
    //priority icon
    if ([torrent priority] != TR_PRI_NORMAL)
    {
        NSImage * priorityImage = [torrent priority] == TR_PRI_HIGH ? [NSImage imageNamed: @"PriorityHigh.png"]
                                                                    : [NSImage imageNamed: @"PriorityLow.png"];
        
        NSRect priorityRect = NSMakeRect(NSMaxX(titleRect) + PADDING_BETWEEN_TITLE_AND_PRIORITY,
                                titleRect.origin.y - (PRIORITY_ICON_HEIGHT - titleRect.size.height) / 2.0,
                                PRIORITY_ICON_WIDTH, PRIORITY_ICON_HEIGHT);
        
        [self drawImage: priorityImage inRect: priorityRect];
    }
    
    //progress
    if (!minimal)
    {
        NSAttributedString * progressString = [self attributedStatusString: [torrent progressString]];
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
    
    [self drawImage: controlImage inRect: [self controlButtonRectForBounds: cellFrame]];
    
    //reveal button
    NSString * revealImageString;
    if (fMouseDownRevealButton)
        revealImageString = @"RevealOn.png";
    else if (!fTracking && fHoverReveal)
        revealImageString = @"RevealHover.png";
    else
        revealImageString = @"RevealOff.png";
    
    NSImage * revealImage = [NSImage imageNamed: revealImageString];
    [self drawImage: revealImage inRect: [self revealButtonRectForBounds: cellFrame]];
    
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
        [self drawImage: actionImage inRect: [self actionButtonRectForBounds: cellFrame]];
    }
    
    //status
    if (!minimal)
    {
        NSAttributedString * statusString = [self attributedStatusString: [self statusString]];
        [statusString drawInRect: [self rectForStatusWithString: statusString inBounds: cellFrame]];
    }
}

@end

@implementation TorrentCell (Private)

- (void) drawBar: (NSRect) barRect
{
    const CGFloat piecesBarPercent = [(TorrentTableView *)[self controlView] piecesBarPercent];
    if (piecesBarPercent > 0.0)
    {
        NSRect piecesBarRect, regularBarRect;
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floorf(NSHeight(barRect) * PIECES_TOTAL_PERCENT * piecesBarPercent),
                    NSMaxYEdge);
        
        [self drawRegularBar: regularBarRect];
        [self drawPiecesBar: piecesBarRect];
    }
    else
    {
        [[self representedObject] setPreviousFinishedPieces: nil];
        
        [self drawRegularBar: barRect];
    }
    
    [fBarBorderColor set];
    [NSBezierPath strokeRect: NSInsetRect(barRect, 0.5, 0.5)];
}

- (void) drawRegularBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];
    
    NSRect haveRect, missingRect;
    NSDivideRect(barRect, &haveRect, &missingRect, floorf([torrent progress] * NSWidth(barRect)), NSMinXEdge);
    
    if (!NSIsEmptyRect(haveRect))
    {
        if ([torrent isActive])
        {
            if ([torrent isChecking])
                [[ProgressGradients progressYellowGradient] drawInRect: haveRect angle: 90];
            else if ([torrent isSeeding])
            {
                NSRect ratioHaveRect, ratioRemainingRect;
                NSDivideRect(haveRect, &ratioHaveRect, &ratioRemainingRect, floorf([torrent progressStopRatio] * NSWidth(haveRect)),
                            NSMinXEdge);
                
                [[ProgressGradients progressGreenGradient] drawInRect: ratioHaveRect angle: 90];
                [[ProgressGradients progressLightGreenGradient] drawInRect: ratioRemainingRect angle: 90];
            }
            else
                [[ProgressGradients progressBlueGradient] drawInRect: haveRect angle: 90];
        }
        else
        {
            if ([torrent waitingToStart])
            {
                if ([torrent allDownloaded])
                    [[ProgressGradients progressDarkGreenGradient] drawInRect: haveRect angle: 90];
                else
                    [[ProgressGradients progressDarkBlueGradient] drawInRect: haveRect angle: 90];
            }
            else
                [[ProgressGradients progressGrayGradient] drawInRect: haveRect angle: 90];
        }
    }
    
    if (!NSIsEmptyRect(missingRect))
    {
        if (![torrent allDownloaded])
        {
            //the ratio of total progress to total width equals ratio of progress of amount wanted to wanted width
            const CGFloat widthRemaining = floorf(NSWidth(barRect) * (1.0 - [torrent progressDone]) / [torrent progress]);
            
            NSRect wantedRect;
            NSDivideRect(missingRect, &wantedRect, &missingRect, widthRemaining, NSMinXEdge);
            
            //not-available section
            if ([torrent isActive] && ![torrent isChecking] && [fDefaults boolForKey: @"DisplayProgressBarAvailable"]
                && [torrent availableDesired] > 0.0)
            {
                NSRect unavailableRect;
                NSDivideRect(wantedRect, &wantedRect, &unavailableRect, floorf([torrent availableDesired] * NSWidth(wantedRect)),
                            NSMinXEdge);
                
                [[ProgressGradients progressRedGradient] drawInRect: unavailableRect angle: 90];
            }
            
            //remaining section
            [[ProgressGradients progressWhiteGradient] drawInRect: wantedRect angle: 90];
        }
        
        //unwanted section
        [[ProgressGradients progressLightGrayGradient] drawInRect: missingRect angle: 90];
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
    NSRect result;
    result.size = [string size];
    
    result.origin.x = NSMaxX(bounds) - (NSWidth(result) + PADDING_HORIZONTAL);
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_MIN_STATUS;
    
    return result;
}

- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds
{
    const BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL
                        + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL
                - (minimal ? PADDING_BETWEEN_TITLE_AND_MIN_STATUS + statusRect.size.width : 0.0)
                - ([[self representedObject] priority] != TR_PRI_NORMAL ? PRIORITY_ICON_WIDTH + PADDING_BETWEEN_TITLE_AND_PRIORITY: 0.0));
    
    return result;
}

- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL);
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS
                        + PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONTAL);
    
    return result;
}

- (NSAttributedString *) attributedTitle
{
    NSString * title = [[self representedObject] name];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusString: (NSString *) string
{
    return [[[NSAttributedString alloc] initWithString: string attributes: fStatusAttributes] autorelease];
}

- (NSString *) buttonString
{
    if (fMouseDownRevealButton || (!fTracking && fHoverReveal))
        return NSLocalizedString(@"Show the data file in Finder", "Torrent cell -> button info");
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

- (void) drawImage: (NSImage *) image inRect: (NSRect) rect
{
    if ([NSApp isOnSnowLeopardOrBetter])
        [image drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
    else
    {
        [image setFlipped: YES];
        [image drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }
}

@end
