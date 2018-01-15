/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import <Cocoa/Cocoa.h>
#import <transmission.h>
#import <Quartz/Quartz.h>
#import <Growl/Growl.h>
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
@class TorrentTableView;
@class URLSheetWindowController;

typedef enum
{
    ADD_MANUAL,
    ADD_AUTO,
    ADD_SHOW_OPTIONS,
    ADD_URL,
    ADD_CREATED
} addType;

@interface Controller : NSObject <GrowlApplicationBridgeDelegate, NSURLDownloadDelegate, NSUserNotificationCenterDelegate, NSPopoverDelegate, NSSharingServiceDelegate, NSSharingServicePickerDelegate, NSSoundDelegate, NSToolbarDelegate, NSWindowDelegate, QLPreviewPanelDataSource, QLPreviewPanelDelegate, VDKQueueDelegate>
{
    tr_session                      * fLib;
    
    NSMutableArray                  * fTorrents, * fDisplayedTorrents;
    
    PrefsController                 * fPrefsController;
    InfoWindowController            * fInfoController;
    MessageWindowController         * fMessageController;
    
    NSUserDefaults                  * fDefaults;
    
    NSString                        * fConfigDirectory;
    
    IBOutlet NSWindow               * fWindow;
    DragOverlayWindow               * fOverlayWindow;
    IBOutlet TorrentTableView       * fTableView;

    io_connect_t                    fRootPort;
    NSTimer                         * fTimer;
    
    VDKQueue                        * fFileWatcherQueue;
    
    IBOutlet NSMenuItem             * fOpenIgnoreDownloadFolder;
    IBOutlet NSButton               * fActionButton, * fSpeedLimitButton, * fClearCompletedButton;
    IBOutlet NSTextField            * fTotalTorrentsField;
    
    StatusBarController             * fStatusBar;
    
    FilterBarController             * fFilterBar;
    IBOutlet NSMenuItem             * fNextFilterItem;
                                
    IBOutlet NSMenuItem             * fNextInfoTabItem, * fPrevInfoTabItem;
    
    IBOutlet NSMenu                 * fSortMenu;
    
    IBOutlet NSMenu                 * fGroupsSetMenu, * fGroupsSetContextMenu;
    
    IBOutlet NSMenu                 * fShareMenu, * fShareContextMenu;
    IBOutlet NSMenuItem             * fShareMenuItem, * fShareContextMenuItem; // remove when dropping 10.6
    
    QLPreviewPanel                  * fPreviewPanel;
    BOOL                            fQuitting;
    BOOL                            fQuitRequested;
    BOOL                            fPauseOnLaunch;
    
    Badger                          * fBadger;
    
    NSMutableArray                  * fAutoImportedNames;
    NSTimer                         * fAutoImportTimer;
    
    NSMutableDictionary             * fPendingTorrentDownloads;
    
    NSMutableSet                    * fAddingTransfers;
    
    NSMutableSet                    * fAddWindows;
    URLSheetWindowController        * fUrlSheetController;
    
    BOOL                            fGlobalPopoverShown;
    BOOL                            fSoundPlaying;
}

- (void) openFiles: (NSArray *) filenames addType: (addType) type forcePath: (NSString *) path;

- (void) askOpenConfirmed: (AddWindowController *) addController add: (BOOL) add;
- (void) openCreatedFile: (NSNotification *) notification;
- (void) openFilesWithDict: (NSDictionary *) dictionary;
- (void) openShowSheet: (id) sender;

- (void) openMagnet: (NSString *) address;
- (void) askOpenMagnetConfirmed: (AddMagnetWindowController *) addController add: (BOOL) add;

- (void) invalidOpenAlert: (NSString *) filename;
- (void) invalidOpenMagnetAlert: (NSString *) address;
- (void) duplicateOpenAlert: (NSString *) name;
- (void) duplicateOpenMagnetAlert: (NSString *) address transferName: (NSString *) name;

- (void) openURL: (NSString *) urlString;
- (void) openURLShowSheet: (id) sender;

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo;

- (tr_session *) sessionHandle;

- (void) createFile: (id) sender;

- (void) resumeSelectedTorrents:    (id) sender;
- (void) resumeAllTorrents:         (id) sender;
- (void) resumeTorrents:            (NSArray *) torrents;

- (void) resumeSelectedTorrentsNoWait:  (id) sender;
- (void) resumeWaitingTorrents:         (id) sender;
- (void) resumeTorrentsNoWait:          (NSArray *) torrents;

- (void) stopSelectedTorrents:      (id) sender;
- (void) stopAllTorrents:           (id) sender;
- (void) stopTorrents:              (NSArray *) torrents;

