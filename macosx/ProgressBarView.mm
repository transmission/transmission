// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "TorrentTableView.h"
#import "Torrent.h"
#import "NSApplicationAdditions.h"

static CGFloat const kPiecesTotalPercent = 0.6;
static NSInteger const kMaxPieces = 18 * 18;

@interface ProgressBarView ()

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, readonly) NSColor* fBarBorderColor;
@property(nonatomic, readonly) NSColor* fBluePieceColor;
@property(nonatomic, readonly) NSColor* fBarMinimalBorderColor;

@end

@implementation ProgressBarView

- (instancetype)init
{
    if ((self = [super init]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        _fBluePieceColor = [NSColor colorWithCalibratedRed:0.0 green:0.4 blue:0.8 alpha:1.0];
        _fBarBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.2];
        _fBarMinimalBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.015];
    }
    return self;
}

- (void)drawBarInRect:(NSRect)barRect forTableView:(TorrentTableView*)tableView withTorrent:(Torrent*)torrent
{
    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];

    CGFloat const piecesBarPercent = tableView.piecesBarPercent;
    if (piecesBarPercent > 0.0)
    {
        NSRect piecesBarRect, regularBarRect;
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floor(NSHeight(barRect) * kPiecesTotalPercent * piecesBarPercent), NSMaxYEdge);

        [self drawRegularBar:regularBarRect forTorrent:torrent];
        [self drawPiecesBar:piecesBarRect forTorrent:torrent];
    }
    else
    {
        torrent.previousFinishedPieces = nil;

        [self drawRegularBar:barRect forTorrent:torrent];
    }

    NSColor* borderColor = minimal ? self.fBarMinimalBorderColor : self.fBarBorderColor;
    [borderColor set];
    [NSBezierPath strokeRect:NSInsetRect(barRect, 0.5, 0.5)];
}

- (void)drawRegularBar:(NSRect)barRect forTorrent:(Torrent*)torrent
{
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

- (void)drawPiecesBar:(NSRect)barRect forTorrent:(Torrent*)torrent
{
    // Fill a solid color bar for magnet links
    if (torrent.magnet)
    {
        if (NSApp.darkMode)
        {
            [NSColor.controlColor set];
        }
        else
        {
            [[NSColor colorWithCalibratedWhite:1.0 alpha:[self.fDefaults boolForKey:@"SmallView"] ? 0.25 : 1.0] set];
        }
        NSRectFillUsingOperation(barRect, NSCompositingOperationSourceOver);
        return;
    }

    int const pieceCount = static_cast<int>(MIN(torrent.pieceCount, kMaxPieces));
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

    NSColor* const pieceBgColor = NSApp.darkMode ? NSColor.controlColor : NSColor.whiteColor;

    for (int i = 0; i < pieceCount; i++)
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
            pieceColor = [pieceBgColor blendedColorWithFraction:piecesPercent[i] ofColor:self.fBluePieceColor];
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

@end

@interface ProgressBarView2 ()
/// Old properties
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, readonly) NSColor* fBarBorderColor;
@property(nonatomic, readonly) NSColor* fBluePieceColor;
@property(nonatomic, readonly) NSColor* fBarMinimalBorderColor;

/// Layers
// Слои в порядке Z-index (от нижнего к верхнему)
@property (nonatomic, strong) CAGradientLayer *unwantedLayer;     // Gray or red (unwanted)
@property (nonatomic, strong) CAGradientLayer *wantedLayer;       // White (wanted)
@property (nonatomic, strong) CAGradientLayer *unavailableLayer;  // Red (Deficit in wanted)
@property (nonatomic, strong) CAGradientLayer *haveLayer; // The main progress, variaty of colors.
@property (nonatomic, strong) CAGradientLayer *ratioLayer;// Seeding ratio (light green).

@property (nonatomic, strong) CALayer* regularBar;
@property (nonatomic, strong) CALayer* piecesBar;

@end

@implementation ProgressBarView2

- (instancetype)init
{
    if ((self = [super init]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        _fBluePieceColor = [NSColor colorWithCalibratedRed:0.0 green:0.4 blue:0.8 alpha:1.0];
        _fBarBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.2];
        _fBarMinimalBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.015];

        [self setupLayers];
    }
    return self;
}

