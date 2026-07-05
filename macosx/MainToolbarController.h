// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

@protocol MainToolbarControllerDelegate<NSObject>

- (NSArray<Torrent*>*)mainToolbarAllTorrents;
- (NSArray<Torrent*>*)mainToolbarSelectedTorrents;
- (NSInteger)mainToolbarSelectedTorrentCount;

- (BOOL)mainToolbarInfoVisible;
- (BOOL)mainToolbarFilterBarVisible;
- (BOOL)mainToolbarQuickLookVisible;

- (void)mainToolbarCreateFile:(id)sender;
- (void)mainToolbarOpenFile:(id)sender;
- (void)mainToolbarOpenURL:(id)sender;
- (void)mainToolbarRemoveSelected:(id)sender;
- (void)mainToolbarToggleInfo:(id)sender;
- (void)mainToolbarToggleFilterBar:(id)sender;
- (void)mainToolbarToggleQuickLook:(id)sender;
- (void)mainToolbarShowShare:(id)sender;
- (void)mainToolbarStopAllTorrents:(id)sender;
- (void)mainToolbarResumeAllTorrents:(id)sender;
- (void)mainToolbarStopSelectedTorrents:(id)sender;
- (void)mainToolbarResumeSelectedTorrents:(id)sender;

@end

@interface MainToolbarController : NSObject<NSToolbarDelegate, NSToolbarItemValidation>

- (instancetype)initWithDelegate:(id<MainToolbarControllerDelegate>)delegate;
- (NSToolbar*)createToolbar;

@end
