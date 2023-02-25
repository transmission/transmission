// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "DragOverlayView.h"

static CGFloat const kPadding = 10.0;
static CGFloat const kIconWidth = 64.0;

@interface DragOverlayView ()

@property(nonatomic) NSImage* fBadge;

@property(nonatomic, readonly) NSDictionary* fMainLineAttributes;
@property(nonatomic, readonly) NSDictionary* fSubLineAttributes;

@end

@implementation DragOverlayView

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        //create attributes
        NSShadow* stringShadow = [[NSShadow alloc] init];
        stringShadow.shadowOffset = NSMakeSize(2.0, -2.0);
        stringShadow.shadowBlurRadius = 4.0;

        NSFont *bigFont = [NSFont boldSystemFontOfSize:18.0], *smallFont = [NSFont systemFontOfSize:14.0];

        NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        paragraphStyle.lineBreakMode = NSLineBreakByTruncatingMiddle;

        _fMainLineAttributes = @{
            NSForegroundColorAttributeName : NSColor.whiteColor,
            NSFontAttributeName : bigFont,
            NSShadowAttributeName : stringShadow,
            NSParagraphStyleAttributeName : paragraphStyle
        };

        _fSubLineAttributes = @{
            NSForegroundColorAttributeName : NSColor.whiteColor,
            NSFontAttributeName : smallFont,
            NSShadowAttributeName : stringShadow,
            NSParagraphStyleAttributeName : paragraphStyle
        };
    }
    return self;
}

- (void)setOverlay:(NSImage*)icon mainLine:(NSString*)mainLine subLine:(NSString*)subLine
{
    //create badge
    NSRect const badgeRect = NSMakeRect(0.0, 0.0, 325.0, 84.0);

    self.fBadge = [[NSImage alloc] initWithSize:badgeRect.size];
    [self.fBadge lockFocus];

    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:badgeRect xRadius:15.0 yRadius:15.0];
    [[NSColor colorWithCalibratedWhite:0.0 alpha:0.75] set];
    [bp fill];

    //place icon
    [icon drawInRect:NSMakeRect(kPadding, (NSHeight(badgeRect) - kIconWidth) * 0.5, kIconWidth, kIconWidth) fromRect:NSZeroRect
           operation:NSCompositingOperationSourceOver
            fraction:1.0];

    //place main text
    NSSize const mainLineSize = [mainLine sizeWithAttributes:self.fMainLineAttributes];
    NSSize const subLineSize = [subLine sizeWithAttributes:self.fSubLineAttributes];

    NSRect lineRect = NSMakeRect(
        kPadding + kIconWidth + 5.0,
        (NSHeight(badgeRect) + (subLineSize.height + 2.0 - mainLineSize.height)) * 0.5,
        NSWidth(badgeRect) - (kPadding + kIconWidth + 2.0) - kPadding,
        mainLineSize.height);
    [mainLine drawInRect:lineRect withAttributes:self.fMainLineAttributes];

    //place sub text
    lineRect.origin.y -= subLineSize.height + 2.0;
    lineRect.size.height = subLineSize.height;
    [subLine drawInRect:lineRect withAttributes:self.fSubLineAttributes];

    [self.fBadge unlockFocus];

    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)rect
{
    if (self.fBadge)
    {
        NSRect const frame = self.frame;
        NSSize const imageSize = self.fBadge.size;
        [self.fBadge drawAtPoint:NSMakePoint((NSWidth(frame) - imageSize.width) * 0.5, (NSHeight(frame) - imageSize.height) * 0.5)
                        fromRect:NSZeroRect
                       operation:NSCompositingOperationSourceOver
                        fraction:1.0];
    }
}

@end