- (CAGradientLayer *)createGradientLayer
{
    CAGradientLayer *layer = [CAGradientLayer layer];
    layer.startPoint = CGPointMake(0.5, 0);
    layer.endPoint = CGPointMake(0.5, 1);
    layer.actions = @{@"position": [NSNull null], @"bounds": [NSNull null], @"colors": [NSNull null]};
    return layer;
}

- (void)setupLayers
{
    self.wantsLayer = YES;
    self.layer.masksToBounds = YES;
    self.layer.borderWidth = 1.0;

    // Отключаем неявные анимации для всех слоев, чтобы прогресс обновлялся мгновенно
    __auto_type actions = @{@"position": [NSNull null], @"bounds": [NSNull null], @"colors": [NSNull null]};

    self.regularBar = [CALayer layer];
    self.regularBar.actions = actions;
    [self.layer addSublayer:self.regularBar];

    self.unwantedLayer = [self createGradientLayer];
    self.unavailableLayer = [self createGradientLayer];
    self.wantedLayer = [self createGradientLayer];
    self.haveLayer = [self createGradientLayer];
    self.ratioLayer = [self createGradientLayer];

    // Инициализируем и добавляем слои в правильном порядке
    [self.regularBar addSublayer:self.unwantedLayer];
    [self.regularBar addSublayer:self.unavailableLayer];
    [self.regularBar addSublayer:self.wantedLayer];
    [self.regularBar addSublayer:self.haveLayer];
    [self.regularBar addSublayer:self.ratioLayer];

    self.piecesBar = [CALayer layer];
    self.piecesBar.actions = newActions;
    self.piecesBar.contentsGravity = kCAGravityResize;
    [self.layer addSublayer:self.piecesBar];

    /// And setup layers colors
    [self setupLayersColors];
}

- (void)setupLayersColors
{
    self.ratioLayer.colors = [ModernProgressGradients progressLightGrayGradient];
    self.wantedLayer.backgroundColor = [ModernProgressGradients progressWhiteGradient];
    self.unavailableLayer.backgroundColor = [ModernProgressGradients progressRedGradient];
}

- (void)drawBarInRect:(NSRect)barRect forTableView:(TorrentTableView*)tableView withTorrent:(Torrent*)torrent
{
    __auto_type isSmall = [self.fDefaults boolForKey:@"SmallView"];
    [self updateWithTorrent:torrent tableView:tableView isSmall:isSmall];
}

- (void)updateWithTorrent:(Torrent *)torrent tableView:(TorrentTableView *)tableView isSmall:(BOOL)minimal
{
    CGRect bounds = self.bounds;
    
    // 1. Расчет геометрии (аналог NSDivideRect)
    CGFloat piecesBarPercent = tableView.piecesBarPercent;
    CGRect piecesRect = CGRectZero;
    CGRect regularRect = bounds;

    if (piecesBarPercent > 0.0) {
        CGFloat piecesHeight = floor(bounds.size.height * kPiecesTotalPercent * piecesBarPercent);
        piecesRect = CGRectMake(0, bounds.size.height - piecesHeight, bounds.size.width, piecesHeight);
        regularRect = CGRectMake(0, 0, bounds.size.width, bounds.size.height - piecesHeight);
    }

    // 2. Обновление Regular Bar (сегменты)
    [self updateRegularBarInRect:regularRect forTorrent:torrent];

    // 3. Обновление Pieces Bar
    if (!CGRectIsEmpty(piecesRect)) {
        self.piecesBar.hidden = NO;
        self.piecesBar.frame = piecesRect;
        self.piecesBar.opacity = minimal ? 0.25 : 1.0;
        [self updatePiecesContentForTorrent:torrent];
    } else {
        self.piecesBar.hidden = YES;
        torrent.previousFinishedPieces = nil;
    }

    // 4. Обновление рамки
    self.layer.borderColor = (minimal ? self.fBarMinimalBorderColor : self.fBarBorderColor).CGColor;
}

