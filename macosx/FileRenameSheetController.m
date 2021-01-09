//
//  FileRenameSheetController.m
//  Transmission
//
//  Created by Mitchell Livingston on 1/20/13.
//  Copyright (c) 2013 The Transmission Project. All rights reserved.
//

#import "FileRenameSheetController.h"
#import "FileListNode.h"
#import "Torrent.h"

@interface FileRenameSheetController ()

@property (nonatomic, strong) Torrent * torrent;
@property (nonatomic, strong) FileListNode * node;
@property (nonatomic, copy) NSString * originalName;

@end

@implementation FileRenameSheetController
{
    Torrent * _torrent;
    FileListNode * _node;
    NSString * _originalName;
}

+ (void) presentSheetForTorrent: (Torrent *) torrent modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler
{
    NSParameterAssert(torrent != nil);
    NSParameterAssert(window != nil);

    FileRenameSheetController * renamer = [[FileRenameSheetController alloc] initWithWindowNibName: @"FileRenameSheetController"];

    renamer.torrent = torrent;

    [window beginSheet:[renamer window] completionHandler:^(NSModalResponse returnCode) {
        completionHandler(returnCode == NSModalResponseOK);
        [[renamer window] orderOut:self];
    }];
}

+ (void) presentSheetForFileListNode: (FileListNode *) node modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler
{
    NSParameterAssert(node != nil);
    NSParameterAssert(window != nil);

    FileRenameSheetController * renamer = [[FileRenameSheetController alloc] initWithWindowNibName: @"FileRenameSheetController"];

    renamer.torrent = [node torrent];
    renamer.node = node;

    [window beginSheet:[renamer window] completionHandler:^(NSModalResponse returnCode) {
        completionHandler(returnCode == NSModalResponseOK);
        [[renamer window] orderOut:self];
    }];
}

- (void) windowDidLoad
{
    [super windowDidLoad];

    self.originalName = [self.node name] ?: [self.torrent name];
    NSString * label = [NSString stringWithFormat: NSLocalizedString(@"Rename the file \"%@\":", "rename sheet label"), self.originalName];
    [self.labelField setStringValue: label];

    [self.inputField setStringValue: self.originalName];
    [self.renameButton setEnabled: NO];

    //resize the buttons so that they're long enough and the same width
    const NSRect oldRenameFrame = [self.renameButton frame];
    const NSRect oldCancelFrame = [self.cancelButton frame];

    //get the extra width of the rename button from the English xib - the width from sizeToFit is too squished
    [self.renameButton sizeToFit];
    const CGFloat extra = NSWidth(oldRenameFrame) - NSWidth([self.renameButton frame]);

    [self.renameButton setTitle: NSLocalizedString(@"Rename", "rename sheet button")];
    [self.cancelButton setTitle: NSLocalizedString(@"Cancel", "rename sheet button")];

    [self.renameButton sizeToFit];
    [self.cancelButton sizeToFit];
    NSRect newRenameFrame = [self.renameButton frame];
    NSRect newCancelFrame = [self.cancelButton frame];
    newRenameFrame.size.width = MAX(NSWidth(newRenameFrame), NSWidth(newCancelFrame)) + extra;
    newCancelFrame.size.width = MAX(NSWidth(newRenameFrame), NSWidth(newCancelFrame)) + extra;

    const CGFloat renameWidthIncrease = NSWidth(newRenameFrame) - NSWidth(oldRenameFrame);
    newRenameFrame.origin.x -= renameWidthIncrease;
    [self.renameButton setFrame:newRenameFrame];

    const CGFloat cancelWidthIncrease = NSWidth(newCancelFrame) - NSWidth(oldCancelFrame);
    newCancelFrame.origin.x -= renameWidthIncrease + cancelWidthIncrease;
    [self.cancelButton setFrame:newCancelFrame];
}

- (IBAction) rename: (id) sender
{
    void (^completionHandler)(BOOL) = ^(BOOL didRename) {
        if (didRename)
            [[[self window] sheetParent] endSheet:[self window] returnCode:NSModalResponseOK];
        else
        {
            #warning more thorough error
            NSBeep();
        }
    };

    if (self.node)
        [self.torrent renameFileNode: self.node withName: [self.inputField stringValue] completionHandler: completionHandler];
    else
        [self.torrent renameTorrent: [self.inputField stringValue] completionHandler: completionHandler];
}

- (IBAction) cancelRename: (id) sender
{
    [[[self window] sheetParent] endSheet:[self window] returnCode:NSModalResponseCancel];
}

- (void) controlTextDidChange: (NSNotification *) notification
{
    [self.renameButton setEnabled: ![[self.inputField stringValue] isEqualToString: @""] && ![[self.inputField stringValue] isEqualToString: self.originalName]];
}

@end
