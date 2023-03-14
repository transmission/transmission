// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCell.h"
#import "GroupsController.h"
#import "NSImageAdditions.h"
#import "NSStringAdditions.h"
#import "ProgressGradients.h"
#import "Torrent.h"
#import "TorrentTableView.h"

static CGFloat const kBarHeight = 12.0;

static CGFloat const kImageSizeRegular = 32.0;
static CGFloat const kImageSizeMin = 16.0;
static CGFloat const kErrorImageSize = 20.0;

static CGFloat const kGroupImageSizeRegular = 10.0;
static CGFloat const kGroupImageSizeMin = 6.0;
static CGFloat const kGroupPaddingRegular = 22.0;
static CGFloat const kGroupPaddingMin = 14.0;

static CGFloat const kNormalButtonWidth = 14.0;
static CGFloat const kActionButtonWidth = 16.0;

static CGFloat const kPriorityIconSize = 12.0;

//ends up being larger than font height
static CGFloat const kHeightTitle = 16.0;
static CGFloat const kHeightStatus = 12.0;

static CGFloat const kPaddingHorizontal = 5.0;
static CGFloat const kPaddingEdgeMax = 12.0;
static CGFloat const kPaddingBetweenButtons = 3.0;
static CGFloat const kPaddingBetweenImageAndTitle = kPaddingHorizontal + 1.0;
static CGFloat const kPaddingBetweenImageAndBar = kPaddingHorizontal;
static CGFloat const kPaddingBetweenTitleAndPriority = 6.0;
static CGFloat const kPaddingAboveTitle = 4.0;
static CGFloat const kPaddingBetweenTitleAndMinStatus = 3.0;
static CGFloat const kPaddingBetweenTitleAndProgress = 1.0;
static CGFloat const kPaddingBetweenProgressAndBar = 2.0;
static CGFloat const kPaddingBetweenBarAndStatus = 2.0;
static CGFloat const kPaddingBetweenBarAndEdgeMin = 3.0;
static CGFloat const kPaddingExpansionFrame = 2.0;

static CGFloat const kPiecesTotalPercent = 0.6;

static NSInteger const kMaxPieces = 18 * 18;

@interface TorrentCell ()

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
        NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        paragraphStyle.lineBreakMode = NSLineBreakByTruncatingMiddle;

        _fTitleAttributes = [[NSMutableDictionary alloc] initWithCapacity:3];
        _fTitleAttributes[NSFontAttributeName] = [NSFont messageFontOfSize:12.0];
        _fTitleAttributes[NSParagraphStyleAttributeName] = paragraphStyle;

        _fStatusAttributes = [[NSMutableDictionary alloc] initWithCapacity:3];
        _fStatusAttributes[NSFontAttributeName] = [NSFont messageFontOfSize:10.0];
        _fStatusAttributes[NSParagraphStyleAttributeName] = paragraphStyle;

        _fBluePieceColor = [NSColor colorWithCalibratedRed:0.0 green:0.4 blue:0.8 alpha:1.0];
        _fBarBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.2];
        _fBarMinimalBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.015];
    }
    return self;
}

- (id)copyWithZone:(NSZone*)zone
{
    TorrentCell* copy = [super copyWithZone:zone];
    copy->_fTitleAttributes = [_fTitleAttributes mutableCopyWithZone:zone];
    copy->_fStatusAttributes = [_fStatusAttributes mutableCopyWithZone:zone];
    copy->_fBluePieceColor = _fBluePieceColor;
    copy->_fBarBorderColor = _fBarBorderColor;
    copy->_fBarMinimalBorderColor = _fBarMinimalBorderColor;
    [copy setRepresentedObject:self.representedObject];
    return copy;
}

- (NSUserDefaults*)fDefaults
{
    return NSUserDefaults.standardUserDefaults;
}

- (NSRect)iconRectForBounds:(NSRect)bounds
{
    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];
    CGFloat const imageSize = minimal ? kImageSizeMin : kImageSizeRegular;
    CGFloat const padding = minimal ? kGroupPaddingMin : kGroupPaddingRegular;

    return NSMakeRect(NSMinX(bounds) + (padding * 0.5) + kPaddingHorizontal, ceil(NSMidY(bounds) - imageSize * 0.5), imageSize, imageSize);
}

