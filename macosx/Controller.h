/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2009 Transmission authors and contributors
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

@class AddWindowController;
@class Badger;
@class DragOverlayWindow;
@class FilterButton;
@class InfoWindowController;
@class MessageWindowController;
@class PrefsController;
@class StatusBarView;
@class Torrent;
@class TorrentTableView;

typedef enum
{
    ADD_MANUAL,
    ADD_AUTO,
    ADD_SHOW_OPTIONS,
    ADD_URL,
    ADD_CREATED
} addType;

#warning uncomment
@interface Controller : NSObject <GrowlApplicationBridgeDelegate> //, QLPreviewPanelDataSource, QLPreviewPanelDelegate>
{
    tr_session                      * fLib;
    
    NSMutableArray                  * fTorrents, * fDisplayedTorrents;
    
    PrefsController                 * fPrefsController;
    InfoWindowController            * fInfoController;
    MessageWindowController         * fMessageController;
    
    NSUserDefaults                  * fDefaults;
    
    IBOutlet NSWindow               * fWindow;
    DragOverlayWindow               * fOverlayWindow;
    IBOutlet TorrentTableView       * fTableView;

    io_connect_t                    fRootPort;
    NSTimer                         * fTimer;
    
    IBOutlet NSMenuItem             * fOpenIgnoreDownloadFolder;
    IBOutlet NSButton               * fActionButton, * fSpeedLimitButton;
    IBOutlet NSTextField            * fTotalTorrentsField;
    
    IBOutlet StatusBarView          * fStatusBar;
    IBOutlet NSButton               * fStatusButton;
    IBOutlet NSTextField            * fTotalDLField, * fTotalULField;
    IBOutlet NSImageView            * fTotalDLImageView;
    
    IBOutlet StatusBarView          * fFilterBar;
    IBOutlet FilterButton           * fNoFilterButton, * fActiveFilterButton, * fDownloadFilterButton,
                                    * fSeedFilterButton, * fPauseFilterButton;
    IBOutlet NSSearchField          * fSearchFilterField;
    IBOutlet NSMenuItem             * fNextFilterItem, * fPrevFilterItem;
                                
    IBOutlet NSMenuItem             * fNextInfoTabItem, * fPrevInfoTabItem;
    
    IBOutlet NSMenu                 * fUploadMenu, * fDownloadMenu;
    IBOutlet NSMenuItem             * fUploadLimitItem, * fUploadNoLimitItem,
                                    * fDownloadLimitItem, * fDownloadNoLimitItem;
    
    IBOutlet NSMenu                 * fRatioStopMenu;
    IBOutlet NSMenuItem             * fCheckRatioItem, * fNoCheckRatioItem;
    
    IBOutlet NSMenu                 * fGroupsSetMenu, * fGroupsSetContextMenu, * fGroupFilterMenu;
    IBOutlet NSPopUpButton          * fGroupsButton;
    
    IBOutlet NSWindow               * fURLSheetWindow;
    IBOutlet NSTextField            * fURLSheetTextField;
    IBOutlet NSButton               * fURLSheetOpenButton;
    
    #warning change to QLPreviewPanel
    id                              fPreviewPanel;
    BOOL                            fQuitting;
    
    BOOL                            fUpdateInProgress;
    BOOL                            fPauseOnLaunch;
    
    Badger                          * fBadger;
    IBOutlet NSMenu                 * fDockMenu;
    
    NSMutableArray                  * fAutoImportedNames;
    NSTimer                         * fAutoImportTimer;
    
    NSMutableDictionary             * fPendingTorrentDownloads;
    
    BOOL                            fSoundPlaying;
}

- (void) openFiles:             (NSArray *) filenames addType: (addType) type forcePath: (NSString *) path;
- (void) askOpenConfirmed:      (AddWindowController *) addController add: (BOOL) add;
- (void) openCreatedFile:       (NSNotification *) notification;
- (void) openFilesWithDict:     (NSDictionary *) dictionary;
- (void) openShowSheet:         (id) sender;

- (void) invalidOpenAlert: (NSString *) filename;
- (void) duplicateOpenAlert: (NSString *) name;

- (void) openURL:               (NSURL *) torrentURL;
- (void) openURLEndSheet:       (id) sender;
- (void) openURLCancelEndSheet: (id) sender;
- (void) openURLShowSheet:      (id) sender;

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo;

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

