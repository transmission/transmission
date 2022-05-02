// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

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

#define MAX_PIECES (18 * 18)

@interface TorrentCell ()

- (void)drawBar:(NSRect)barRect;
- (void)drawRegularBar:(NSRect)barRect;
- (void)drawPiecesBar:(NSRect)barRect;

- (NSRect)rectForMinimalStatusWithString:(NSAttributedString*)string inBounds:(NSRect)bounds;
- (NSRect)rectForTitleWithString:(NSAttributedString*)string
                  withRightBound:(CGFloat)rightBound
                        inBounds:(NSRect)bounds
                         minimal:(BOOL)minimal;
- (NSRect)rectForProgressWithStringInBounds:(NSRect)bounds;
- (NSRect)rectForStatusWithStringInBounds:(NSRect)bounds;
- (NSRect)barRectRegForBounds:(NSRect)bounds;
- (NSRect)barRectMinForBounds:(NSRect)bounds;

- (NSRect)controlButtonRectForBounds:(NSRect)bounds;
- (NSRect)revealButtonRectForBounds:(NSRect)bounds;
- (NSRect)actionButtonRectForBounds:(NSRect)bounds;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, readonly) NSMutableDictionary* fTitleAttributes;
@property(nonatomic, readonly) NSMutableDictionary* fStatusAttributes;

@property(nonatomic) BOOL fTracking;
@property(nonatomic) BOOL fMouseDownControlButton;
@property(nonatomic) BOOL fMouseDownRevealButton;

@property(nonatomic, readonly) NSColor* fBarBorderColor;
@property(nonatomic, readonly) NSColor* fBluePieceColor;
@property(nonatomic, readonly) NSColor* fBarMinimalBorderColor;

@property(nonatomic, readonly) NSAttributedString* attributedTitle;
- (NSAttributedString*)attributedStatusString:(NSString*)string;

@property(nonatomic, readonly) NSString* buttonString;
@property(nonatomic, readonly) NSString* statusString;
@property(nonatomic, readonly) NSString* minimalStatusString;

@end

@implementation TorrentCell

//only called once and the main table is always needed, so don't worry about releasing
- (instancetype)init
{
    if ((self = [super init]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        paragraphStyle.lineBreakMode = NSLineBreakByTruncatingMiddle;

        _fTitleAttributes = [[NSMutableDictionary alloc] initWithCapacity:3];
        _fTitleAttributes[NSFontAttributeName] = [NSFont messageFontOfSize:12.0];
        _fTitleAttributes[NSParagraphStyleAttributeName] = paragraphStyle;

        _fStatusAttributes = [[NSMutableDictionary alloc] initWithCapacity:3];
        _fStatusAttributes[NSFontAttributeName] = [NSFont messageFontOfSize:9.0];
        _fStatusAttributes[NSParagraphStyleAttributeName] = paragraphStyle;

        _fBluePieceColor = [NSColor colorWithCalibratedRed:0.0 green:0.4 blue:0.8 alpha:1.0];
        _fBarBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.2];
        _fBarMinimalBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.015];
    }
    return self;
}

- (id)copyWithZone:(NSZone*)zone
{
    id value = [super copyWithZone:zone];
    [value setRepresentedObject:self.representedObject];
    return value;
}

- (NSRect)iconRectForBounds:(NSRect)bounds
{
    CGFloat const imageSize = [self.fDefaults boolForKey:@"SmallView"] ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG;

    return NSMakeRect(NSMinX(bounds) + PADDING_HORIZONTAL, ceil(NSMidY(bounds) - imageSize * 0.5), imageSize, imageSize);
}

- (NSCellHitResult)hitTestForEvent:(NSEvent*)event inRect:(NSRect)cellFrame ofView:(NSView*)controlView
{
    NSPoint point = [controlView convertPoint:event.locationInWindow fromView:nil];

    if (NSMouseInRect(point, [self controlButtonRectForBounds:cellFrame], controlView.flipped) ||
        NSMouseInRect(point, [self revealButtonRectForBounds:cellFrame], controlView.flipped))
    {
        return NSCellHitContentArea | NSCellHitTrackableArea;
    }

    return NSCellHitContentArea;
}