- (NSRect)actionRectForBounds:(NSRect)bounds
{
    NSRect iconRect = [self iconRectForBounds:bounds];
    NSRect actionRect = [self actionButtonRectForBounds:iconRect];

    return actionRect;
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
    NSRect actionButtonRect = [self actionRectForBounds:cellFrame];
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
    NSRect const parentRect = [self iconRectForBounds:cellFrame];
    NSRect iconRect = NSMakeRect(parentRect.origin.x, parentRect.origin.y, parentRect.size.width, parentRect.size.height);

    NSInteger const groupValue = torrent.groupValue;
    if (groupValue != -1)
    {
        NSRect groupRect = [self groupIconRectForBounds:iconRect];
        NSColor* groupColor = [GroupsController.groups colorForIndex:groupValue];
        NSImage* icon = [NSImage discIconWithColor:groupColor insetFactor:0];
        [icon drawInRect:groupRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0f];
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
        NSRect const errorRect = NSMakeRect(NSMaxX(iconRect) - kErrorImageSize, NSMaxY(iconRect) - kErrorImageSize, kErrorImageSize, kErrorImageSize);
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
            [actionImage drawInRect:[self actionButtonRectForBounds:iconRect] fromRect:NSZeroRect
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
            NSMaxX(titleRect) + kPaddingBetweenTitleAndPriority,
            NSMidY(titleRect) - kPriorityIconSize * 0.5,
            kPriorityIconSize,
            kPriorityIconSize);

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
        return NSInsetRect(realRect, -kPaddingExpansionFrame, -kPaddingExpansionFrame);
    }

    return NSZeroRect;
}

- (void)drawWithExpansionFrame:(NSRect)cellFrame inView:(NSView*)view
{
    cellFrame.origin.x += kPaddingExpansionFrame;
    cellFrame.origin.y += kPaddingExpansionFrame;

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
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floor(NSHeight(barRect) * kPiecesTotalPercent * piecesBarPercent), NSMaxYEdge);

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

    NSInteger pieceCount = MIN(torrent.pieceCount, kMaxPieces);
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
        // faster and non-broken alternative to `[bitmap setColor:pieceColor atX:i y:0]`
        unsigned char* data = bitmap.bitmapData + (i << 2);
        data[0] = pieceColor.redComponent * 255;
        data[1] = pieceColor.greenComponent * 255;
        data[2] = pieceColor.blueComponent * 255;
        data[3] = pieceColor.alphaComponent * 255;
    }

    free(piecesPercent);

    torrent.previousFinishedPieces = finishedIndexes.count > 0 ? finishedIndexes : nil; //don't bother saving if none are complete

    //actually draw image
    [bitmap drawInRect:barRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
              fraction:[self.fDefaults boolForKey:@"SmallView"] ? 0.25 : 1.0
        respectFlipped:YES
                 hints:nil];
}

- (NSRect)rectForMinimalStatusWithString:(NSAttributedString*)string inBounds:(NSRect)bounds
{
    NSRect result;
    result.size = [string size];

    result.origin.x = NSMaxX(bounds) - (kPaddingHorizontal + NSWidth(result) + kPaddingEdgeMax);
    result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);

    return result;
}

- (NSRect)rectForTitleWithString:(NSAttributedString*)string
                  withRightBound:(CGFloat)rightBound
                        inBounds:(NSRect)bounds
                         minimal:(BOOL)minimal
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + kPaddingHorizontal + (minimal ? kImageSizeMin : kImageSizeRegular) + kPaddingBetweenImageAndTitle;
    result.size.height = kHeightTitle;

    if (minimal)
    {
        result.origin.x += kGroupPaddingMin;
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);
        result.size.width = rightBound - NSMinX(result) - kPaddingBetweenTitleAndMinStatus;
    }
    else
    {
        result.origin.x += kGroupPaddingRegular;
        result.origin.y = NSMinY(bounds) + kPaddingAboveTitle;
        result.size.width = rightBound - NSMinX(result) - kPaddingHorizontal - kPaddingEdgeMax;
    }

    if (((Torrent*)self.representedObject).priority != TR_PRI_NORMAL)
    {
        result.size.width -= kPriorityIconSize + kPaddingBetweenTitleAndPriority;
    }
    result.size.width = MIN(NSWidth(result), [string size].width);

    return result;
}

