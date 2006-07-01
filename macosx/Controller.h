/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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
#import "Badger.h"
#import "SmoothAquaView.h"

@class TorrentTableView;

@interface Controller : NSObject
{
    tr_handle_t                 * fLib;
    int                         fCompleted;
    NSMutableArray              * fTorrents;
    
    PrefsController             * fPrefsController;
    NSUserDefaults              * fDefaults;
    InfoWindowController        * fInfoController;

    IBOutlet NSWindow           * fWindow;
    IBOutlet NSScrollView       * fScrollView;
    IBOutlet TorrentTableView   * fTableView;
    NSToolbar                   * fToolbar;
    
    IBOutlet NSMenuItem         * fAdvancedBarItem, * fSpeedLimitItem, * fSpeedLimitDockItem;
    IBOutlet NSButton           * fActionButton, * fSpeedLimitButton;
    
    IBOutlet SmoothAquaView     * fStatusBar;
    BOOL                        fStatusBarVisible;
    IBOutlet NSTextField        * fTotalDLField;
    IBOutlet NSTextField        * fTotalULField;
    IBOutlet NSTextField        * fTotalTorrentsField;
    
    NSString                    * fSortType;
    IBOutlet NSMenuItem         * fNameSortItem, * fStateSortItem, * fProgressSortItem,
                                * fDateSortItem, * fOrderSortItem;
                                
    IBOutlet NSMenuItem         * fNextInfoTabItem, * fPrevInfoTabItem;
    
    IBOutlet NSMenu             * fUploadMenu, * fDownloadMenu;
    IBOutlet NSMenuItem         * fUploadLimitItem, * fUploadNoLimitItem,
                                * fDownloadLimitItem, * fDownloadNoLimitItem,
                                * fRatioSetItem, * fRatioNotSetItem;

    io_connect_t                fRootPort;
    NSTimer                     * fTimer;
    
    BOOL                        fHasGrowl;
    Badger                      * fBadger;
    BOOL                        fUpdateInProgress;
}

- (void) openShowSheet:   (id) sender;
- (void) openSheetClosed: (NSOpenPanel *) s returnCode: (int) code
                        contextInfo: (void *) info;

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
                            contextInfo: (void *) contextInfo;

- (NSArray *) torrentsAtIndexes: (NSIndexSet *) indexSet;
- (void) torrentNumberChanged;

- (void) resumeTorrent:             (id) sender;
- (void) resumeAllTorrents:         (id) sender;
- (void) resumeTorrentWithIndex:    (NSIndexSet *) indexSet;
- (void) stopTorrent:               (id) sender;
- (void) stopAllTorrents:           (id) sender;
- (void) stopTorrentWithIndex:      (NSIndexSet *) indexSet;

- (void) removeWithIndex: (NSIndexSet *) indexSet
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteData;
- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
                        contextInfo: (NSDictionary *) dict;
- (void) confirmRemove: (NSArray *) torrents
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent;
- (void) removeNoDelete:                (id) sender;
- (void) removeDeleteData:       (id) sender;
- (void) removeDeleteTorrent:    (id) sender;
- (void) removeDeleteBoth:       (id) sender;

- (void) copyTorrentFile: (id) sender;
- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents;
- (void) saveTorrentCopySheetClosed: (NSSavePanel *) panel returnCode: (int) code
    contextInfo: (NSMutableArray *) torrents;

- (void) revealFile: (id) sender;

- (void) showPreferenceWindow: (id) sender;

- (void) showInfo: (id) sender;
- (void) setInfoTab: (id) sender;

- (void) updateUI: (NSTimer *) timer;
- (void) updateTorrentHistory;

- (void) sortTorrents;
- (void) setSort: (id) sender;

- (void) toggleSpeedLimit: (id) sender;

- (void) setLimitGlobalEnabled: (id) sender;
- (void) setQuickLimitGlobal: (id) sender;
- (void) limitGlobalChange: (NSNotification *) notification;

- (void) setRatioGlobalEnabled: (id) sender;
- (void) setQuickRatioGlobal: (id) sender;
- (void) ratioGlobalChange: (NSNotification *) notification;

- (void) checkWaitingForStopped: (NSNotification *) notification;
- (void) checkWaitingForFinished: (Torrent *) finishedTorrent;
- (void) torrentStartSettingChange: (NSNotification *) notification;
- (void) globalStartSettingChange: (NSNotification *) notification;

- (void) attemptToStartAuto: (Torrent *) torrent;

- (void) reloadInspector: (NSNotification *) notification;

- (void) sleepCallBack: (natural_t) messageType argument:
                        (void *) messageArgument;

- (void) toggleStatusBar: (id) sender;
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate;
- (void) toggleAdvancedBar: (id) sender;

- (void) showMainWindow:    (id) sender;
- (void) linkHomepage:      (id) sender;
- (void) linkForums:        (id) sender;

- (void) notifyGrowl:       (NSString *) file;
- (void) growlRegister;

- (void) checkUpdate:       (id) sender;
- (void) prepareForUpdate:  (NSNotification *) notification;

@end

#endif
