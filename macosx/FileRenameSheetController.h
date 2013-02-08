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
{
    Torrent * _torrent;
    FileListNode * _node;
    void (^_completionHandler)(BOOL);
    NSString * _originalName;
    
    IBOutlet NSTextField * _labelField;
    IBOutlet NSTextField * _inputField;
    IBOutlet NSButton * _renameButton;
    IBOutlet NSButton * _cancelButton;
}

+ (void) presentSheetForTorrent: (Torrent *) torrent modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler;
+ (void) presentSheetForFileListNode: (FileListNode *) node modalForWindow: (NSWindow *) window completionHandler: (void (^)(BOOL didRename)) completionHandler;

@property (assign) IBOutlet NSTextField * labelField;
@property (assign) IBOutlet NSTextField * inputField;
@property (assign) IBOutlet NSButton * renameButton;
@property (assign) IBOutlet NSButton * cancelButton;

- (IBAction) rename: (id) sender;
- (IBAction) cancelRename: (id) sender;

@end
