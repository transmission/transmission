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

#import <Cocoa/Cocoa.h>
#import <transmission.h>
#import <Sparkle/Sparkle.h>

@interface PrefsController : NSWindowController
{
    tr_handle_t * fHandle;
    
    NSToolbar               * fToolbar;
    IBOutlet NSView         * fGeneralView, * fTransfersView, * fBandwidthView, * fNetworkView;
    
    IBOutlet NSPopUpButton  * fFolderPopUp, * fImportFolderPopUp;
    IBOutlet NSButton       * fQuitCheck, * fRemoveCheck,
                            * fQuitDownloadingCheck, * fRemoveDownloadingCheck,
                            * fBadgeDownloadRateCheck, * fBadgeUploadRateCheck,
                            * fPlayDownloadSoundCheck,
                            * fCopyTorrentCheck, * fDeleteOriginalTorrentCheck,
                            * fAutoImportCheck, * fAutoSizeCheck;
                            
    IBOutlet NSPopUpButton  * fUpdatePopUp;

    IBOutlet NSTextField    * fUploadField, * fDownloadField,
                            * fSpeedLimitUploadField, * fSpeedLimitDownloadField;
    IBOutlet NSButton       * fUploadCheck, * fDownloadCheck;

    IBOutlet NSTextField    * fPortField;
    
    IBOutlet NSButton       * fRatioCheck;
    IBOutlet NSTextField    * fRatioField;
    
    IBOutlet NSMatrix       * fStartMatrix;
    IBOutlet NSTextField    * fWaitToStartField;
    
    IBOutlet SUUpdater      * fUpdater;

    NSString                * fDownloadFolder, * fImportFolder;
    NSUserDefaults          * fDefaults;
}

- (id) initWithWindowNibName: (NSString *) name handle: (tr_handle_t *) handle;

- (void) setShowMessage:    (id) sender;
- (void) setBadge:          (id) sender;
- (void) setPlaySound:      (id) sender;
- (void) setUpdate:         (id) sender;
- (void) checkUpdate;

- (void) setStartSetting:   (id) sender;
- (void) setWaitToStart:    (id) sender;

- (void) setMoveTorrent:        (id) sender;
- (void) setDownloadLocation:   (id) sender;
- (void) folderSheetShow:       (id) sender;

- (void) setPort:       (id) sender;
- (void) setSpeedLimit: (id) sender;

- (void) setLimit:          (id) sender;
- (void) setLimitCheck:     (id) sender;
- (void) setLimitEnabled:   (BOOL) enable type: (NSString *) type;
- (void) setQuickLimit:     (int) limit type: (NSString *) type;

- (void) enableSpeedLimit: (BOOL) enable;

- (void) setAutoImport: (id) sender;
- (void) importFolderSheetShow: (id) sender;

- (void) setAutoSize: (id) sender;

- (void) setRatio:          (id) sender;
- (void) setRatioCheck:     (id) sender;
- (void) setRatioEnabled:   (BOOL) enable;
- (void) setQuickRatio: (float) ratioLimit;

@end
