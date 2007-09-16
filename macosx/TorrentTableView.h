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

#import <Cocoa/Cocoa.h>
#import <transmission.h>
#import <Controller.h>

//button layout (from end of bar) is: button, padding, button, padding
#define BUTTON_WIDTH 14.0
#define BUTTONS_TOTAL_WIDTH 36.0

#define ACTION_BUTTON_HEIGHT 12.0
#define ACTION_BUTTON_WIDTH 28.0

@interface TorrentTableView : NSTableView
{
    IBOutlet Controller * fController;
    NSArray * fTorrents;
    
    NSPoint fClickPoint;
    BOOL fClickIn;
    
    NSUserDefaults * fDefaults;
    
    IBOutlet NSMenu * fContextRow, * fContextNoRow;
    NSImage * fResumeOnIcon, * fResumeOffIcon, * fPauseOnIcon, * fPauseOffIcon,
            * fResumeNoWaitOnIcon, * fResumeNoWaitOffIcon, * fRevealOnIcon, * fRevealOffIcon,
            * fActionOnIcon, * fActionOffIcon;
    
    NSMutableArray * fKeyStrokes;
    
    IBOutlet NSMenu * fActionMenu, * fUploadMenu, * fDownloadMenu, * fRatioMenu;
    Torrent * fMenuTorrent;
}

- (void) setTorrents: (NSArray *) torrents;

- (void) displayTorrentMenuForEvent: (NSEvent *) event;

- (void) setQuickLimitMode: (id) sender;
- (void) setQuickLimit: (id) sender;

- (void) setQuickRatioMode: (id) sender;
- (void) setQuickRatio: (id) sender;

- (void) checkFile: (id) sender;

@end
