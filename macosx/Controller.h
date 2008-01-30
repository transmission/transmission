/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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
#import "PrefsController.h"
#import "InfoWindowController.h"
#import "MessageWindowController.h"
#import "AddWindowController.h"
#import "DragOverlayWindow.h"
#import "Badger.h"
#import "StatusBarView.h"
#import "FilterButton.h"
#import "MenuLabel.h"
#import "IPCController.h"

#import <Growl/Growl.h>

@class TorrentTableView;

typedef enum
{
    ADD_NORMAL,
    ADD_SHOW_OPTIONS,
    ADD_URL,
    ADD_CREATED
} addType;

@interface Controller : NSObject <GrowlApplicationBridgeDelegate>
{
    tr_handle                       * fLib;
    
    NSMutableArray                  * fTorrents, * fDisplayedTorrents;
    NSMutableIndexSet               * fDisplayedGroupIndexes;
    
    PrefsController                 * fPrefsController;
    InfoWindowController            * fInfoController;
    MessageWindowController         * fMessageController;
    IPCController                   * fIPCController;
    
    NSUserDefaults                  * fDefaults;
    
    IBOutlet NSWindow               * fWindow;
    DragOverlayWindow               * fOverlayWindow;
    IBOutlet NSScrollView           * fScrollView;
    IBOutlet TorrentTableView       * fTableView;
    
    IBOutlet NSMenuItem             * fOpenIgnoreDownloadFolder;
    
    IBOutlet StatusBarView          * fBottomTigerBar;
    IBOutlet NSBox                  * fBottomTigerLine;
    IBOutlet NSButton               * fActionButton, * fSpeedLimitButton;
    NSTimer                         * fSpeedLimitTimer;
    IBOutlet NSTextField            * fTotalTorrentsField;
    
    IBOutlet StatusBarView          * fStatusBar;
    IBOutlet NSButton               * fStatusButton;
    IBOutlet MenuLabel              * fStatusTigerField;
    IBOutlet NSImageView            * fStatusTigerImageView;
    IBOutlet NSTextField            * fTotalDLField, * fTotalULField;
    
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

    io_connect_t                    fRootPort;
    NSTimer                         * fTimer;
    
    IBOutlet SUUpdater              * fUpdater;
    BOOL                            fUpdateInProgress;
    
    Badger                          * fBadger;
    IBOutlet NSMenu                 * fDockMenu;
    
    NSMutableArray                  * fAutoImportedNames;
    NSTimer                         * fAutoImportTimer;
    
    NSMutableDictionary             * fPendingTorrentDownloads;
    NSMutableArray                  * fTempTorrentFiles;

    BOOL                            fRemoteQuit;
}

- (void) openFiles:             (NSArray *) filenames addType: (addType) type forcePath: (NSString *) path;
- (void) askOpenConfirmed:      (AddWindowController *) addController add: (BOOL) add;
- (void) openCreatedFile:       (NSNotification *) notification;
- (void) openFilesWithDict:     (NSDictionary *) dictionary;
- (void) openShowSheet:         (id) sender;

- (void) duplicateOpenAlert: (NSString *) name;

- (void) openURL:               (NSURL *) torrentURL;
- (void) openURLEndSheet:       (id) sender;
- (void) openURLCancelEndSheet: (id) sender;
- (void) openURLShowSheet:      (id) sender;

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (void *) contextInfo;

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

- (void) removeTorrents: (NSArray *) torrents
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteData;
- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
                        contextInfo: (NSDictionary *) dict;
- (void) confirmRemoveTorrents: (NSArray *) torrents
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent;
- (void) removeNoDelete:                (id) sender;
- (void) removeDeleteData:              (id) sender;
- (void) removeDeleteTorrent:           (id) sender;
- (void) removeDeleteDataAndTorrent:    (id) sender;

- (void) moveDataFiles: (id) sender;
- (void) moveDataFileChoiceClosed: (NSOpenPanel *) panel returnCode: (int) code contextInfo: (NSArray *) torrents;

- (void) copyTorrentFiles: (id) sender;
- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents;

- (void) revealFile: (id) sender;

- (void) announceSelectedTorrents: (id) sender;

- (void) resetCacheForSelectedTorrents: (id) sender;

- (void) showPreferenceWindow: (id) sender;

- (void) showAboutWindow: (id) sender;

- (void) showInfo: (id) sender;
- (void) setInfoTab: (id) sender;

- (void) showMessageWindow: (id) sender;
- (void) showStatsWindow: (id) sender;

- (void) updateUI;

- (void) updateTorrentsInQueue;
- (int) numToStartFromQueue: (BOOL) downloadQueue;

- (void) torrentFinishedDownloading: (NSNotification *) notification;
- (void) torrentRestartedDownloading: (NSNotification *) notification;

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

- (void) showGroups: (id) sender;
- (void) setGroup: (id) sender; //used by delegate-generated menu items
- (void) setGroupFilter: (id) sender;
- (void) updateGroupsFilterButton;
- (void) updateGroupsFilters: (NSNotification *) notification;

- (void) toggleSpeedLimit: (id) sender;
- (void) autoSpeedLimitChange: (NSNotification *) notification;
- (void) autoSpeedLimit;

- (void) setLimitGlobalEnabled: (id) sender;
- (void) setQuickLimitGlobal: (id) sender;

- (void) setRatioGlobalEnabled: (id) sender;
- (void) setQuickRatioGlobal: (id) sender;

- (void) torrentStoppedForRatio: (NSNotification *) notification;

- (void) changeAutoImport;
- (void) checkAutoImportDirectory;

- (void) beginCreateFile: (NSNotification *) notification;

- (void) sleepCallBack: (natural_t) messageType argument: (void *) messageArgument;

- (void) torrentTableViewSelectionDidChange: (NSNotification *) notification;

- (void) toggleSmallView: (id) sender;
- (void) togglePiecesBar: (id) sender;
- (void) toggleAvailabilityBar: (id) sender;

- (void) toggleStatusBar: (id) sender;
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate;
- (void) toggleFilterBar: (id) sender;
- (void) showFilterBar: (BOOL) show animate: (BOOL) animate;

- (void) allToolbarClicked: (id) sender;
- (void) selectedToolbarClicked: (id) sender;

- (void) setWindowSizeToFit;
- (NSRect) sizedWindowFrame;

- (void) showMainWindow: (id) sender;

- (void) linkHomepage: (id) sender;
- (void) linkForums: (id) sender;
- (void) linkDonate: (id) sender;

- (void) prepareForUpdate:  (NSNotification *) notification;

@end
