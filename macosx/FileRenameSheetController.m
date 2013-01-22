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

@property (nonatomic, retain) Torrent * torrent;
@property (nonatomic, retain) FileListNode * node;
@property (nonatomic, copy) CompletionBlock completionHandler;

@end

@implementation FileRenameSheetController

+ (void) presentSheetForTorrent: (Torrent *) torrent modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler
{
    NSParameterAssert(torrent != nil);
    NSParameterAssert(window != nil);
    
    FileRenameSheetController * renamer = [[FileRenameSheetController alloc] initWithWindowNibName: @"FileRenameSheetController"];
    
    renamer.torrent = torrent;
    renamer.completionHandler = completionHandler;
    
    [NSApp beginSheet: [renamer window] modalForWindow: window modalDelegate: self didEndSelector: @selector(sheetDidEnd:returnCode:contextInfo:) contextInfo: renamer];
}

+ (void) presentSheetForFileListNode: (FileListNode *) node modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler
{
    
    NSParameterAssert(node != nil);
    NSParameterAssert(window != nil);
    
    FileRenameSheetController * renamer = [[FileRenameSheetController alloc] initWithWindowNibName: @"FileRenameSheetController"];
    
    renamer.torrent = [node torrent];
    renamer.node = node;
    renamer.completionHandler = completionHandler;
    
    [NSApp beginSheet: [renamer window] modalForWindow: window modalDelegate: self didEndSelector: @selector(sheetDidEnd:returnCode:contextInfo:) contextInfo: renamer];
}

+ (void) sheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    FileRenameSheetController * renamer = contextInfo;
    NSParameterAssert([renamer isKindOfClass:[FileRenameSheetController class]]);
    
    renamer.completionHandler(returnCode == NSOKButton);
    
    //TODO: retain/release logic needs to be figured out for ARC (when ARC is enabled)
    [renamer release];
    [sheet orderOut: self];
}

- (void) dealloc
{
    [_torrent release];
    [_node release];
    [_completionHandler release];
    [super dealloc];
}

- (void) windowDidLoad
{
    [super windowDidLoad];
    
    NSString * name = self.node ? [self.node name] : [self.torrent name];
    
    NSString * label;
    if (self.node)
        label = [NSString stringWithFormat: NSLocalizedString(@"Rename \"%@\":", "rename sheet label"), name];
    else
        label = [NSString stringWithFormat: NSLocalizedString(@"Rename the transfer \"%@\":", "rename sheet label"), name];
    [self.labelField setStringValue: label];
    
    [self.inputField setStringValue: name];
    [self.renameButton setEnabled: NO];
    
    #warning size these
    [self.renameButton setStringValue: NSLocalizedString(@"Rename", "rename sheet button")];
    [self.cancelButton setStringValue: NSLocalizedString(@"Cancel", "rename sheet button")];
}

- (IBAction) rename: (id) sender;
{
    void (^completionHandler)(BOOL) = ^(BOOL didRename) {
        if (didRename)
            [NSApp endSheet: [self window] returnCode: NSOKButton];
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
    [NSApp endSheet: [self window] returnCode: NSCancelButton];
}

- (void) controlTextDidChange: (NSNotification *) notification
{
    [self.renameButton setEnabled: ![[self.inputField stringValue] isEqualToString: @""] && ![[self.inputField stringValue] isEqualToString: [self.torrent name]]];
}

@end