- (void)updateRegularBarInRect:(CGRect)rect forTorrent:(Torrent *)torrent
{
    self.regularBar.frame = rect;
    CGFloat width = rect.size.width;
    CGFloat currentX = 0;

    // Have section
    CGFloat haveWidth = round(torrent.progress * width);
    self.haveLayer.frame = CGRectMake(0, 0, haveWidth, rect.size.height);
    currentX = haveWidth;

    if (haveWidth > 0)
    {
        if (torrent.seeding)
        {
            CGFloat ratioWidth = round(torrent.progressStopRatio * haveWidth);
            CGRect haveRect = self.haveLayer.frame;
            self.ratioLayer.frame = CGRectMake(CGRectGetMinX(haveRect) + ratioWidth, 0, haveWidth - ratioWidth, rect.size.height);
        }
    }

    // Missing/Wanted/Unavailable logic
    if (!torrent.allDownloaded) {
        CGFloat wantedWidthTotal = round(width * torrent.progressLeft);
        CGFloat availableWidth = wantedWidthTotal;
        CGFloat unavailableWidth = 0;

        if (torrent.active && !torrent.checking && torrent.availableDesired < 1.0 && [self.fDefaults boolForKey:@"DisplayProgressBarAvailable"]) {
            availableWidth = round(wantedWidthTotal * torrent.availableDesired);
            unavailableWidth = wantedWidthTotal - availableWidth;
        }

        _wantedLayer.frame = CGRectMake(currentX, 0, availableWidth, rect.size.height);
        currentX += availableWidth;

        _unavailableLayer.frame = CGRectMake(currentX, 0, unavailableWidth, rect.size.height);
        currentX += unavailableWidth;
    } else {
        _wantedLayer.frame = CGRectZero;
        _unavailableLayer.frame = CGRectZero;
    }

    // Unwanted section
    CGFloat remainingWidth = width - currentX;
    if (remainingWidth > 0) {
        _unwantedLayer.frame = CGRectMake(currentX, 0, remainingWidth, rect.size.height);
    } else {
        _unwantedLayer.frame = CGRectZero;
    }

    [self updateRegularBarColorsForTorrent:torrent];
}

- (NSArray<NSColor *>*)haveLayerColorsForTorrent:(Torrent* )torrent
{
    // Цвета для HaveLayer (на основе вашего исходного метода)
    if (torrent.active) {
        if (torrent.checking)
        { 
            return [ModernProgressGradients progressYellowGradient];
        }
        else if (torrent.seeding)
        { 
            return [ModernProgressGradients progressGreenGradient];
        }
        else 
        { 
            return [ModernProgressGradients progressBlueGradient];
        }
    } 
    else
    {
        if (torrent.waitingToStart) {
            if (torrent.allDownloaded)
            {
                return [ModernProgressGradients progressDarkGreenGradient];
            }
            else
            {
                return [ModernProgressGradients progressDarkBlueGradient];
            }
        } 
        else
        {
            return [ModernProgressGradients progressGrayGradient];
        }
    }
}

- (NSArray<NSColor *>*)unwantedLayerColorsForTorrent:(Torrent* )torrent
{
    if (torrent.magnet)
    {
        return [ModernProgressGradients progressRedGradient];
    }
    else
    {
        return [ModernProgressGradients progressLightGrayGradient];
    }
}

- (void)updateRegularBarColorsForTorrent:(Torrent* )torrent
{
    self.haveLayer.colors = [self haveLayerColorsForTorrent:torrent];
    self.unwantedLayer.colors = [self unwantedLayerColorsForTorrent:torrent];
}

- (void)updatePiecesContentForTorrent:(Torrent* )torrent
{
    // do nothing at now.
}

@end