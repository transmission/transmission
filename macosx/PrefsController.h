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
#import <Sparkle/Sparkle.h>
#import "PortChecker.h"

@interface PrefsController : NSWindowController
{
    tr_handle * fHandle;
    
    NSUserDefaults * fDefaults;
    BOOL fHasLoaded;
    
    IBOutlet NSView * fGeneralView, * fTransfersView, * fBandwidthView, * fPeersView, * fNetworkView, * fRemoteView;
    
    NSString * fInitialString;
    
    IBOutlet NSPopUpButton * fFolderPopUp, * fIncompleteFolderPopUp, * fImportFolderPopUp;
    IBOutlet NSTextField * fRatioStopField, * fQueueDownloadField, * fQueueSeedField, * fStalledField;
    
    SUUpdater * fUpdater;

    IBOutlet NSTextField * fUploadField, * fDownloadField,
                        * fSpeedLimitUploadField, * fSpeedLimitDownloadField;
    
    IBOutlet NSTextField * fPeersGlobalField, * fPeersTorrentField, * fBlocklistMessageField;
    IBOutlet NSButton * fBlocklistEnableCheck;
    
    PortChecker * fPortChecker;
    IBOutlet NSTextField * fPortField, * fPortStatusField;
    IBOutlet NSButton * fNatCheck;
    IBOutlet NSImageView * fPortStatusImage;
    IBOutlet NSProgressIndicator * fPortStatusProgress;
    NSTimer * fPortStatusTimer;
    int fPeerPort, fNatStatus;
    
    IBOutlet NSTextField * fProxyAddressField, * fProxyPortField, * fProxyPasswordField;
    IBOutlet NSPopUpButton * fProxyTypePopUp;
    
    IBOutlet NSTextField * fRPCPortField, * fRPCPasswordField;
    IBOutlet NSTableView * fRPCAccessTable;
    IBOutlet NSSegmentedControl * fRPCAddRemoveControl;
    NSMutableArray * fRPCAccessArray;
}

- (id) initWithHandle: (tr_handle *) handle;
- (tr_handle *) handle;
- (void) setUpdater: (SUUpdater *) updater;

- (void) updatePortField;
- (void) setPort: (id) sender;
- (void) setNat: (id) sender;
- (void) updatePortStatus;
- (void) portCheckerDidFinishProbing: (PortChecker *) portChecker;

- (NSArray *) sounds;
- (void) setSound: (id) sender;

- (void) setPeersGlobal: (id) sender;
- (void) setPeersTorrent: (id) sender;

- (void) setPEX: (id) sender;

- (void) setEncryptionMode: (id) sender;

- (void) setBlocklistEnabled: (id) sender;
- (void) updateBlocklist: (id) sender;
- (void) updateBlocklistFields;

- (void) setBadge: (id) sender;
- (void) resetWarnings: (id) sender;
- (void) setCheckForUpdate: (id) sender;

- (void) setQueue: (id) sender;
- (void) setQueueNumber: (id) sender;

- (void) setStalled: (id) sender;
- (void) setStalledMinutes: (id) sender;

- (void) setDownloadLocation: (id) sender;
- (void) folderSheetShow: (id) sender;
- (void) incompleteFolderSheetShow: (id) sender;

- (void) applyRatioSetting: (id) sender;
- (void) updateRatioStopField;
- (void) setRatioStop: (id) sender;

- (void) applySpeedSettings: (id) sender;

- (void) updateLimitFields;
- (void) setGlobalLimit: (id) sender;

- (void) setSpeedLimit: (id) sender;
- (void) setAutoSpeedLimit: (id) sender;

- (void) setAutoImport: (id) sender;
- (void) importFolderSheetShow: (id) sender;

- (void) setAutoSize: (id) sender;

- (void) setProxyEnabled: (id) sender;
- (void) setProxyAddress: (id) sender;
- (void) setProxyPort: (id) sender;
- (void) setProxyType: (id) sender;
- (void) updateProxyType;
- (void) setProxyAuthorize: (id) sender;
- (void) setProxyUsername: (id) sender;
- (void) setProxyPassword: (id) sender;
- (void) updateProxyPassword;

- (void) setRPCEnabled: (id) sender;
- (void) linkWebUI: (id) sender;
- (void) setRPCAuthorize: (id) sender;
- (void) setRPCUsername: (id) sender;
- (void) setRPCPassword: (id) sender;
- (void) updateRPCPassword;
- (void) setRPCPort: (id) sender;
- (void) updateRPCAccessList;
- (void) addRemoveRPCIP: (id) sender;

- (void) helpForPeers: (id) sender;
- (void) helpForNetwork: (id) sender;
- (void) helpForRemote: (id) sender;

- (void) rpcUpdatePrefs;

@end
