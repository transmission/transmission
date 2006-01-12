/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

#include <Cocoa/Cocoa.h>
#include <transmission.h>
#include "PrefsController.h"

@class TorrentTableView;

@interface Controller : NSObject
{
    tr_handle_t                  * fHandle;
    int                            fCount;
    tr_stat_t                    * fStat;
    int                            fResumeOnWake[TR_MAX_TORRENT_COUNT];

    NSToolbar                    * fToolbar;

    IBOutlet PrefsController     * fPrefsController;

    IBOutlet NSMenuItem          * fAdvancedBarItem;
    IBOutlet NSMenuItem          * fPauseResumeItem;
    IBOutlet NSMenuItem          * fRemoveItem;
    IBOutlet NSMenuItem          * fRevealItem;

    IBOutlet NSWindow            * fWindow;
    IBOutlet TorrentTableView    * fTableView;
    IBOutlet NSTextField         * fTotalDLField;
    IBOutlet NSTextField         * fTotalULField;
    IBOutlet NSMenu              * fContextMenu;

    IBOutlet NSPanel             * fInfoPanel;
    IBOutlet NSTextField         * fInfoTitle;
    IBOutlet NSTextField         * fInfoTracker;
    IBOutlet NSTextField         * fInfoAnnounce;
    IBOutlet NSTextField         * fInfoSize;
    IBOutlet NSTextField         * fInfoPieces;
    IBOutlet NSTextField         * fInfoPieceSize;
    IBOutlet NSTextField         * fInfoSeeders;
    IBOutlet NSTextField         * fInfoLeechers;
    IBOutlet NSTextField         * fInfoFolder;
    IBOutlet NSTextField         * fInfoDownloaded;
    IBOutlet NSTextField         * fInfoUploaded;

    io_connect_t                   fRootPort;
    NSArray * fFilenames;
    NSTimer * fTimer;
}

- (void) advancedChanged: (id) sender;
- (void) openShowSheet:   (id) sender;
- (void) openSheetClosed: (NSOpenPanel *) s returnCode: (int) code
                            contextInfo: (void *) info;
- (void) stopTorrent:     (id) sender;
- (void) stopAllTorrents: (id) sender;
- (void) stopTorrentWithIndex: (int) index;
- (void) resumeTorrent:   (id) sender;
- (void) resumeAllTorrents: (id) sender;
- (void) resumeTorrentWithIndex: (int) index;
- (void) removeTorrent:   (id) sender;
- (void) removeTorrentDeleteFile: (id) sender;
- (void) removeTorrentDeleteData: (id) sender;
- (void) removeTorrentDeleteBoth: (id) sender;
- (void) removeTorrentWithIndex: (int) idx
                  deleteTorrent: (BOOL) deleteTorrent
                     deleteData: (BOOL) deleteData;
- (void) showInfo:        (id) sender;

- (void) updateUI:        (NSTimer *) timer;
- (void) sleepCallBack:   (natural_t) messageType argument:
                            (void *) messageArgument;

- (NSMenu *) menuForIndex: (int) idx;

- (void) showMainWindow:  (id) sender;
- (void) linkHomepage:    (id) sender;
- (void) linkForums:      (id) sender;
- (void) notifyGrowl:	  (NSString *) file;
- (void) finderReveal:    (NSString *) path;
- (void) finderTrash:     (NSString *) path;
- (void) growlRegister:   (id) sender;

@end

#endif
