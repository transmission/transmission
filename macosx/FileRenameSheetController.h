// This file Copyright © 2013-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.
// Created by Mitchell Livingston on 1/20/13.

#import <AppKit/AppKit.h>

@class FileListNode;
@class Torrent;

@interface FileRenameSheetController : NSWindowController

+ (void)presentSheetForTorrent:(Torrent*)torrent
                modalForWindow:(NSWindow*)window
             completionHandler:(void (^)(BOOL didRename))completionHandler;
+ (void)presentSheetForFileListNode:(FileListNode*)node
                     modalForWindow:(NSWindow*)window
                  completionHandler:(void (^)(BOOL didRename))completionHandler;

- (IBAction)rename:(id)sender;
- (IBAction)cancelRename:(id)sender;

@end