- (void) removeTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData;
- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode
                        contextInfo: (NSDictionary *) dict;
- (void) confirmRemoveTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData;
- (void) removeNoDelete:                (id) sender;
- (void) removeDeleteData:              (id) sender;

- (void) clearCompleted: (id) sender;

- (void) moveDataFilesSelected: (id) sender;
- (void) moveDataFiles: (NSArray *) torrents;

- (void) copyTorrentFiles: (id) sender;
- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents;

- (void) copyMagnetLinks: (id) sender;

- (void) revealFile: (id) sender;

- (IBAction) renameSelected: (id) sender;

- (void) announceSelectedTorrents: (id) sender;

- (void) verifySelectedTorrents: (id) sender;
- (void) verifyTorrents: (NSArray *) torrents;

- (NSArray *)selectedTorrents;

@property (retain, readonly) PrefsController * prefsController;
- (void) showPreferenceWindow: (id) sender;

- (void) showAboutWindow: (id) sender;

- (void) showInfo: (id) sender;
- (void) resetInfo;
- (void) setInfoTab: (id) sender;

@property (retain, readonly) MessageWindowController * messageWindowController;
- (void) showMessageWindow: (id) sender;
- (void) showStatsWindow: (id) sender;

- (void) updateUI;
- (void) fullUpdateUI;

- (void) setBottomCountText: (BOOL) filtering;

- (Torrent *) torrentForHash: (NSString *) hash;

- (void) torrentFinishedDownloading: (NSNotification *) notification;
- (void) torrentRestartedDownloading: (NSNotification *) notification;
- (void) torrentFinishedSeeding: (NSNotification *) notification;

- (void) updateTorrentHistory;

- (void) applyFilter;

- (void) sortTorrents: (BOOL) includeQueueOrder;
- (void) sortTorrentsCallUpdates: (BOOL) callUpdates includeQueueOrder: (BOOL) includeQueueOrder;
- (void) rearrangeTorrentTableArray: (NSMutableArray *) rearrangeArray forParent: (id) parent withSortDescriptors: (NSArray *) descriptors beganTableUpdate: (BOOL *) beganTableUpdate;
- (void) setSort: (id) sender;
- (void) setSortByGroup: (id) sender;
- (void) setSortReverse: (id) sender;

- (void) switchFilter: (id) sender;

- (IBAction) showGlobalPopover: (id) sender;

- (void) setGroup: (id) sender; //used by delegate-generated menu items

- (void) toggleSpeedLimit: (id) sender;
- (void) speedLimitChanged: (id) sender;
- (void) altSpeedToggledCallbackIsLimited: (NSDictionary *) dict;

- (void) changeAutoImport;
- (void) checkAutoImportDirectory;

- (void) beginCreateFile: (NSNotification *) notification;

- (void) sleepCallback: (natural_t) messageType argument: (void *) messageArgument;

@property (retain, readonly) VDKQueue * fileWatcherQueue;

- (void) torrentTableViewSelectionDidChange: (NSNotification *) notification;

- (void) toggleSmallView: (id) sender;
- (void) togglePiecesBar: (id) sender;
- (void) toggleAvailabilityBar: (id) sender;

- (void) toggleStatusBar: (id) sender;
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate;
- (void) toggleFilterBar: (id) sender;
- (void) showFilterBar: (BOOL) show animate: (BOOL) animate;
- (void) focusFilterField;

- (void) allToolbarClicked: (id) sender;
- (void) selectedToolbarClicked: (id) sender;

- (void) setWindowSizeToFit;
- (NSRect) sizedWindowFrame;
- (void) updateForAutoSize;
- (void) setWindowMinMaxToCurrent;
- (CGFloat) minWindowContentSizeAllowed;

- (void) updateForExpandCollape;

- (void) showMainWindow: (id) sender;

- (void) toggleQuickLook: (id) sender;

- (void) linkHomepage: (id) sender;
- (void) linkForums: (id) sender;
- (void) linkGitHub: (id) sender;
- (void) linkDonate: (id) sender;

- (void) rpcCallback: (tr_rpc_callback_type) type forTorrentStruct: (struct tr_torrent *) torrentStruct;
- (void) rpcAddTorrentStruct: (struct tr_torrent *) torrentStruct;
- (void) rpcRemoveTorrent: (Torrent *) torrent deleteData: (BOOL) deleteData;
- (void) rpcStartedStoppedTorrent: (Torrent *) torrent;
- (void) rpcChangedTorrent: (Torrent *) torrent;
- (void) rpcMovedTorrent: (Torrent *) torrent;
- (void) rpcUpdateQueue;

@end
