/******************************************************************************
 * Copyright (c) 2006-2012 Transmission authors and contributors
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
#import "NSImageAdditions.h"
#import "NSStringAdditions.h"
#import "ProgressGradients.h"
#import "Torrent.h"
#import "TorrentTableView.h"

#define BAR_HEIGHT 12.0

#define IMAGE_SIZE_REG 32.0
#define IMAGE_SIZE_MIN 16.0
#define ERROR_IMAGE_SIZE 20.0

#define NORMAL_BUTTON_WIDTH 14.0
#define ACTION_BUTTON_WIDTH 16.0

#define PRIORITY_ICON_WIDTH 12.0
#define PRIORITY_ICON_HEIGHT 12.0

//ends up being larger than font height
#define HEIGHT_TITLE 16.0
#define HEIGHT_STATUS 12.0

#define PADDING_HORIZONTAL 5.0
#define PADDING_BETWEEN_BUTTONS 3.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE (PADDING_HORIZONTAL + 1.0)
#define PADDING_BETWEEN_IMAGE_AND_BAR PADDING_HORIZONTAL
#define PADDING_BETWEEN_TITLE_AND_PRIORITY 6.0
#define PADDING_ABOVE_TITLE 4.0
#define PADDING_BETWEEN_TITLE_AND_MIN_STATUS 3.0
#define PADDING_BETWEEN_TITLE_AND_PROGRESS 1.0
#define PADDING_BETWEEN_PROGRESS_AND_BAR 2.0
#define PADDING_BETWEEN_BAR_AND_STATUS 2.0
#define PADDING_BETWEEN_BAR_AND_EDGE_MIN 3.0
#define PADDING_EXPANSION_FRAME 2.0

#define PIECES_TOTAL_PERCENT 0.6

#define MAX_PIECES (18*18)

@interface TorrentCell (Private)

- (void) drawBar: (NSRect) barRect;
- (void) drawRegularBar: (NSRect) barRect;
- (void) drawPiecesBar: (NSRect) barRect;

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForTitleWithString: (NSAttributedString *) string withRightBound: (CGFloat) rightBound inBounds: (NSRect) bounds;
- (NSRect) rectForProgressWithStringInBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithStringInBounds: (NSRect) bounds;
- (NSRect) barRectRegForBounds: (NSRect) bounds;
- (NSRect) barRectMinForBounds: (NSRect) bounds;

- (NSRect) controlButtonRectForBounds: (NSRect) bounds;
- (NSRect) revealButtonRectForBounds: (NSRect) bounds;
- (NSRect) actionButtonRectForBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitle;
- (NSAttributedString *) attributedStatusString: (NSString *) string;

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
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingMiddle];

        fTitleAttributes = [[NSMutableDictionary alloc] initWithCapacity: 3];
        fTitleAttributes[NSFontAttributeName] = [NSFont messageFontOfSize: 12.0];
        fTitleAttributes[NSParagraphStyleAttributeName] = paragraphStyle;

        fStatusAttributes = [[NSMutableDictionary alloc] initWithCapacity: 3];
        fStatusAttributes[NSFontAttributeName] = [NSFont messageFontOfSize: 9.0];
        fStatusAttributes[NSParagraphStyleAttributeName] = paragraphStyle;


        fBluePieceColor = [NSColor colorWithCalibratedRed: 0.0 green: 0.4 blue: 0.8 alpha: 1.0];
        fBarBorderColor = [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.2];
        fBarMinimalBorderColor = [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.015];
    }
    return self;
}

- (id) copyWithZone: (NSZone *) zone
{
    id value = [super copyWithZone: zone];
    [value setRepresentedObject: [self representedObject]];
    return value;
}

- (NSRect) iconRectForBounds: (NSRect) bounds
{
    const CGFloat imageSize = [fDefaults boolForKey: @"SmallView"] ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG;

    return NSMakeRect(NSMinX(bounds) + PADDING_HORIZONTAL, ceil(NSMidY(bounds) - imageSize * 0.5),
                        imageSize, imageSize);
}

- (NSCellHitResult) hitTestForEvent: (NSEvent *) event inRect: (NSRect) cellFrame ofView: (NSView *) controlView
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

    const NSRect controlRect = [self controlButtonRectForBounds: cellFrame];
    const BOOL checkControl = NSMouseInRect(point, controlRect, [controlView isFlipped]);

    const NSRect revealRect = [self revealButtonRectForBounds: cellFrame];
    const BOOL checkReveal = NSMouseInRect(point, revealRect, [controlView isFlipped]);

    [(TorrentTableView *)controlView removeTrackingAreas];

    while ([event type] != NSLeftMouseUp)
    {
        point = [controlView convertPoint: [event locationInWindow] fromView: nil];

        if (checkControl)
        {
            const BOOL inControlButton = NSMouseInRect(point, controlRect, [controlView isFlipped]);
            if (fMouseDownControlButton != inControlButton)
            {
                fMouseDownControlButton = inControlButton;
                [controlView setNeedsDisplayInRect: cellFrame];
            }
        }
        else if (checkReveal)
        {
            const BOOL inRevealButton = NSMouseInRect(point, revealRect, [controlView isFlipped]);
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

        NSString * location = [[self representedObject] dataLocation];
        if (location)
        {
            NSURL * file = [NSURL fileURLWithPath: location];
            [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: @[file]];
        }
    }
    else;

    [controlView updateTrackingAreas];

    return YES;
}

- (void) addTrackingAreasForView: (NSView *) controlView inRect: (NSRect) cellFrame withUserInfo: (NSDictionary *) userInfo
            mouseLocation: (NSPoint) mouseLocation
{
    const NSTrackingAreaOptions options = NSTrackingEnabledDuringMouseDrag | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways;

    //whole row
    if ([fDefaults boolForKey: @"SmallView"])
    {
        NSTrackingAreaOptions rowOptions = options;
        if (NSMouseInRect(mouseLocation, cellFrame, [controlView isFlipped]))
        {
            rowOptions |= NSTrackingAssumeInside;
            [(TorrentTableView *)controlView setRowHover: [userInfo[@"Row"] integerValue]];
        }

        NSMutableDictionary * rowInfo = [userInfo mutableCopy];
        rowInfo[@"Type"] = @"Row";
        NSTrackingArea * area = [[NSTrackingArea alloc] initWithRect: cellFrame options: rowOptions owner: controlView userInfo: rowInfo];
        [controlView addTrackingArea: area];
    }

    //control button
    NSRect controlButtonRect = [self controlButtonRectForBounds: cellFrame];
    NSTrackingAreaOptions controlOptions = options;
    if (NSMouseInRect(mouseLocation, controlButtonRect, [controlView isFlipped]))
    {
        controlOptions |= NSTrackingAssumeInside;
        [(TorrentTableView *)controlView setControlButtonHover: [userInfo[@"Row"] integerValue]];
    }

    NSMutableDictionary * controlInfo = [userInfo mutableCopy];
    controlInfo[@"Type"] = @"Control";
    NSTrackingArea * area = [[NSTrackingArea alloc] initWithRect: controlButtonRect options: controlOptions owner: controlView
                                userInfo: controlInfo];
    [controlView addTrackingArea: area];

    //reveal button
    NSRect revealButtonRect = [self revealButtonRectForBounds: cellFrame];
    NSTrackingAreaOptions revealOptions = options;
    if (NSMouseInRect(mouseLocation, revealButtonRect, [controlView isFlipped]))
    {
        revealOptions |= NSTrackingAssumeInside;
        [(TorrentTableView *)controlView setRevealButtonHover: [userInfo[@"Row"] integerValue]];
    }

    NSMutableDictionary * revealInfo = [userInfo mutableCopy];
    revealInfo[@"Type"] = @"Reveal";
    area = [[NSTrackingArea alloc] initWithRect: revealButtonRect options: revealOptions owner: controlView
                                userInfo: revealInfo];
    [controlView addTrackingArea: area];

    //action button
    NSRect actionButtonRect = [self iconRectForBounds: cellFrame]; //use the whole icon
    NSTrackingAreaOptions actionOptions = options;
    if (NSMouseInRect(mouseLocation, actionButtonRect, [controlView isFlipped]))
    {
        actionOptions |= NSTrackingAssumeInside;
        [(TorrentTableView *)controlView setActionButtonHover: [userInfo[@"Row"] integerValue]];
    }

    NSMutableDictionary * actionInfo = [userInfo mutableCopy];
    actionInfo[@"Type"] = @"Action";
    area = [[NSTrackingArea alloc] initWithRect: actionButtonRect options: actionOptions owner: controlView userInfo: actionInfo];
    [controlView addTrackingArea: area];
}

- (void) setHover: (BOOL) hover
{
    fHover = hover;
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
    NSAssert(torrent != nil, @"can't have a TorrentCell without a Torrent");

    const BOOL minimal = [fDefaults boolForKey: @"SmallView"];

    //bar
    [self drawBar: minimal ? [self barRectMinForBounds: cellFrame] : [self barRectRegForBounds: cellFrame]];

    //group coloring
    const NSRect iconRect = [self iconRectForBounds: cellFrame];

    const NSInteger groupValue = [torrent groupValue];
    if (groupValue != -1)
    {
        NSRect groupRect = NSInsetRect(iconRect, -1.0, -2.0);
        if (!minimal)
        {
            groupRect.size.height -= 1.0;
            groupRect.origin.y -= 1.0;
        }
        const CGFloat radius = minimal ? 3.0 : 6.0;

        NSColor * groupColor = [[GroupsController groups] colorForIndex: groupValue],
                * darkGroupColor = [groupColor blendedColorWithFraction: 0.2 ofColor: [NSColor whiteColor]];

        //border
        NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: groupRect xRadius: radius yRadius: radius];
        [darkGroupColor set];
        [bp setLineWidth: 2.0];
        [bp stroke];

        //inside
        bp = [NSBezierPath bezierPathWithRoundedRect: groupRect xRadius: radius yRadius: radius];
        NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: [groupColor blendedColorWithFraction: 0.7
                                    ofColor: [NSColor whiteColor]] endingColor: darkGroupColor];
        [gradient drawInBezierPath: bp angle: 90.0];
    }

    const BOOL error = [torrent isAnyErrorOrWarning];

    //icon
    if (!minimal || !(!fTracking && fHoverAction)) //don't show in minimal mode when hovered over
    {
        NSImage * icon = (minimal && error) ? [NSImage imageNamed: NSImageNameCaution]
                                            : [torrent icon];
        [icon drawInRect: iconRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
    }

    //error badge
    if (error && !minimal)
    {
        NSImage * errorImage = [NSImage imageNamed: NSImageNameCaution];
        const NSRect errorRect = NSMakeRect(NSMaxX(iconRect) - ERROR_IMAGE_SIZE, NSMaxY(iconRect) - ERROR_IMAGE_SIZE, ERROR_IMAGE_SIZE, ERROR_IMAGE_SIZE);
        [errorImage drawInRect: errorRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
    }

    //text color
    NSColor * titleColor, * statusColor;
    if ([self backgroundStyle] == NSBackgroundStyleDark)
        titleColor = statusColor = [NSColor whiteColor];
    else
    {
        titleColor = [NSColor labelColor];
        statusColor = [NSColor secondaryLabelColor];
    }

    fTitleAttributes[NSForegroundColorAttributeName] = titleColor;
    fStatusAttributes[NSForegroundColorAttributeName] = statusColor;

    //minimal status
    CGFloat minimalTitleRightBound;
    if (minimal)
    {
        NSAttributedString * minimalString = [self attributedStatusString: [self minimalStatusString]];
        NSRect minimalStatusRect = [self rectForMinimalStatusWithString: minimalString inBounds: cellFrame];

        if (!fHover)
            [minimalString drawInRect: minimalStatusRect];

        minimalTitleRightBound = NSMinX(minimalStatusRect);
    }

    //progress
    if (!minimal)
    {
        NSAttributedString * progressString = [self attributedStatusString: [torrent progressString]];
        NSRect progressRect = [self rectForProgressWithStringInBounds: cellFrame];

        [progressString drawInRect: progressRect];
    }

    if (!minimal || fHover)
    {
        //control button
        NSString * controlImageSuffix;
        if (fMouseDownControlButton)
            controlImageSuffix = @"On";
        else if (!fTracking && fHoverControl)
            controlImageSuffix = @"Hover";
        else
            controlImageSuffix = @"Off";

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

        const NSRect controlRect = [self controlButtonRectForBounds: cellFrame];
        [controlImage drawInRect: controlRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
        minimalTitleRightBound = MIN(minimalTitleRightBound, NSMinX(controlRect));

        //reveal button
        NSString * revealImageString;
        if (fMouseDownRevealButton)
            revealImageString = @"RevealOn";
        else if (!fTracking && fHoverReveal)
            revealImageString = @"RevealHover";
        else
            revealImageString = @"RevealOff";

        NSImage * revealImage = [NSImage imageNamed: revealImageString];
        [revealImage drawInRect: [self revealButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];

        //action button
        #warning image should use new gear
        NSString * actionImageString;
        if (fMouseDownActionButton)
            #warning we can get rid of this on 10.7
            actionImageString = @"ActionOn";
        else if (!fTracking && fHoverAction)
            actionImageString = @"ActionHover";
        else
            actionImageString = nil;

        if (actionImageString)
        {
            NSImage * actionImage = [NSImage imageNamed: actionImageString];
            [actionImage drawInRect: [self actionButtonRectForBounds: cellFrame] fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
        }
    }

    //title
    NSAttributedString * titleString = [self attributedTitle];
    NSRect titleRect = [self rectForTitleWithString: titleString withRightBound: minimalTitleRightBound inBounds: cellFrame];
    [titleString drawInRect: titleRect];

    //priority icon
    if ([torrent priority] != TR_PRI_NORMAL)
    {
        const NSRect priorityRect = NSMakeRect(NSMaxX(titleRect) + PADDING_BETWEEN_TITLE_AND_PRIORITY,
                                               NSMidY(titleRect) - PRIORITY_ICON_HEIGHT  * 0.5,
                                               PRIORITY_ICON_WIDTH, PRIORITY_ICON_HEIGHT);

        NSColor * priorityColor = [self backgroundStyle] == NSBackgroundStyleDark ? [NSColor whiteColor] : [NSColor labelColor];
        NSImage * priorityImage = [[NSImage imageNamed: ([torrent priority] == TR_PRI_HIGH ? @"PriorityHighTemplate" : @"PriorityLowTemplate")] imageWithColor: priorityColor];
        [priorityImage drawInRect: priorityRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
    }

    //status
    if (!minimal)
    {
        NSAttributedString * statusString = [self attributedStatusString: [self statusString]];
        [statusString drawInRect: [self rectForStatusWithStringInBounds: cellFrame]];
    }
}

- (NSRect) expansionFrameWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];

    //this code needs to match the code in drawInteriorWithFrame:withView:
    CGFloat minimalTitleRightBound;
    if (minimal)
    {
        NSAttributedString * minimalString = [self attributedStatusString: [self minimalStatusString]];
        NSRect minimalStatusRect = [self rectForMinimalStatusWithString: minimalString inBounds: cellFrame];

        minimalTitleRightBound = NSMinX(minimalStatusRect);
    }

    if (!minimal || fHover)
    {
        const NSRect controlRect = [self controlButtonRectForBounds: cellFrame];
        minimalTitleRightBound = MIN(minimalTitleRightBound, NSMinX(controlRect));
    }

    NSAttributedString * titleString = [self attributedTitle];
    NSRect realRect = [self rectForTitleWithString: titleString withRightBound: minimalTitleRightBound inBounds: cellFrame];

    NSAssert([titleString size].width >= NSWidth(realRect), @"Full rect width should not be less than the used title rect width!");

    if ([titleString size].width > NSWidth(realRect)
        && NSMouseInRect([view convertPoint: [[view window] mouseLocationOutsideOfEventStream] fromView: nil], realRect, [view isFlipped]))
    {
        realRect.size.width = [titleString size].width;
        return NSInsetRect(realRect, -PADDING_EXPANSION_FRAME, -PADDING_EXPANSION_FRAME);
    }

    return NSZeroRect;
}

- (void) drawWithExpansionFrame: (NSRect) cellFrame inView: (NSView *)view
{
    cellFrame.origin.x += PADDING_EXPANSION_FRAME;
    cellFrame.origin.y += PADDING_EXPANSION_FRAME;

    fTitleAttributes[NSForegroundColorAttributeName] = [NSColor labelColor];
    NSAttributedString * titleString = [self attributedTitle];
    [titleString drawInRect: cellFrame];
}

@end

@implementation TorrentCell (Private)

- (void) drawBar: (NSRect) barRect
{
    const BOOL minimal = [fDefaults boolForKey: @"SmallView"];

    const CGFloat piecesBarPercent = [(TorrentTableView *)[self controlView] piecesBarPercent];
    if (piecesBarPercent > 0.0)
    {
        NSRect piecesBarRect, regularBarRect;
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floor(NSHeight(barRect) * PIECES_TOTAL_PERCENT * piecesBarPercent),
                    NSMaxYEdge);

        [self drawRegularBar: regularBarRect];
        [self drawPiecesBar: piecesBarRect];
    }
    else
    {
        [[self representedObject] setPreviousFinishedPieces: nil];

        [self drawRegularBar: barRect];
    }

    NSColor * borderColor = minimal ? fBarMinimalBorderColor : fBarBorderColor;
    [borderColor set];
    [NSBezierPath strokeRect: NSInsetRect(barRect, 0.5, 0.5)];
}

- (void) drawRegularBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];

    NSRect haveRect, missingRect;
    NSDivideRect(barRect, &haveRect, &missingRect, round([torrent progress] * NSWidth(barRect)), NSMinXEdge);

    if (!NSIsEmptyRect(haveRect))
    {
        if ([torrent isActive])
        {
            if ([torrent isChecking])
                [[ProgressGradients progressYellowGradient] drawInRect: haveRect angle: 90];
            else if ([torrent isSeeding])
            {
                NSRect ratioHaveRect, ratioRemainingRect;
                NSDivideRect(haveRect, &ratioHaveRect, &ratioRemainingRect, round([torrent progressStopRatio] * NSWidth(haveRect)),
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

    if (![torrent allDownloaded])
    {
        const CGFloat widthRemaining = round(NSWidth(barRect) * [torrent progressLeft]);

        NSRect wantedRect;
        NSDivideRect(missingRect, &wantedRect, &missingRect, widthRemaining, NSMinXEdge);

        //not-available section
        if ([torrent isActive] && ![torrent isChecking] && [torrent availableDesired] < 1.0
            && [fDefaults boolForKey: @"DisplayProgressBarAvailable"])
        {
            NSRect unavailableRect;
            NSDivideRect(wantedRect, &wantedRect, &unavailableRect, round(NSWidth(wantedRect) * [torrent availableDesired]),
                        NSMinXEdge);

            [[ProgressGradients progressRedGradient] drawInRect: unavailableRect angle: 90];
        }

        //remaining section
        [[ProgressGradients progressWhiteGradient] drawInRect: wantedRect angle: 90];
    }

    //unwanted section
    if (!NSIsEmptyRect(missingRect))
    {
        if (![torrent isMagnet])
            [[ProgressGradients progressLightGrayGradient] drawInRect: missingRect angle: 90];
        else
            [[ProgressGradients progressRedGradient] drawInRect: missingRect angle: 90];
    }
}

- (void) drawPiecesBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];

    //fill an all-white bar for magnet links
    if ([torrent isMagnet])
    {
        [[NSColor colorWithCalibratedWhite: 1.0 alpha: [fDefaults boolForKey: @"SmallView"] ? 0.25 : 1.0] set];
        NSRectFillUsingOperation(barRect, NSCompositeSourceOver);
        return;
    }

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
    [bitmap drawInRect: barRect fromRect: NSZeroRect operation: NSCompositeSourceOver
        fraction: ([fDefaults boolForKey: @"SmallView"] ? 0.25 : 1.0) respectFlipped: YES hints: nil];

}

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    NSRect result;
    result.size = [string size];

    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NSWidth(result));
    result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);

    return result;
}

- (NSRect) rectForTitleWithString: (NSAttributedString *) string withRightBound: (CGFloat) rightBound inBounds: (NSRect) bounds
{
    const BOOL minimal = [fDefaults boolForKey: @"SmallView"];

    NSRect result;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL
                        + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    result.size.height = HEIGHT_TITLE;

    if (minimal)
    {
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);
        result.size.width = rightBound - NSMinX(result) - PADDING_BETWEEN_TITLE_AND_MIN_STATUS;
    }
    else
    {
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE;
        result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL;
    }

    if ([(Torrent *)[self representedObject] priority] != TR_PRI_NORMAL)
        result.size.width -= PRIORITY_ICON_WIDTH + PADDING_BETWEEN_TITLE_AND_PRIORITY;
    result.size.width = MIN(NSWidth(result), [string size].width);

    return result;
}

- (NSRect) rectForProgressWithStringInBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;

    result.size.height = HEIGHT_STATUS;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL;

    return result;
}

- (NSRect) rectForStatusWithStringInBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS
                        + PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;

    result.size.height = HEIGHT_STATUS;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL;

    return result;
}

- (NSRect) barRectRegForBounds: (NSRect) bounds
{
    NSRect result;
    result.size.height = BAR_HEIGHT;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_BAR;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS
                        + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;

    result.size.width = floor(NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL
                        - 2.0 * (PADDING_BETWEEN_BUTTONS + NORMAL_BUTTON_WIDTH));

    return result;
}

- (NSRect) barRectMinForBounds: (NSRect) bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_MIN + PADDING_BETWEEN_IMAGE_AND_BAR;
    result.origin.y = NSMinY(bounds) + PADDING_BETWEEN_BAR_AND_EDGE_MIN;
    result.size.height = NSHeight(bounds) - 2.0 * PADDING_BETWEEN_BAR_AND_EDGE_MIN;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_BETWEEN_BAR_AND_EDGE_MIN;

    return result;
}

- (NSRect) controlButtonRectForBounds: (NSRect) bounds
{
    NSRect result;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH + PADDING_BETWEEN_BUTTONS + NORMAL_BUTTON_WIDTH);

    if (![fDefaults boolForKey: @"SmallView"])
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5
                            + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    else
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);

    return result;
}

- (NSRect) revealButtonRectForBounds: (NSRect) bounds
{
    NSRect result;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH);

    if (![fDefaults boolForKey: @"SmallView"])
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5
                            + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    else
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);

    return result;
}

- (NSRect) actionButtonRectForBounds: (NSRect) bounds
{
    const NSRect iconRect = [self iconRectForBounds: bounds];

    //in minimal view the rect will be the icon rect, but avoid the extra defaults lookup with some cheap math
    return NSMakeRect(NSMidX(iconRect) - ACTION_BUTTON_WIDTH * 0.5, NSMidY(iconRect) - ACTION_BUTTON_WIDTH * 0.5,
                        ACTION_BUTTON_WIDTH, ACTION_BUTTON_WIDTH);
}

- (NSAttributedString *) attributedTitle
{
    NSString * title = [(Torrent *)[self representedObject] name];
    return [[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes];
}

- (NSAttributedString *) attributedStatusString: (NSString *) string
{
    return [[NSAttributedString alloc] initWithString: string attributes: fStatusAttributes];
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
    Torrent * torrent = [self representedObject];
    return [fDefaults boolForKey: @"DisplaySmallStatusRegular"] ? [torrent shortStatusString] : [torrent remainingTimeString];
}

@end