+ (BOOL)prefersTrackingUntilMouseUp
{
    return YES;
}

- (BOOL)trackMouse:(NSEvent*)event inRect:(NSRect)cellFrame ofView:(NSView*)controlView untilMouseUp:(BOOL)flag
{
    self.fTracking = YES;

    self.controlView = controlView;

    NSPoint point = [controlView convertPoint:event.locationInWindow fromView:nil];

    NSRect const controlRect = [self controlButtonRectForBounds:cellFrame];
    BOOL const checkControl = NSMouseInRect(point, controlRect, controlView.flipped);

    NSRect const revealRect = [self revealButtonRectForBounds:cellFrame];
    BOOL const checkReveal = NSMouseInRect(point, revealRect, controlView.flipped);

    [(TorrentTableView*)controlView removeTrackingAreas];

    while (event.type != NSEventTypeLeftMouseUp)
    {
        point = [controlView convertPoint:event.locationInWindow fromView:nil];

        if (checkControl)
        {
            BOOL const inControlButton = NSMouseInRect(point, controlRect, controlView.flipped);
            if (self.fMouseDownControlButton != inControlButton)
            {
                self.fMouseDownControlButton = inControlButton;
                [controlView setNeedsDisplayInRect:cellFrame];
            }
        }
        else if (checkReveal)
        {
            BOOL const inRevealButton = NSMouseInRect(point, revealRect, controlView.flipped);
            if (self.fMouseDownRevealButton != inRevealButton)
            {
                self.fMouseDownRevealButton = inRevealButton;
                [controlView setNeedsDisplayInRect:cellFrame];
            }
        }

        //send events to where necessary
        if (event.type == NSEventTypeMouseEntered || event.type == NSEventTypeMouseExited)
        {
            [NSApp sendEvent:event];
        }
        event = [controlView.window nextEventMatchingMask:(NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged |
                                                           NSEventMaskMouseEntered | NSEventMaskMouseExited)];
    }

    self.fTracking = NO;

    if (self.fMouseDownControlButton)
    {
        self.fMouseDownControlButton = NO;

        [(TorrentTableView*)controlView toggleControlForTorrent:self.representedObject];
    }
    else if (self.fMouseDownRevealButton)
    {
        self.fMouseDownRevealButton = NO;
        [controlView setNeedsDisplayInRect:cellFrame];

        NSString* location = ((Torrent*)self.representedObject).dataLocation;
        if (location)
        {
            NSURL* file = [NSURL fileURLWithPath:location];
            [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ file ]];
        }
    }

    [controlView updateTrackingAreas];

    return YES;
}

