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

typedef void (^CompletionBlock)(BOOL);

@interface FileRenameSheetController ()

@property(nonatomic) Torrent* torrent;
@property(nonatomic) FileListNode* node;
@property(nonatomic, copy) CompletionBlock completionHandler;

@property(nonatomic, copy) NSString* originalName;

@end

@implementation FileRenameSheetController

+ (void)presentSheetForTorrent:(Torrent*)torrent
                modalForWindow:(NSWindow*)window
             completionHandler:(void (^)(BOOL didRename))completionHandler
{
    NSParameterAssert(torrent != nil);
    NSParameterAssert(window != nil);

    FileRenameSheetController* renamer = [[FileRenameSheetController alloc] initWithWindowNibName:@"FileRenameSheetController"];

    renamer.torrent = torrent;
    renamer.completionHandler = completionHandler;

    [NSApp beginSheet:renamer.window modalForWindow:window modalDelegate:self
        didEndSelector:@selector(sheetDidEnd:returnCode:contextInfo:)
           contextInfo:(__bridge_retained void*)(renamer)];
}

+ (void)presentSheetForFileListNode:(FileListNode*)node
                     modalForWindow:(NSWindow*)window
                  completionHandler:(void (^)(BOOL didRename))completionHandler
{
    NSParameterAssert(node != nil);
    NSParameterAssert(window != nil);

    FileRenameSheetController* renamer = [[FileRenameSheetController alloc] initWithWindowNibName:@"FileRenameSheetController"];

    renamer.torrent = node.torrent;
    renamer.node = node;
    renamer.completionHandler = completionHandler;

    [NSApp beginSheet:renamer.window modalForWindow:window modalDelegate:self
        didEndSelector:@selector(sheetDidEnd:returnCode:contextInfo:)
           contextInfo:(__bridge_retained void*)(renamer)];
}

+ (void)sheetDidEnd:(NSWindow*)sheet returnCode:(NSInteger)returnCode contextInfo:(void*)contextInfo
{
    FileRenameSheetController* renamer = (__bridge_transfer FileRenameSheetController*)(contextInfo);
    NSParameterAssert([renamer isKindOfClass:[FileRenameSheetController class]]);

    renamer.completionHandler(returnCode == NSOKButton);

    [sheet orderOut:self];
}

- (void)windowDidLoad
{
    [super windowDidLoad];

    self.originalName = self.node.name ?: self.torrent.name;
    NSString* label = [NSString stringWithFormat:NSLocalizedString(@"Rename the file \"%@\":", "rename sheet label"), self.originalName];
    self.labelField.stringValue = label;

    self.inputField.stringValue = self.originalName;
    self.renameButton.enabled = NO;

    //resize the buttons so that they're long enough and the same width
    NSRect const oldRenameFrame = self.renameButton.frame;
    NSRect const oldCancelFrame = self.cancelButton.frame;

    //get the extra width of the rename button from the English xib - the width from sizeToFit is too squished
    [self.renameButton sizeToFit];
    CGFloat const extra = NSWidth(oldRenameFrame) - NSWidth(self.renameButton.frame);

    self.renameButton.title = NSLocalizedString(@"Rename", "rename sheet button");
    self.cancelButton.title = NSLocalizedString(@"Cancel", "rename sheet button");

    [self.renameButton sizeToFit];
    [self.cancelButton sizeToFit];
    NSRect newRenameFrame = self.renameButton.frame;
    NSRect newCancelFrame = self.cancelButton.frame;
    newRenameFrame.size.width = MAX(NSWidth(newRenameFrame), NSWidth(newCancelFrame)) + extra;
    newCancelFrame.size.width = MAX(NSWidth(newRenameFrame), NSWidth(newCancelFrame)) + extra;

    CGFloat const renameWidthIncrease = NSWidth(newRenameFrame) - NSWidth(oldRenameFrame);
    newRenameFrame.origin.x -= renameWidthIncrease;
    self.renameButton.frame = newRenameFrame;

    CGFloat const cancelWidthIncrease = NSWidth(newCancelFrame) - NSWidth(oldCancelFrame);
    newCancelFrame.origin.x -= renameWidthIncrease + cancelWidthIncrease;
    self.cancelButton.frame = newCancelFrame;
}

- (IBAction)rename:(id)sender
{
    void (^completionHandler)(BOOL) = ^(BOOL didRename) {
        if (didRename)
        {
            [NSApp endSheet:self.window returnCode:NSOKButton];
        }
        else
        {
#warning more thorough error
            NSBeep();
        }
    };

    if (self.node)
    {
        [self.torrent renameFileNode:self.node withName:self.inputField.stringValue completionHandler:completionHandler];
    }
    else
    {
        [self.torrent renameTorrent:self.inputField.stringValue completionHandler:completionHandler];
    }
}

- (IBAction)cancelRename:(id)sender
{
    [NSApp endSheet:self.window returnCode:NSCancelButton];
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    self.renameButton.enabled = ![self.inputField.stringValue isEqualToString:@""] &&
        ![self.inputField.stringValue isEqualToString:self.originalName];
}

@end
