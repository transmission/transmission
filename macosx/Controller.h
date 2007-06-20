/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#ifndef CONTROLLER_H
#define CONTROLLER_H

#import <Cocoa/Cocoa.h>
#import <transmission.h>
#import "PrefsController.h"
#import "InfoWindowController.h"
#import "MessageWindowController.h"
#import "DragOverlayWindow.h"
#import "Badger.h"
#import "ImageBackgroundView.h"
#import "FilterBarView.h"
#import "IPCController.h"

#import <Growl/Growl.h>

@class TorrentTableView;

@interface Controller : NSObject <GrowlApplicationBridgeDelegate>
{
    tr_handle_t                     * fLib;
    
    NSMutableArray                  * fTorrents, * fDisplayedTorrents;
    
    PrefsController                 * fPrefsController;
    NSUserDefaults                  * fDefaults;
    InfoWindowController            * fInfoController;
    MessageWindowController         * fMessageController;
    IPCController                   * fIPCController;

    IBOutlet NSWindow               * fWindow;
    DragOverlayWindow               * fOverlayWindow;
    IBOutlet NSScrollView           * fScrollView;
    IBOutlet TorrentTableView       * fTableView;
    NSToolbar                       * fToolbar;
    
    IBOutlet NSMenuItem             * fOpenIgnoreDownloadFolder;
    
    IBOutlet ImageBackgroundView    * fBottomBar;
    IBOutlet NSButton               * fActionButton, * fSpeedLimitButton;
    NSTimer                         * fSpeedLimitTimer;
    
    IBOutlet ImageBackgroundView    * fStatusBar;
    IBOutlet NSTextField            * fTotalDLField, * fTotalULField, * fTotalTorrentsField;
    
    IBOutlet NSMenuItem             * fNameSortItem, * fStateSortItem, * fProgressSortItem,
                                    * fDateSortItem, * fOrderSortItem,
                                    * fNameSortActionItem, * fStateSortActionItem, * fProgressSortActionItem,
                                    * fDateSortActionItem, * fOrderSortActionItem;
    
    IBOutlet FilterBarView          * fFilterBar;
    IBOutlet FilterBarButton        * fNoFilterButton, * fDownloadFilterButton,
                                    * fSeedFilterButton, * fPauseFilterButton;
    IBOutlet NSSearchField          * fSearchFilterField;
    IBOutlet NSMenuItem             * fNextFilterItem, * fPrevFilterItem;
                                
    IBOutlet NSMenuItem             * fNextInfoTabItem, * fPrevInfoTabItem;
    
    IBOutlet NSMenu                 * fUploadMenu, * fDownloadMenu;
    IBOutlet NSMenuItem             * fUploadLimitItem, * fUploadNoLimitItem,
                                    * fDownloadLimitItem, * fDownloadNoLimitItem;
    
    IBOutlet NSWindow               * fURLSheetWindow;
    IBOutlet NSTextField            * fURLSheetTextField;

    io_connect_t                    fRootPort;
    NSTimer                         * fTimer;
    
    IBOutlet SUUpdater              * fUpdater;
    BOOL                            fUpdateInProgress;
    
    Badger                          * fBadger;
    IBOutlet NSMenu                 * fDockMenu;
    
    NSMutableArray                  * fAutoImportedNames;
    NSMutableDictionary             * fPendingTorrentDownloads;
    NSTimer                         * fAutoImportTimer;

    BOOL                            fRemoteQuit;
}

- (void) openFiles:             (NSArray *) filenames;
- (void) openFiles:             (NSArray *) filenames forcePath: (NSString *) path ignoreDownloadFolder: (BOOL) ignore
                                            forceDeleteTorrent: (BOOL) delete;
- (void) openCreatedFile:       (NSNotification *) notification;
- (void) openFilesWithDict:     (NSDictionary *) dictionary;
- (void) openFilesAsk:          (NSMutableArray *) files forceDeleteTorrent: (BOOL) delete;
- (void) openFilesAskWithDict:  (NSDictionary *) dictionary;
- (void) openShowSheet:         (id) sender;
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
- (void) moveDataFileForTorrents: (NSMutableArray *) torrents;

- (void) copyTorrentFiles: (id) sender;
- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents;

- (void) revealFile: (id) sender;

- (void) announceSelectedTorrents: (id) sender;

- (void) resetCacheForSelectedTorrents: (id) sender;

- (void) showPreferenceWindow: (id) sender;

- (void) showInfo: (id) sender;
- (void) setInfoTab: (id) sender;

- (void) showMessageWindow: (id) sender;

- (void) updateControlTint: (NSNotification *) notification;

- (void) updateUI;

- (void) updateTorrentsInQueue;

- (void) torrentFinishedDownloading: (NSNotification *) notification;
- (void) torrentRestartedDownloading: (NSNotification *) notification;
- (void) updateTorrentHistory;

- (void) sortTorrents;
- (void) sortTorrentsIgnoreSelected;
- (void) setSort: (id) sender;
- (void) setSortReverse: (id) sender;
- (void) applyFilter: (id) sender;
- (void) setFilter: (id) sender;
- (void) switchFilter: (id) sender;

- (void) applySpeedLimit: (id) sender;
- (void) toggleSpeedLimit: (id) sender;
- (void) autoSpeedLimitChange: (NSNotification *) notification;
- (void) autoSpeedLimit;

- (void) setLimitGlobalEnabled: (id) sender;
- (void) setQuickLimitGlobal: (id) sender;

- (void) setQuickRatioGlobal: (id) sender;

- (void) torrentStoppedForRatio: (NSNotification *) notification;

- (void) changeAutoImport;
- (void) checkAutoImportDirectory;

- (void) sleepCallBack: (natural_t) messageType argument: (void *) messageArgument;

- (void) toggleSmallView: (id) sender;

- (void) toggleStatusBar: (id) sender;
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate;
- (void) toggleFilterBar: (id) sender;
- (void) showFilterBar: (BOOL) show animate: (BOOL) animate;

- (void) toggleAdvancedBar: (id) sender;

- (void) doNothing: (id) sender; //needed for menu items that use bindings with no associated action

- (void) updateDockBadge: (NSNotification *) notification;

- (void) setWindowSizeToFit;
- (NSRect) sizedWindowFrame;

- (void) showMainWindow:    (id) sender;
- (void) linkHomepage:      (id) sender;
- (void) linkForums:        (id) sender;

- (void) prepareForUpdate:  (NSNotification *) notification;

- (NSString *) applicationSupportFolder;

@end

#endif