- (void)addTrackingAreasForView:(NSView*)controlView
                         inRect:(NSRect)cellFrame
                   withUserInfo:(NSDictionary*)userInfo
                  mouseLocation:(NSPoint)mouseLocation
{
    NSTrackingAreaOptions const options = NSTrackingEnabledDuringMouseDrag | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways;

    //whole row
    if ([self.fDefaults boolForKey:@"SmallView"])
    {
        NSTrackingAreaOptions rowOptions = options;
        if (NSMouseInRect(mouseLocation, cellFrame, controlView.flipped))
        {
            rowOptions |= NSTrackingAssumeInside;
            ((TorrentTableView*)controlView).hoverRow = [userInfo[@"Row"] integerValue];
        }

        NSMutableDictionary* rowInfo = [userInfo mutableCopy];
        rowInfo[@"Type"] = @"Row";
        NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:cellFrame options:rowOptions owner:controlView userInfo:rowInfo];
        [controlView addTrackingArea:area];
    }

    //control button
    NSRect controlButtonRect = [self controlButtonRectForBounds:cellFrame];
    NSTrackingAreaOptions controlOptions = options;
    if (NSMouseInRect(mouseLocation, controlButtonRect, controlView.flipped))
    {
        controlOptions |= NSTrackingAssumeInside;
        ((TorrentTableView*)controlView).controlButtonHoverRow = [userInfo[@"Row"] integerValue];
    }

    NSMutableDictionary* controlInfo = [userInfo mutableCopy];
    controlInfo[@"Type"] = @"Control";
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:controlButtonRect options:controlOptions owner:controlView
                                                       userInfo:controlInfo];
    [controlView addTrackingArea:area];

    //reveal button
    NSRect revealButtonRect = [self revealButtonRectForBounds:cellFrame];
    NSTrackingAreaOptions revealOptions = options;
    if (NSMouseInRect(mouseLocation, revealButtonRect, controlView.flipped))
    {
        revealOptions |= NSTrackingAssumeInside;
        ((TorrentTableView*)controlView).revealButtonHoverRow = [userInfo[@"Row"] integerValue];
    }

    NSMutableDictionary* revealInfo = [userInfo mutableCopy];
    revealInfo[@"Type"] = @"Reveal";
    area = [[NSTrackingArea alloc] initWithRect:revealButtonRect options:revealOptions owner:controlView userInfo:revealInfo];
    [controlView addTrackingArea:area];

    //action button
    NSRect actionButtonRect = [self iconRectForBounds:cellFrame]; //use the whole icon
    NSTrackingAreaOptions actionOptions = options;
    if (NSMouseInRect(mouseLocation, actionButtonRect, controlView.flipped))
    {
        actionOptions |= NSTrackingAssumeInside;
        ((TorrentTableView*)controlView).actionButtonHoverRow = [userInfo[@"Row"] integerValue];
    }

    NSMutableDictionary* actionInfo = [userInfo mutableCopy];
    actionInfo[@"Type"] = @"Action";
    area = [[NSTrackingArea alloc] initWithRect:actionButtonRect options:actionOptions owner:controlView userInfo:actionInfo];
    [controlView addTrackingArea:area];
}

- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    Torrent* torrent = self.representedObject;
    NSAssert(torrent != nil, @"can't have a TorrentCell without a Torrent");

    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];

    //bar
    [self drawBar:minimal ? [self barRectMinForBounds:cellFrame] : [self barRectRegForBounds:cellFrame]];

    //group coloring
    NSRect const iconRect = [self iconRectForBounds:cellFrame];

    NSInteger const groupValue = torrent.groupValue;
    if (groupValue != -1)
    {
        NSRect groupRect = NSInsetRect(iconRect, -1.0, -2.0);
        if (!minimal)
        {
            groupRect.size.height -= 1.0;
            groupRect.origin.y -= 1.0;
        }
        CGFloat const radius = minimal ? 3.0 : 6.0;

        NSColor *groupColor = [GroupsController.groups colorForIndex:groupValue],
                *darkGroupColor = [groupColor blendedColorWithFraction:0.2 ofColor:NSColor.whiteColor];

        //border
        NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:groupRect xRadius:radius yRadius:radius];
        [darkGroupColor set];
        bp.lineWidth = 2.0;
        [bp stroke];

        //inside
        bp = [NSBezierPath bezierPathWithRoundedRect:groupRect xRadius:radius yRadius:radius];
        NSGradient* gradient = [[NSGradient alloc] initWithStartingColor:[groupColor blendedColorWithFraction:0.7
                                                                                                      ofColor:NSColor.whiteColor]
                                                             endingColor:darkGroupColor];
        [gradient drawInBezierPath:bp angle:90.0];
    }

    BOOL const error = torrent.anyErrorOrWarning;

    //icon
    if (!minimal || !(!self.fTracking && self.hoverAction)) //don't show in minimal mode when hovered over
    {
        NSImage* icon = (minimal && error) ? [NSImage imageNamed:NSImageNameCaution] : torrent.icon;
        [icon drawInRect:iconRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0 respectFlipped:YES
                     hints:nil];
    }

    //error badge
    if (error && !minimal)
    {
        NSImage* errorImage = [NSImage imageNamed:NSImageNameCaution];
        NSRect const errorRect = NSMakeRect(NSMaxX(iconRect) - ERROR_IMAGE_SIZE, NSMaxY(iconRect) - ERROR_IMAGE_SIZE, ERROR_IMAGE_SIZE, ERROR_IMAGE_SIZE);
        [errorImage drawInRect:errorRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                respectFlipped:YES
                         hints:nil];
    }

    //text color
    NSColor *titleColor, *statusColor;
    if (self.backgroundStyle == NSBackgroundStyleEmphasized)
    {
        titleColor = statusColor = NSColor.whiteColor;
    }
    else
    {
        titleColor = NSColor.labelColor;
        statusColor = NSColor.secondaryLabelColor;
    }

    self.fTitleAttributes[NSForegroundColorAttributeName] = titleColor;
    self.fStatusAttributes[NSForegroundColorAttributeName] = statusColor;

    CGFloat titleRightBound;
    //minimal status
    if (minimal)
    {
        NSAttributedString* minimalString = [self attributedStatusString:self.minimalStatusString];
        NSRect minimalStatusRect = [self rectForMinimalStatusWithString:minimalString inBounds:cellFrame];

        if (!self.hover)
        {
            [minimalString drawInRect:minimalStatusRect];
        }

        titleRightBound = NSMinX(minimalStatusRect);
    }
    //progress
    else
    {
        NSAttributedString* progressString = [self attributedStatusString:torrent.progressString];
        NSRect progressRect = [self rectForProgressWithStringInBounds:cellFrame];

        [progressString drawInRect:progressRect];
        titleRightBound = NSMaxX(cellFrame);
    }

    if (!minimal || self.hover)
    {
        //control button
        NSString* controlImageSuffix;
        if (self.fMouseDownControlButton)
        {
            controlImageSuffix = @"On";
        }
        else if (!self.fTracking && self.hoverControl)
        {
            controlImageSuffix = @"Hover";
        }
        else
        {
            controlImageSuffix = @"Off";
        }

        NSImage* controlImage;
        if (torrent.active)
        {
            controlImage = [NSImage imageNamed:[@"Pause" stringByAppendingString:controlImageSuffix]];
        }
        else
        {
            if (NSApp.currentEvent.modifierFlags & NSEventModifierFlagOption)
            {
                controlImage = [NSImage imageNamed:[@"ResumeNoWait" stringByAppendingString:controlImageSuffix]];
            }
            else if (torrent.waitingToStart)
            {
                controlImage = [NSImage imageNamed:[@"Pause" stringByAppendingString:controlImageSuffix]];
            }
            else
            {
                controlImage = [NSImage imageNamed:[@"Resume" stringByAppendingString:controlImageSuffix]];
            }
        }

        NSRect const controlRect = [self controlButtonRectForBounds:cellFrame];
        [controlImage drawInRect:controlRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                  respectFlipped:YES
                           hints:nil];
        if (minimal)
        {
            titleRightBound = MIN(titleRightBound, NSMinX(controlRect));
        }

        //reveal button
        NSString* revealImageString;
        if (self.fMouseDownRevealButton)
        {
            revealImageString = @"RevealOn";
        }
        else if (!self.fTracking && self.hoverReveal)
        {
            revealImageString = @"RevealHover";
        }
        else
        {
            revealImageString = @"RevealOff";
        }

        NSImage* revealImage = [NSImage imageNamed:revealImageString];
        [revealImage drawInRect:[self revealButtonRectForBounds:cellFrame] fromRect:NSZeroRect
                      operation:NSCompositingOperationSourceOver
                       fraction:1.0
                 respectFlipped:YES
                          hints:nil];

        //action button
#warning image should use new gear
        if (!self.fTracking && self.hoverAction)
        {
            NSImage* actionImage = [NSImage imageNamed:@"ActionHover"];
            [actionImage drawInRect:[self actionButtonRectForBounds:cellFrame] fromRect:NSZeroRect
                          operation:NSCompositingOperationSourceOver
                           fraction:1.0
                     respectFlipped:YES
                              hints:nil];
        }
    }

    //title
    NSAttributedString* titleString = self.attributedTitle;
    NSRect titleRect = [self rectForTitleWithString:titleString withRightBound:titleRightBound inBounds:cellFrame minimal:minimal];
    [titleString drawInRect:titleRect];

    //priority icon
    if (torrent.priority != TR_PRI_NORMAL)
    {
        NSRect const priorityRect = NSMakeRect(
            NSMaxX(titleRect) + PADDING_BETWEEN_TITLE_AND_PRIORITY,
            NSMidY(titleRect) - PRIORITY_ICON_HEIGHT * 0.5,
            PRIORITY_ICON_WIDTH,
            PRIORITY_ICON_HEIGHT);

        NSColor* priorityColor = self.backgroundStyle == NSBackgroundStyleEmphasized ? NSColor.whiteColor : NSColor.labelColor;

        NSImage* priorityImage = [[NSImage imageNamed:(torrent.priority == TR_PRI_HIGH ? @"PriorityHighTemplate" : @"PriorityLowTemplate")]
            imageWithColor:priorityColor];
        [priorityImage drawInRect:priorityRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                   respectFlipped:YES
                            hints:nil];
    }

    //status
    if (!minimal)
    {
        NSAttributedString* statusString = [self attributedStatusString:self.statusString];
        [statusString drawInRect:[self rectForStatusWithStringInBounds:cellFrame]];
    }
}

