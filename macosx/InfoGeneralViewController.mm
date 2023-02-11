// This file Copyright © 2010-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoGeneralViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

@interface InfoGeneralViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) IBOutlet NSTextField* fPiecesField;
@property(nonatomic) IBOutlet NSTextField* fHashField;
@property(nonatomic) IBOutlet NSTextField* fSecureField;
@property(nonatomic) IBOutlet NSTextField* fDataLocationField;
@property(nonatomic) IBOutlet NSTextField* fCreatorField;
@property(nonatomic) IBOutlet NSTextField* fDateCreatedField;

@property(nonatomic) IBOutlet NSTextView* fCommentView;

@property(nonatomic) IBOutlet NSButton* fRevealDataButton;

//remove when we switch to auto layout
@property(nonatomic) IBOutlet NSTextField* fPiecesLabel;
@property(nonatomic) IBOutlet NSTextField* fHashLabel;
@property(nonatomic) IBOutlet NSTextField* fSecureLabel;
@property(nonatomic) IBOutlet NSTextField* fCreatorLabel;
@property(nonatomic) IBOutlet NSTextField* fDateCreatedLabel;
@property(nonatomic) IBOutlet NSTextField* fCommentLabel;
@property(nonatomic) IBOutlet NSTextField* fDataLocationLabel;
@property(nonatomic) IBOutlet NSTextField* fInfoSectionLabel;
@property(nonatomic) IBOutlet NSTextField* fWhereSectionLabel;
@property(nonatomic) IBOutlet NSScrollView* fCommentScrollView;

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
#warning remove when switching to auto layout
    [self.fInfoSectionLabel sizeToFit];
    [self.fWhereSectionLabel sizeToFit];

    NSArray* labels = @[
        self.fPiecesLabel,
        self.fHashLabel,
        self.fSecureLabel,
        self.fCreatorLabel,
        self.fDateCreatedLabel,
        self.fCommentLabel,
        self.fDataLocationLabel,
    ];

    CGFloat oldMaxWidth = 0.0, originX = 0.0, newMaxWidth = 0.0;
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
        self.fPiecesField,
        self.fHashField,
        self.fSecureField,
        self.fCreatorField,
        self.fDateCreatedField,
        self.fCommentScrollView,
        self.fDataLocationField,
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

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet)
    {
        [self setupInfo];
    }

    if (self.fTorrents.count != 1)
    {
        return;
    }

    Torrent* torrent = self.fTorrents[0];

    NSString* location = torrent.dataLocation;
    self.fDataLocationField.stringValue = location ? location.stringByAbbreviatingWithTildeInPath : @"";
    self.fDataLocationField.toolTip = location ? location : @"";

    self.fRevealDataButton.hidden = !location;
}

- (void)revealDataFile:(id)sender
{
    Torrent* torrent = self.fTorrents[0];
    NSString* location = torrent.dataLocation;
    if (!location)
    {
        return;
    }

    NSURL* file = [NSURL fileURLWithPath:location];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ file ]];
}

#pragma mark - Private

- (void)setupInfo
{
    if (self.fTorrents.count == 1)
    {
        Torrent* torrent = self.fTorrents[0];

#warning candidate for localizedStringWithFormat (although then we'll get two commas)
        NSString* piecesString = !torrent.magnet ?
            [NSString stringWithFormat:@"%ld, %@", torrent.pieceCount, [NSString stringForFileSize:torrent.pieceSize]] :
            @"";
        self.fPiecesField.stringValue = piecesString;

        NSString* hashString = torrent.hashString;
        self.fHashField.stringValue = hashString;
        self.fHashField.toolTip = hashString;
        self.fSecureField.stringValue = torrent.privateTorrent ?
            NSLocalizedString(@"Private Torrent, non-tracker peer discovery disabled", "Inspector -> private torrent") :
            NSLocalizedString(@"Public Torrent", "Inspector -> private torrent");

        NSString* commentString = torrent.comment;
        self.fCommentView.string = commentString;

        NSString* creatorString = torrent.creator;
        self.fCreatorField.stringValue = creatorString;
        self.fDateCreatedField.objectValue = torrent.dateCreated;
    }
    else
    {
        self.fPiecesField.stringValue = @"";
        self.fHashField.stringValue = @"";
        self.fHashField.toolTip = nil;
        self.fSecureField.stringValue = @"";
        self.fCommentView.string = @"";

        self.fCreatorField.stringValue = @"";
        self.fDateCreatedField.stringValue = @"";

        self.fDataLocationField.stringValue = @"";
        self.fDataLocationField.toolTip = nil;

        self.fRevealDataButton.hidden = YES;
    }

    self.fSet = YES;
}

@end
