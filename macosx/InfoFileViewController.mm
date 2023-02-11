// This file Copyright © 2010-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoFileViewController.h"
#import "FileListNode.h"
#import "FileOutlineController.h"
#import "FileOutlineView.h"
#import "Torrent.h"

@interface InfoFileViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) IBOutlet FileOutlineController* fFileController;

@property(nonatomic) IBOutlet NSSearchField* fFileFilterField;
@property(nonatomic) IBOutlet NSButton* fCheckAllButton;
@property(nonatomic) IBOutlet NSButton* fUncheckAllButton;

@end

@implementation InfoFileViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoFileView" bundle:nil]))
    {
        self.title = NSLocalizedString(@"Files", "Inspector view -> title");
    }

    return self;
}

- (void)awakeFromNib
{
    CGFloat const height = [NSUserDefaults.standardUserDefaults floatForKey:@"InspectorContentHeightFiles"];
    if (height != 0.0)
    {
        NSRect viewRect = self.view.frame;
        viewRect.size.height = height;
        self.view.frame = viewRect;
    }

    [self.fFileFilterField.cell setPlaceholderString:NSLocalizedString(@"Filter", "inspector -> file filter")];

    //localize and place all and none buttons
    self.fCheckAllButton.title = NSLocalizedString(@"All", "inspector -> check all");
    self.fUncheckAllButton.title = NSLocalizedString(@"None", "inspector -> check all");

    NSRect checkAllFrame = self.fCheckAllButton.frame;
    NSRect uncheckAllFrame = self.fUncheckAllButton.frame;
    CGFloat const oldAllWidth = checkAllFrame.size.width;
    CGFloat const oldNoneWidth = uncheckAllFrame.size.width;

    [self.fCheckAllButton sizeToFit];
    [self.fUncheckAllButton sizeToFit];
    CGFloat const newWidth = MAX(self.fCheckAllButton.bounds.size.width, self.fUncheckAllButton.bounds.size.width);

    CGFloat const uncheckAllChange = newWidth - oldNoneWidth;
    uncheckAllFrame.size.width = newWidth;
    uncheckAllFrame.origin.x -= uncheckAllChange;
    self.fUncheckAllButton.frame = uncheckAllFrame;

    CGFloat const checkAllChange = newWidth - oldAllWidth;
    checkAllFrame.size.width = newWidth;
    checkAllFrame.origin.x -= (checkAllChange + uncheckAllChange);
    self.fCheckAllButton.frame = checkAllFrame;
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

    if (self.fTorrents.count == 1)
    {
        [self.fFileController refresh];

#warning use TorrentFileCheckChange notification as well
        Torrent* torrent = self.fTorrents[0];
        if (torrent.folder)
        {
            NSInteger const filesCheckState = [torrent
                checkForFiles:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, torrent.fileCount)]];
            self.fCheckAllButton.enabled = filesCheckState != NSControlStateValueOn; //if anything is unchecked
            self.fUncheckAllButton.enabled = !torrent.allDownloaded; //if there are any checked files that aren't finished
        }
    }
}

- (void)saveViewSize
{
    [NSUserDefaults.standardUserDefaults setFloat:NSHeight(self.view.frame) forKey:@"InspectorContentHeightFiles"];
}

- (void)setFileFilterText:(id)sender
{
    self.fFileController.filterText = [sender stringValue];
}

- (IBAction)checkAll:(id)sender
{
    [self.fFileController checkAll];
}

- (IBAction)uncheckAll:(id)sender
{
    [self.fFileController uncheckAll];
}

- (void)keyDown:(NSEvent*)event
{
    unichar const firstChar = [event.charactersIgnoringModifiers characterAtIndex:0];

    if (firstChar == ' ')
    {
        [self toggleQuickLook:nil];
    }
    else
    {
        [super keyDown:event];
    }
}

- (void)toggleQuickLook:(id)sender
{
    if ([QLPreviewPanel sharedPreviewPanel].visible)
    {
        [[QLPreviewPanel sharedPreviewPanel] orderOut:nil];
    }
    else
    {
        [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFront:nil];
    }
}

- (NSArray<NSURL*>*)quickLookURLs
{
    FileOutlineView* fileOutlineView = self.fFileController.outlineView;
    Torrent* torrent = self.fTorrents[0];
    NSIndexSet* indexes = fileOutlineView.selectedRowIndexes;
    NSMutableArray* urlArray = [NSMutableArray arrayWithCapacity:indexes.count];

    for (NSUInteger i = indexes.firstIndex; i != NSNotFound; i = [indexes indexGreaterThanIndex:i])
    {
        FileListNode* item = [fileOutlineView itemAtRow:i];
        if ([self canQuickLookFile:item])
        {
            [urlArray addObject:[NSURL fileURLWithPath:[torrent fileLocation:item]]];
        }
    }

    return urlArray;
}

- (BOOL)canQuickLook
{
    if (self.fTorrents.count != 1)
    {
        return NO;
    }

    Torrent* torrent = self.fTorrents[0];
    if (!torrent.folder)
    {
        return NO;
    }

    FileOutlineView* fileOutlineView = self.fFileController.outlineView;
    NSIndexSet* indexes = fileOutlineView.selectedRowIndexes;

    for (NSUInteger i = indexes.firstIndex; i != NSNotFound; i = [indexes indexGreaterThanIndex:i])
    {
        if ([self canQuickLookFile:[fileOutlineView itemAtRow:i]])
        {
            return YES;
        }
    }

    return NO;
}

- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item
{
    FileOutlineView* fileOutlineView = self.fFileController.outlineView;

    NSString* fullPath = ((NSURL*)item).path;
    Torrent* torrent = self.fTorrents[0];
    NSRange visibleRows = [fileOutlineView rowsInRect:fileOutlineView.bounds];

    for (NSUInteger row = visibleRows.location; row < NSMaxRange(visibleRows); row++)
    {
        FileListNode* rowItem = [fileOutlineView itemAtRow:row];
        if ([[torrent fileLocation:rowItem] isEqualToString:fullPath])
        {
            NSRect frame = [fileOutlineView iconRectForRow:row];

            if (!NSIntersectsRect(fileOutlineView.visibleRect, frame))
            {
                return NSZeroRect;
            }

            frame.origin = [fileOutlineView convertPoint:frame.origin toView:nil];
            frame = [self.view.window convertRectToScreen:frame];
            frame.origin.y -= frame.size.height;
            return frame;
        }
    }

    return NSZeroRect;
}

#pragma mark - Private

- (void)setupInfo
{
    self.fFileFilterField.stringValue = @"";
    [self setFileFilterText:self.fFileFilterField];

    if (self.fTorrents.count == 1)
    {
        Torrent* torrent = self.fTorrents[0];

        self.fFileController.torrent = torrent;

        BOOL const isFolder = torrent.folder;
        self.fFileFilterField.enabled = isFolder;

        if (!isFolder)
        {
            self.fCheckAllButton.enabled = NO;
            self.fUncheckAllButton.enabled = NO;
        }
    }
    else
    {
        self.fFileController.torrent = nil;

        self.fFileFilterField.enabled = NO;

        self.fCheckAllButton.enabled = NO;
        self.fUncheckAllButton.enabled = NO;
    }

    self.fSet = YES;
}

- (BOOL)canQuickLookFile:(FileListNode*)item
{
    Torrent* torrent = self.fTorrents[0];
    return (item.isFolder || [torrent fileProgress:item] >= 1.0) && [torrent fileLocation:item];
}

@end