- (NSRect)expansionFrameWithFrame:(NSRect)cellFrame inView:(NSView*)view
{
    BOOL minimal = [self.fDefaults boolForKey:@"SmallView"];

    //this code needs to match the code in drawInteriorWithFrame:withView:
    CGFloat titleRightBound;
    if (minimal)
    {
        NSAttributedString* minimalString = [self attributedStatusString:self.minimalStatusString];
        NSRect minimalStatusRect = [self rectForMinimalStatusWithString:minimalString inBounds:cellFrame];

        titleRightBound = NSMinX(minimalStatusRect);

        if (self.hover)
        {
            NSRect const controlRect = [self controlButtonRectForBounds:cellFrame];
            titleRightBound = MIN(titleRightBound, NSMinX(controlRect));
        }
    }
    else
    {
        titleRightBound = NSMaxX(cellFrame);
    }

    NSAttributedString* titleString = self.attributedTitle;
    NSRect realRect = [self rectForTitleWithString:titleString withRightBound:titleRightBound inBounds:cellFrame minimal:minimal];

    NSAssert([titleString size].width >= NSWidth(realRect), @"Full rect width should not be less than the used title rect width!");

    if ([titleString size].width > NSWidth(realRect) &&
        NSMouseInRect([view convertPoint:view.window.mouseLocationOutsideOfEventStream fromView:nil], realRect, view.flipped))
    {
        realRect.size.width = [titleString size].width;
        return NSInsetRect(realRect, -PADDING_EXPANSION_FRAME, -PADDING_EXPANSION_FRAME);
    }

    return NSZeroRect;
}

- (void)drawWithExpansionFrame:(NSRect)cellFrame inView:(NSView*)view
{
    cellFrame.origin.x += PADDING_EXPANSION_FRAME;
    cellFrame.origin.y += PADDING_EXPANSION_FRAME;

    self.fTitleAttributes[NSForegroundColorAttributeName] = NSColor.labelColor;
    NSAttributedString* titleString = self.attributedTitle;
    [titleString drawInRect:cellFrame];
}

#pragma mark - Private

