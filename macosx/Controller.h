/******************************************************************************
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
#import "Badger.h"

@class TorrentTableView;

@interface Controller : NSObject
{
    tr_handle_t                 * fLib;
    int                         fCompleted;
    NSMutableArray              * fTorrents;

    NSToolbar                   * fToolbar;

    IBOutlet NSMenuItem         * fAdvancedBarItem;
    IBOutlet NSMenuItem         * fPauseResumeItem;
    IBOutlet NSMenuItem         * fRemoveItem;
    IBOutlet NSMenuItem         * fRemoveTorrentItem;
    IBOutlet NSMenuItem         * fRemoveDataItem;
    IBOutlet NSMenuItem         * fRemoveBothItem;
    IBOutlet NSMenuItem         * fRevealItem;
    IBOutlet NSMenuItem         * fShowHideToolbar;

    IBOutlet NSWindow           * fWindow;
    IBOutlet NSScrollView       * fScrollView;
    IBOutlet TorrentTableView   * fTableView;
    IBOutlet NSTextField        * fTotalDLField;
    IBOutlet NSTextField        * fTotalULField;
    IBOutlet NSTextField        * fTotalTorrentsField;
    IBOutlet NSBox              * fStats;
    BOOL                        fStatusBar;

    IBOutlet NSPanel            * fInfoPanel;
    IBOutlet NSImageView        * fInfoImageView;
    IBOutlet NSTextField        * fInfoName;
    IBOutlet NSTextField        * fInfoSize;
    IBOutlet NSTextField        * fInfoTracker;
    IBOutlet NSTextField        * fInfoAnnounce;
    IBOutlet NSTextField        * fInfoPieceSize;
    IBOutlet NSTextField        * fInfoPieces;
    IBOutlet NSTextField        * fInfoHash1;
    IBOutlet NSTextField        * fInfoHash2;
    IBOutlet NSTextField        * fInfoSeeders;
    IBOutlet NSTextField        * fInfoLeechers;
    IBOutlet NSTextField        * fInfoDownloaded;
    IBOutlet NSTextField        * fInfoUploaded;
    NSImage                     * fAppIcon;

    io_connect_t                fRootPort;
    NSTimer                     * fTimer;
    NSTimer                     * fUpdateTimer;
    
    IBOutlet NSPanel            * fPrefsWindow;
    IBOutlet PrefsController    * fPrefsController;
    NSUserDefaults              * fDefaults;
    
    BOOL                        fHasGrowl;
    Badger                      * fBadger; 
    BOOL                        fCheckIsAutomatic;
}

- (void) advancedChanged: (id) sender;
- (void) openShowSheet:   (id) sender;
- (void) openSheetClosed: (NSOpenPanel *) s returnCode: (int) code
                        contextInfo: (void *) info;

- (void) quitSheetDidEnd:   (NSWindow *)sheet returnCode:(int)returnCode
                            contextInfo:(void *)contextInfo;
                        
- (void) resumeTorrent:             (id) sender;
- (void) resumeAllTorrents:         (id) sender;
- (void) resumeTorrentWithIndex:    (NSIndexSet *) indexSet;
- (void) stopTorrent:               (id) sender;
- (void) stopAllTorrents:           (id) sender;
- (void) stopTorrentWithIndex:      (NSIndexSet *) indexSet;

- (void) removeTorrent:             (id) sender;
- (void) removeTorrentDeleteFile:   (id) sender;
- (void) removeTorrentDeleteData:   (id) sender;
- (void) removeTorrentDeleteBoth:   (id) sender;
- (void) removeTorrentWithIndex:    (NSIndexSet *) indexSet
                deleteTorrent:      (BOOL) deleteTorrent
                deleteData:         (BOOL) deleteData;
                
- (void) removeSheetDidEnd:(NSWindow *)sheet returnCode:(int)returnCode
                        contextInfo:(NSDictionary *)dict;
- (void) confirmRemoveTorrentWithIndex: (NSIndexSet *) indexSet
            deleteTorrent: (BOOL) deleteTorrent
            deleteData: (BOOL) deleteData;
                     
- (void) showInfo:        (id) sender;

- (void) updateUI:        (NSTimer *) timer;
- (void) updateTorrentHistory;
- (void) sleepCallBack:   (natural_t) messageType argument:
                        (void *) messageArgument;

- (void) runCustomizationPalette: (id) sender;
- (void) showHideToolbar: (id) sender;
- (void) toggleStatusBar: (id) sender;

- (void) showPreferenceWindow: (id) sender;

- (void) showMainWindow:    (id) sender;
- (void) linkHomepage:      (id) sender;
- (void) linkForums:        (id) sender;
- (void) notifyGrowl:       (NSString *) file;
- (void) revealFromMenu:    (id) sender;
- (void) growlRegister:     (id) sender;

- (void) checkForUpdate:      (id) sender;
- (void) checkForUpdateTimer: (NSTimer *) timer;
- (void) checkForUpdateAuto:  (BOOL) automatic;

@end

#endif
