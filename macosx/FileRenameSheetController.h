//
//  FileRenameSheetController.h
//  Transmission
//
//  Created by Mitchell Livingston on 1/20/13.
//  Copyright (c) 2013 The Transmission Project. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@class FileListNode;
@class Torrent;

@interface FileRenameSheetController : NSWindowController

+ (void) presentSheetForTorrent: (Torrent *) torrent modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler;
+ (void) presentSheetForFileListNode: (FileListNode *) node modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler;

@property (weak) IBOutlet NSTextField * labelField;
@property (weak) IBOutlet NSTextField * inputField;
@property (weak) IBOutlet NSButton * renameButton;
@property (weak) IBOutlet NSButton * cancelButton;

- (IBAction) rename: (id) sender;
- (IBAction) cancelRename: (id) sender;

@end