- (void)drawBar:(NSRect)barRect
{
    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];

    CGFloat const piecesBarPercent = ((TorrentTableView*)self.controlView).piecesBarPercent;
    if (piecesBarPercent > 0.0)
    {
        NSRect piecesBarRect, regularBarRect;
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floor(NSHeight(barRect) * PIECES_TOTAL_PERCENT * piecesBarPercent), NSMaxYEdge);

        [self drawRegularBar:regularBarRect];
        [self drawPiecesBar:piecesBarRect];
    }
    else
    {
        ((Torrent*)self.representedObject).previousFinishedPieces = nil;

        [self drawRegularBar:barRect];
    }

    NSColor* borderColor = minimal ? self.fBarMinimalBorderColor : self.fBarBorderColor;
    [borderColor set];
    [NSBezierPath strokeRect:NSInsetRect(barRect, 0.5, 0.5)];
}

- (void)drawRegularBar:(NSRect)barRect
{
    Torrent* torrent = self.representedObject;

    NSRect haveRect, missingRect;
    NSDivideRect(barRect, &haveRect, &missingRect, round(torrent.progress * NSWidth(barRect)), NSMinXEdge);

    if (!NSIsEmptyRect(haveRect))
    {
        if (torrent.active)
        {
            if (torrent.checking)
            {
                [ProgressGradients.progressYellowGradient drawInRect:haveRect angle:90];
            }
            else if (torrent.seeding)
            {
                NSRect ratioHaveRect, ratioRemainingRect;
                NSDivideRect(haveRect, &ratioHaveRect, &ratioRemainingRect, round(torrent.progressStopRatio * NSWidth(haveRect)), NSMinXEdge);

                [ProgressGradients.progressGreenGradient drawInRect:ratioHaveRect angle:90];
                [ProgressGradients.progressLightGreenGradient drawInRect:ratioRemainingRect angle:90];
            }
            else
            {
                [ProgressGradients.progressBlueGradient drawInRect:haveRect angle:90];
            }
        }
        else
        {
            if (torrent.waitingToStart)
            {
                if (torrent.allDownloaded)
                {
                    [ProgressGradients.progressDarkGreenGradient drawInRect:haveRect angle:90];
                }
                else
                {
                    [ProgressGradients.progressDarkBlueGradient drawInRect:haveRect angle:90];
                }
            }
            else
            {
                [ProgressGradients.progressGrayGradient drawInRect:haveRect angle:90];
            }
        }
    }

    if (!torrent.allDownloaded)
    {
        CGFloat const widthRemaining = round(NSWidth(barRect) * torrent.progressLeft);

        NSRect wantedRect;
        NSDivideRect(missingRect, &wantedRect, &missingRect, widthRemaining, NSMinXEdge);

        //not-available section
        if (torrent.active && !torrent.checking && torrent.availableDesired < 1.0 && [self.fDefaults boolForKey:@"DisplayProgressBarAvailable"])
        {
            NSRect unavailableRect;
            NSDivideRect(wantedRect, &wantedRect, &unavailableRect, round(NSWidth(wantedRect) * torrent.availableDesired), NSMinXEdge);

            [ProgressGradients.progressRedGradient drawInRect:unavailableRect angle:90];
        }

        //remaining section
        [ProgressGradients.progressWhiteGradient drawInRect:wantedRect angle:90];
    }

    //unwanted section
    if (!NSIsEmptyRect(missingRect))
    {
        if (!torrent.magnet)
        {
            [ProgressGradients.progressLightGrayGradient drawInRect:missingRect angle:90];
        }
        else
        {
            [ProgressGradients.progressRedGradient drawInRect:missingRect angle:90];
        }
    }
}

