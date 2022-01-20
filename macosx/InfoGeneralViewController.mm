// This file Copyright (c) 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoGeneralViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

@interface InfoGeneralViewController (Private)

- (void)setupInfo;

@end

@implementation InfoGeneralViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoGeneralView" bundle:nil]))
    {
        self.title = NSLocalizedString(@"General Info", "Inspector view -> title");
    }

    return self;
}

- (void)awakeFromNib
{
#warning remove when 10.7-only with auto layout
    [fInfoSectionLabel sizeToFit];
    [fWhereSectionLabel sizeToFit];

    NSArray* labels = @[
        fPiecesLabel,
        fHashLabel,
        fSecureLabel,
        fCreatorLabel,
        fDateCreatedLabel,
        fCommentLabel,
        fDataLocationLabel,
    ];

    CGFloat oldMaxWidth = 0.0, originX, newMaxWidth = 0.0;
    for (NSTextField* label in labels)
    {
        NSRect const oldFrame = label.frame;
        if (oldFrame.size.width > oldMaxWidth)
        {
            oldMaxWidth = oldFrame.size.width;
            originX = oldFrame.origin.x;
        }

        [label sizeToFit];
        CGFloat const newWidth = label.bounds.size.width;
        if (newWidth > newMaxWidth)
        {
            newMaxWidth = newWidth;
        }
    }

    for (NSTextField* label in labels)
    {
        NSRect frame = label.frame;
        frame.origin.x = originX + (newMaxWidth - frame.size.width);
        label.frame = frame;
    }

    NSArray* fields = @[
        fPiecesField,
        fHashField,
        fSecureField,
        fCreatorField,
        fDateCreatedField,
        fCommentScrollView,
        fDataLocationField,
    ];

    CGFloat const widthIncrease = newMaxWidth - oldMaxWidth;
    for (NSView* field in fields)
    {
        NSRect frame = field.frame;
        frame.origin.x += widthIncrease;
        frame.size.width -= widthIncrease;
        field.frame = frame;
    }
}

- (void)setInfoForTorrents:(NSArray*)torrents
{
    //don't check if it's the same in case the metadata changed
    fTorrents = torrents;

    fSet = NO;
}

- (void)updateInfo
{
    if (!fSet)
    {
        [self setupInfo];
    }

    if (fTorrents.count != 1)
    {
        return;
    }

    Torrent* torrent = fTorrents[0];

    NSString* location = torrent.dataLocation;
    fDataLocationField.stringValue = location ? location.stringByAbbreviatingWithTildeInPath : @"";
    fDataLocationField.toolTip = location ? location : @"";

    fRevealDataButton.hidden = !location;
}

- (void)revealDataFile:(id)sender
{
    Torrent* torrent = fTorrents[0];
    NSString* location = torrent.dataLocation;
    if (!location)
    {
        return;
    }

    NSURL* file = [NSURL fileURLWithPath:location];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ file ]];
}

@end

@implementation InfoGeneralViewController (Private)

- (void)setupInfo
{
    if (fTorrents.count == 1)
    {
        Torrent* torrent = fTorrents[0];

#warning candidate for localizedStringWithFormat (although then we'll get two commas)
        NSString* piecesString = !torrent.magnet ?
            [NSString stringWithFormat:@"%ld, %@", torrent.pieceCount, [NSString stringForFileSize:torrent.pieceSize]] :
            @"";
        fPiecesField.stringValue = piecesString;

        NSString* hashString = torrent.hashString;
        fHashField.stringValue = hashString;
        fHashField.toolTip = hashString;
        fSecureField.stringValue = torrent.privateTorrent ?
            NSLocalizedString(@"Private Torrent, non-tracker peer discovery disabled", "Inspector -> private torrent") :
            NSLocalizedString(@"Public Torrent", "Inspector -> private torrent");

        NSString* commentString = torrent.comment;
        fCommentView.string = commentString;

        NSString* creatorString = torrent.creator;
        fCreatorField.stringValue = creatorString;
        fDateCreatedField.objectValue = torrent.dateCreated;
    }
    else
    {
        fPiecesField.stringValue = @"";
        fHashField.stringValue = @"";
        fHashField.toolTip = nil;
        fSecureField.stringValue = @"";
        fCommentView.string = @"";

        fCreatorField.stringValue = @"";
        fDateCreatedField.stringValue = @"";

        fDataLocationField.stringValue = @"";
        fDataLocationField.toolTip = nil;

        fRevealDataButton.hidden = YES;
    }

    fSet = YES;
}

@end
