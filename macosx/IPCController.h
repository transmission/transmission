/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#include "ipcparse.h"

@interface NSObject (IPCControllerDelegate)

- (void)                    ipcQuit;
- (NSArray *)    ipcGetTorrentsByID: (NSArray *) idlist;
- (NSArray *)  ipcGetTorrentsByHash: (NSArray *) hashlist;
- (BOOL)             ipcAddTorrents: (NSArray *) torrents;
- (BOOL)          ipcAddTorrentFile: (NSString *) path
                          directory: (NSString *) dir;
- (BOOL) ipcAddTorrentFileAutostart: (NSString *) path
                          directory: (NSString *) dir
                          autostart: (BOOL) autostart;
- (BOOL)          ipcAddTorrentData: (NSData *) data
                          directory: (NSString *) dir;
- (BOOL) ipcAddTorrentDataAutostart: (NSData *) data
                          directory: (NSString *) dir
                          autostart: (BOOL) autostart;
- (BOOL)           ipcStartTorrents: (NSArray *) torrents;
- (BOOL)            ipcStopTorrents: (NSArray *) torrents;
- (BOOL)          ipcRemoveTorrents: (NSArray *) torrents;
/* XXX how to get and set prefs nicely? */

@end

@interface IPCController : NSObject
{
    NSSocketPort     * _sock;
    NSFileHandle     * _listen;
    struct ipc_funcs * _funcs;
    NSMutableArray   * _clients;
    id                 _delegate;
}

- (id)          init;
- (id)      delegate;
- (void) setDelegate: (id) newdelegate;

@end