- (void)drawPiecesBar:(NSRect)barRect
{
    Torrent* torrent = self.representedObject;

    //fill an all-white bar for magnet links
    if (torrent.magnet)
    {
        [[NSColor colorWithCalibratedWhite:1.0 alpha:[self.fDefaults boolForKey:@"SmallView"] ? 0.25 : 1.0] set];
        NSRectFillUsingOperation(barRect, NSCompositingOperationSourceOver);
        return;
    }

    NSInteger pieceCount = MIN(torrent.pieceCount, MAX_PIECES);
    float* piecesPercent = static_cast<float*>(malloc(pieceCount * sizeof(float)));
    [torrent getAmountFinished:piecesPercent size:pieceCount];

    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:pieceCount pixelsHigh:1
                                                                    bitsPerSample:8
                                                                  samplesPerPixel:4
                                                                         hasAlpha:YES
                                                                         isPlanar:NO
                                                                   colorSpaceName:NSCalibratedRGBColorSpace
                                                                      bytesPerRow:0
                                                                     bitsPerPixel:0];

    NSIndexSet* previousFinishedIndexes = torrent.previousFinishedPieces;
    NSMutableIndexSet* finishedIndexes = [NSMutableIndexSet indexSet];

    for (NSInteger i = 0; i < pieceCount; i++)
    {
        NSColor* pieceColor;
        if (piecesPercent[i] == 1.0f)
        {
            if (previousFinishedIndexes && ![previousFinishedIndexes containsIndex:i])
            {
                pieceColor = NSColor.orangeColor;
            }
            else
            {
                pieceColor = self.fBluePieceColor;
            }
            [finishedIndexes addIndex:i];
        }
        else
        {
            pieceColor = [NSColor.whiteColor blendedColorWithFraction:piecesPercent[i] ofColor:self.fBluePieceColor];
        }

        //it's faster to just set color instead of checking previous color
        [bitmap setColor:pieceColor atX:i y:0];
    }

    free(piecesPercent);

    torrent.previousFinishedPieces = finishedIndexes.count > 0 ? finishedIndexes : nil; //don't bother saving if none are complete

    //actually draw image
    [bitmap drawInRect:barRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
              fraction:([self.fDefaults boolForKey:@"SmallView"] ? 0.25 : 1.0)respectFlipped:YES
                 hints:nil];
}

- (NSRect)rectForMinimalStatusWithString:(NSAttributedString*)string inBounds:(NSRect)bounds
{
    NSRect result;
    result.size = [string size];

    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NSWidth(result));
    result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);

    return result;
}

- (NSRect)rectForTitleWithString:(NSAttributedString*)string
                  withRightBound:(CGFloat)rightBound
                        inBounds:(NSRect)bounds
                         minimal:(BOOL)minimal
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    result.size.height = HEIGHT_TITLE;

    if (minimal)
    {
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);
        result.size.width = rightBound - NSMinX(result) - PADDING_BETWEEN_TITLE_AND_MIN_STATUS;
    }
    else
    {
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE;
        result.size.width = rightBound - NSMinX(result) - PADDING_HORIZONTAL;
    }

    if (((Torrent*)self.representedObject).priority != TR_PRI_NORMAL)
    {
        result.size.width -= PRIORITY_ICON_WIDTH + PADDING_BETWEEN_TITLE_AND_PRIORITY;
    }
    result.size.width = MIN(NSWidth(result), [string size].width);

    return result;
}

- (NSRect)rectForProgressWithStringInBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;

    result.size.height = HEIGHT_STATUS;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL;

    return result;
}

- (NSRect)rectForStatusWithStringInBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS +
        PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;

    result.size.height = HEIGHT_STATUS;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL;

    return result;
}

- (NSRect)barRectRegForBounds:(NSRect)bounds
{
    NSRect result;
    result.size.height = BAR_HEIGHT;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_BAR;
    result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS +
        PADDING_BETWEEN_PROGRESS_AND_BAR;

    result.size.width = floor(NSMaxX(bounds) - NSMinX(result) - PADDING_HORIZONTAL - 2.0 * (PADDING_BETWEEN_BUTTONS + NORMAL_BUTTON_WIDTH));

    return result;
}

