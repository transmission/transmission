// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>
#import <Quartz/Quartz.h>

#import <Sparkle/SUUpdaterDelegate.h>

#include <libtransmission/transmission.h>

#import "VDKQueue.h"

@class AddMagnetWindowController;
@class AddWindowController;
@class Badger;
@class DragOverlayWindow;
@class FilterBarController;
@class InfoWindowController;
@class MessageWindowController;
@class PrefsController;
@class StatusBarController;
@class Torrent;
@class URLSheetWindowController;

typedef NS_ENUM(unsigned int, addType) { //
    ADD_MANUAL,
    ADD_AUTO,
    ADD_SHOW_OPTIONS,
    ADD_URL,
    ADD_CREATED
};

@interface Controller
    : NSObject<NSApplicationDelegate, NSURLDownloadDelegate, NSUserNotificationCenterDelegate, NSPopoverDelegate, NSSharingServiceDelegate, NSSharingServicePickerDelegate, NSSoundDelegate, NSToolbarDelegate, NSWindowDelegate, QLPreviewPanelDataSource, QLPreviewPanelDelegate, VDKQueueDelegate, SUUpdaterDelegate>

- (void)openFiles:(NSArray*)filenames addType:(addType)type forcePath:(NSString*)path;

- (void)askOpenConfirmed:(AddWindowController*)addController add:(BOOL)add;
- (void)openCreatedFile:(NSNotification*)notification;
- (void)openFilesWithDict:(NSDictionary*)dictionary;
- (void)openShowSheet:(id)sender;

- (void)openMagnet:(NSString*)address;
- (void)askOpenMagnetConfirmed:(AddMagnetWindowController*)addController add:(BOOL)add;

- (void)invalidOpenAlert:(NSString*)filename;
- (void)invalidOpenMagnetAlert:(NSString*)address;
- (void)duplicateOpenAlert:(NSString*)name;
- (void)duplicateOpenMagnetAlert:(NSString*)address transferName:(NSString*)name;

- (void)openURL:(NSString*)urlString;
- (void)openURLShowSheet:(id)sender;

@property(nonatomic, readonly) tr_session* sessionHandle;

- (void)createFile:(id)sender;

- (void)resumeSelectedTorrents:(id)sender;
- (void)resumeAllTorrents:(id)sender;
- (void)resumeTorrents:(NSArray*)torrents;

- (void)resumeSelectedTorrentsNoWait:(id)sender;
- (void)resumeWaitingTorrents:(id)sender;
- (void)resumeTorrentsNoWait:(NSArray<Torrent*>*)torrents;

- (void)stopSelectedTorrents:(id)sender;
- (void)stopAllTorrents:(id)sender;
- (void)stopTorrents:(NSArray<Torrent*>*)torrents;

- (void)removeTorrents:(NSArray<Torrent*>*)torrents deleteData:(BOOL)deleteData;
- (void)confirmRemoveTorrents:(NSArray<Torrent*>*)torrents deleteData:(BOOL)deleteData;
- (void)removeNoDelete:(id)sender;
- (void)removeDeleteData:(id)sender;

- (void)clearCompleted:(id)sender;

- (void)moveDataFilesSelected:(id)sender;
- (void)moveDataFiles:(NSArray<Torrent*>*)torrents;

- (void)copyTorrentFiles:(id)sender;
- (void)copyTorrentFileForTorrents:(NSMutableArray<Torrent*>*)torrents;

- (void)copyMagnetLinks:(id)sender;

- (void)revealFile:(id)sender;

- (IBAction)renameSelected:(id)sender;

- (void)announceSelectedTorrents:(id)sender;

- (void)verifySelectedTorrents:(id)sender;
- (void)verifyTorrents:(NSArray*)torrents;

@property(nonatomic, readonly) NSArray<Torrent*>* selectedTorrents;

@property(nonatomic, readonly) PrefsController* prefsController;
- (void)showPreferenceWindow:(id)sender;