- (NSRect)rectForProgressWithStringInBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + kPaddingAboveTitle + kHeightTitle + kPaddingBetweenTitleAndProgress;
    result.origin.x = NSMinX(bounds) + kPaddingHorizontal + kGroupPaddingRegular + kImageSizeRegular + kPaddingBetweenImageAndTitle;

    result.size.height = kHeightStatus;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - kPaddingHorizontal;

    return result;
}

- (NSRect)rectForStatusWithStringInBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.y = NSMinY(bounds) + kPaddingAboveTitle + kHeightTitle + kPaddingBetweenTitleAndProgress + kHeightStatus +
        kPaddingBetweenProgressAndBar + kBarHeight + kPaddingBetweenBarAndStatus;
    result.origin.x = NSMinX(bounds) + kPaddingHorizontal + kGroupPaddingRegular + kImageSizeRegular + kPaddingBetweenImageAndTitle;

    result.size.height = kHeightStatus;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - kPaddingHorizontal;

    return result;
}

- (NSRect)barRectRegForBounds:(NSRect)bounds
{
    NSRect result;
    result.size.height = kBarHeight;
    result.origin.x = NSMinX(bounds) + kPaddingHorizontal + kGroupPaddingRegular + kImageSizeRegular + kPaddingBetweenImageAndBar;
    result.origin.y = NSMinY(bounds) + kPaddingAboveTitle + kHeightTitle + kPaddingBetweenTitleAndProgress + kHeightStatus +
        kPaddingBetweenProgressAndBar;

    result.size.width = floor(
        NSMaxX(bounds) - NSMinX(result) - kPaddingHorizontal - 2.0 * (kPaddingBetweenButtons + kNormalButtonWidth + kPaddingEdgeMax));

    return result;
}

- (NSRect)barRectMinForBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + kPaddingHorizontal + kImageSizeMin + kGroupPaddingMin + kPaddingBetweenImageAndBar;
    result.origin.y = NSMinY(bounds) + kPaddingBetweenBarAndEdgeMin;
    result.size.height = NSHeight(bounds) - 2.0 * kPaddingBetweenBarAndEdgeMin;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - kPaddingBetweenBarAndEdgeMin - kPaddingEdgeMax;

    return result;
}

- (NSRect)controlButtonRectForBounds:(NSRect)bounds
{
    NSRect result;
    result.size.height = kNormalButtonWidth;
    result.size.width = kNormalButtonWidth;
    result.origin.x = NSMaxX(bounds) - (kPaddingHorizontal + kNormalButtonWidth + kPaddingBetweenButtons + kNormalButtonWidth + kPaddingEdgeMax);

    if (![self.fDefaults boolForKey:@"SmallView"])
    {
        result.origin.y = NSMinY(bounds) + kPaddingAboveTitle + kHeightTitle - (kNormalButtonWidth - kBarHeight) * 0.5 +
            kPaddingBetweenTitleAndProgress + kHeightStatus + kPaddingBetweenProgressAndBar;
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
    result.size.height = kNormalButtonWidth;
    result.size.width = kNormalButtonWidth;
    result.origin.x = NSMaxX(bounds) - (kPaddingHorizontal + kNormalButtonWidth + kPaddingEdgeMax);

    if (![self.fDefaults boolForKey:@"SmallView"])
    {
        result.origin.y = NSMinY(bounds) + kPaddingAboveTitle + kHeightTitle - (kNormalButtonWidth - kBarHeight) * 0.5 +
            kPaddingBetweenTitleAndProgress + kHeightStatus + kPaddingBetweenProgressAndBar;
    }
    else
    {
        result.origin.y = ceil(NSMidY(bounds) - NSHeight(result) * 0.5);
    }

    return result;
}

- (NSRect)actionButtonRectForBounds:(NSRect)bounds
{
    return NSMakeRect(NSMidX(bounds) - kActionButtonWidth * 0.5, NSMidY(bounds) - kActionButtonWidth * 0.5, kActionButtonWidth, kActionButtonWidth);
}

- (NSRect)groupIconRectForBounds:(NSRect)bounds
{
    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];
    CGFloat const imageSize = minimal ? kGroupImageSizeMin : kGroupImageSizeRegular;
    CGFloat const padding = minimal ? kGroupPaddingMin + 2 : kGroupPaddingRegular + 1.5;

    return NSMakeRect(NSMinX(bounds) - padding * 0.5, NSMidY(bounds) - imageSize * 0.5, imageSize, imageSize);
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