- (NSRect)barRectMinForBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + PADDING_HORIZONTAL + IMAGE_SIZE_MIN + PADDING_BETWEEN_IMAGE_AND_BAR;
    result.origin.y = NSMinY(bounds) + PADDING_BETWEEN_BAR_AND_EDGE_MIN;
    result.size.height = NSHeight(bounds) - 2.0 * PADDING_BETWEEN_BAR_AND_EDGE_MIN;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - PADDING_BETWEEN_BAR_AND_EDGE_MIN;

    return result;
}

- (NSRect)controlButtonRectForBounds:(NSRect)bounds
{
    NSRect result;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH + PADDING_BETWEEN_BUTTONS + NORMAL_BUTTON_WIDTH);

    if (![self.fDefaults boolForKey:@"SmallView"])
    {
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5 +
            PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    }
    else
    {
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);
    }

    return result;
}

- (NSRect)revealButtonRectForBounds:(NSRect)bounds
{
    NSRect result;
    result.size.height = NORMAL_BUTTON_WIDTH;
    result.size.width = NORMAL_BUTTON_WIDTH;
    result.origin.x = NSMaxX(bounds) - (PADDING_HORIZONTAL + NORMAL_BUTTON_WIDTH);

    if (![self.fDefaults boolForKey:@"SmallView"])
    {
        result.origin.y = NSMinY(bounds) + PADDING_ABOVE_TITLE + HEIGHT_TITLE - (NORMAL_BUTTON_WIDTH - BAR_HEIGHT) * 0.5 +
            PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    }
    else
    {
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);
    }

    return result;
}

- (NSRect)actionButtonRectForBounds:(NSRect)bounds
{
    NSRect const iconRect = [self iconRectForBounds:bounds];

    //in minimal view the rect will be the icon rect, but avoid the extra defaults lookup with some cheap math
    return NSMakeRect(NSMidX(iconRect) - ACTION_BUTTON_WIDTH * 0.5, NSMidY(iconRect) - ACTION_BUTTON_WIDTH * 0.5, ACTION_BUTTON_WIDTH, ACTION_BUTTON_WIDTH);
}

- (NSAttributedString*)attributedTitle
{
    NSString* title = ((Torrent*)self.representedObject).name;
    return [[NSAttributedString alloc] initWithString:title attributes:self.fTitleAttributes];
}

- (NSAttributedString*)attributedStatusString:(NSString*)string
{
    return [[NSAttributedString alloc] initWithString:string attributes:self.fStatusAttributes];
}

- (NSString*)buttonString
{
    if (self.fMouseDownRevealButton || (!self.fTracking && self.hoverReveal))
    {
        return NSLocalizedString(@"Show the data file in Finder", "Torrent cell -> button info");
    }
    else if (self.fMouseDownControlButton || (!self.fTracking && self.hoverControl))
    {
        Torrent* torrent = self.representedObject;
        if (torrent.active)
            return NSLocalizedString(@"Pause the transfer", "Torrent Table -> tooltip");
        else
        {
            if (NSApp.currentEvent.modifierFlags & NSEventModifierFlagOption)
            {
                return NSLocalizedString(@"Resume the transfer right away", "Torrent cell -> button info");
            }
            else if (torrent.waitingToStart)
            {
                return NSLocalizedString(@"Stop waiting to start", "Torrent cell -> button info");
            }
            else
            {
                return NSLocalizedString(@"Resume the transfer", "Torrent cell -> button info");
            }
        }
    }
    else if (!self.fTracking && self.hoverAction)
    {
        return NSLocalizedString(@"Change transfer settings", "Torrent Table -> tooltip");
    }
    else
    {
        return nil;
    }
}

- (NSString*)statusString
{
    NSString* buttonString;
    if ((buttonString = self.buttonString))
    {
        return buttonString;
    }
    else
    {
        return ((Torrent*)self.representedObject).statusString;
    }
}

- (NSString*)minimalStatusString
{
    Torrent* torrent = self.representedObject;
    return [self.fDefaults boolForKey:@"DisplaySmallStatusRegular"] ? torrent.shortStatusString : torrent.remainingTimeString;
}

@end
