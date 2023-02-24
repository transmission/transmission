// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PeerProgressIndicatorCell.h"
#import "NSStringAdditions.h"

@interface PeerProgressIndicatorCell ()

@property(nonatomic, copy) NSDictionary* fAttributes;

@end

@implementation PeerProgressIndicatorCell

- (id)copyWithZone:(NSZone*)zone
{
    PeerProgressIndicatorCell* copy = [super copyWithZone:zone];
    copy->_fAttributes = _fAttributes;

    return copy;
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"DisplayPeerProgressBarNumber"])
    {
        if (!self.fAttributes)
        {
            NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
            paragraphStyle.alignment = NSTextAlignmentRight;

            self.fAttributes = @{
                NSFontAttributeName : [NSFont systemFontOfSize:11.0],
                NSForegroundColorAttributeName : NSColor.labelColor,
                NSParagraphStyleAttributeName : paragraphStyle
            };
        }

        [[NSString percentString:self.floatValue longDecimals:NO] drawInRect:cellFrame withAttributes:self.fAttributes];
    }
    else
    {
        //attributes not needed anymore
        if (self.fAttributes)
        {
            self.fAttributes = nil;
        }

        [super drawWithFrame:cellFrame inView:controlView];
        if (self.seed)
        {
            NSImage* checkImage = [NSImage imageNamed:@"CompleteCheck"];

            NSSize const imageSize = checkImage.size;
            NSRect const rect = NSMakeRect(
                floor(NSMidX(cellFrame) - imageSize.width * 0.5),
                floor(NSMidY(cellFrame) - imageSize.height * 0.5),
                imageSize.width,
                imageSize.height);

            [checkImage drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                    respectFlipped:YES
                             hints:nil];
        }
    }
}

@end