- (void) moveDataFilesSelected: (id) sender;
- (void) moveDataFiles: (NSArray *) torrents;
- (void) moveDataFileChoiceClosed: (NSOpenPanel *) panel returnCode: (NSInteger) code contextInfo: (NSArray *) torrents;

- (void) copyTorrentFiles: (id) sender;
- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents;

- (void) revealFile: (id) sender;

- (void) announceSelectedTorrents: (id) sender;

- (void) verifySelectedTorrents: (id) sender;
- (void) verifyTorrents: (NSArray *) torrents;

- (void) showPreferenceWindow: (id) sender;

- (void) showAboutWindow: (id) sender;

- (void) showInfo: (id) sender;
- (void) resetInfo;
- (void) setInfoTab: (id) sender;

- (void) showMessageWindow: (id) sender;
- (void) showStatsWindow: (id) sender;

- (void) updateUI;

- (void) resizeStatusButton;
- (void) setBottomCountText: (BOOL) filtering;

- (void) updateSpeedFieldsToolTips;

- (void) updateTorrentsInQueue;
- (NSUInteger) numToStartFromQueue: (BOOL) downloadQueue;

- (void) torrentFinishedDownloading: (NSNotification *) notification;
- (void) torrentRestartedDownloading: (NSNotification *) notification;
- (void) torrentStoppedForRatio: (NSNotification *) notification;

- (void) updateTorrentHistory;

- (void) applyFilter: (id) sender;

- (void) sortTorrents;
- (void) sortTorrentsIgnoreSelected;
- (void) setSort: (id) sender;
- (void) setSortByGroup: (id) sender;
- (void) setSortReverse: (id) sender;

- (void) setFilter: (id) sender;
- (void) setFilterSearchType: (id) sender;
- (void) switchFilter: (id) sender;

- (void) setStatusLabel: (id) sender;

- (void) setGroup: (id) sender; //used by delegate-generated menu items
- (void) setGroupFilter: (id) sender;
- (void) updateGroupsFilterButton;
- (void) updateGroupsFilters: (NSNotification *) notification;

- (void) toggleSpeedLimit: (id) sender;
- (void) speedLimitChanged: (id) sender;
- (void) altSpeedToggledCallbackIsLimited: (NSDictionary *) dict;

- (void) setLimitGlobalEnabled: (id) sender;
- (void) setQuickLimitGlobal: (id) sender;

- (void) setRatioGlobalEnabled: (id) sender;
- (void) setQuickRatioGlobal: (id) sender;

- (void) changeAutoImport;
- (void) checkAutoImportDirectory;

- (void) beginCreateFile: (NSNotification *) notification;

- (void) sleepCallback: (natural_t) messageType argument: (void *) messageArgument;

- (void) torrentTableViewSelectionDidChange: (NSNotification *) notification;

- (void) toggleSmallView: (id) sender;
- (void) togglePiecesBar: (id) sender;
- (void) toggleAvailabilityBar: (id) sender;
- (void) toggleStatusString: (id) sender;

- (void) toggleStatusBar: (id) sender;
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate;
- (void) toggleFilterBar: (id) sender;
- (void) showFilterBar: (BOOL) show animate: (BOOL) animate;
- (void) focusFilterField;

- (void) allToolbarClicked: (id) sender;
- (void) selectedToolbarClicked: (id) sender;

- (void) setWindowSizeToFit;
- (NSRect) sizedWindowFrame;

- (void) updateForExpandCollape;

- (void) showMainWindow: (id) sender;

- (void) toggleQuickLook: (id) sender;

- (void) linkHomepage: (id) sender;
- (void) linkForums: (id) sender;
- (void) linkTrac: (id) sender;
- (void) linkDonate: (id) sender;

- (void) rpcCallback: (tr_rpc_callback_type) type forTorrentStruct: (struct tr_torrent *) torrentStruct;
- (void) rpcAddTorrentStruct: (NSValue *) torrentStructPtr;
- (void) rpcRemoveTorrent: (Torrent *) torrent;
- (void) rpcStartedStoppedTorrent: (Torrent *) torrent;
- (void) rpcChangedTorrent: (Torrent *) torrent;
- (void) rpcMovedTorrent: (Torrent *) torrent;

@end