- (void)showAboutWindow:(id)sender;

- (void)showInfo:(id)sender;
- (void)resetInfo;
- (void)setInfoTab:(id)sender;

@property(nonatomic, readonly) MessageWindowController* messageWindowController;
- (void)showMessageWindow:(id)sender;
- (void)showStatsWindow:(id)sender;

- (void)updateUI;
- (void)fullUpdateUI;

- (void)setBottomCountText:(BOOL)filtering;

- (Torrent*)torrentForHash:(NSString*)hash;

- (void)torrentFinishedDownloading:(NSNotification*)notification;
- (void)torrentRestartedDownloading:(NSNotification*)notification;
- (void)torrentFinishedSeeding:(NSNotification*)notification;

- (void)updateTorrentHistory;

- (void)applyFilter;

- (void)sortTorrentsAndIncludeQueueOrder:(BOOL)includeQueueOrder;
- (void)sortTorrentsCallUpdates:(BOOL)callUpdates includeQueueOrder:(BOOL)includeQueueOrder;
- (void)rearrangeTorrentTableArray:(NSMutableArray*)rearrangeArray
                         forParent:(id)parent
               withSortDescriptors:(NSArray*)descriptors
                  beganTableUpdate:(BOOL*)beganTableUpdate;
- (void)setSort:(id)sender;
- (void)setSortByGroup:(id)sender;
- (void)setSortReverse:(id)sender;

- (void)switchFilter:(id)sender;

- (IBAction)showGlobalPopover:(id)sender;

- (void)setGroup:(id)sender; //used by delegate-generated menu items

- (void)toggleSpeedLimit:(id)sender;
- (void)speedLimitChanged:(id)sender;
- (void)altSpeedToggledCallbackIsLimited:(NSDictionary*)dict;

- (void)changeAutoImport;
- (void)checkAutoImportDirectory;

- (void)beginCreateFile:(NSNotification*)notification;

- (void)sleepCallback:(natural_t)messageType argument:(void*)messageArgument;

@property(nonatomic, readonly) VDKQueue* fileWatcherQueue;

- (void)torrentTableViewSelectionDidChange:(NSNotification*)notification;

- (void)toggleSmallView:(id)sender;
- (void)togglePiecesBar:(id)sender;
- (void)toggleAvailabilityBar:(id)sender;

- (void)toggleStatusBar:(id)sender;
- (void)showStatusBar:(BOOL)show animate:(BOOL)animate;
- (void)toggleFilterBar:(id)sender;
- (void)showFilterBar:(BOOL)show animate:(BOOL)animate;
- (void)focusFilterField;

- (void)allToolbarClicked:(id)sender;
- (void)selectedToolbarClicked:(id)sender;

- (void)setWindowSizeToFit;
@property(nonatomic, readonly) NSRect sizedWindowFrame;
- (void)updateForAutoSize;
- (void)setWindowMinMaxToCurrent;
@property(nonatomic, readonly) CGFloat minWindowContentSizeAllowed;

- (void)updateForExpandCollapse;

- (void)showMainWindow:(id)sender;

- (void)toggleQuickLook:(id)sender;

- (void)linkHomepage:(id)sender;
- (void)linkForums:(id)sender;
- (void)linkGitHub:(id)sender;
- (void)linkDonate:(id)sender;

- (void)rpcCallback:(tr_rpc_callback_type)type forTorrentStruct:(struct tr_torrent*)torrentStruct;
- (void)rpcAddTorrentStruct:(struct tr_torrent*)torrentStruct;
- (void)rpcRemoveTorrent:(Torrent*)torrent deleteData:(BOOL)deleteData;
- (void)rpcStartedStoppedTorrent:(Torrent*)torrent;
- (void)rpcChangedTorrent:(Torrent*)torrent;
- (void)rpcMovedTorrent:(Torrent*)torrent;
- (void)rpcUpdateQueue;

@end
