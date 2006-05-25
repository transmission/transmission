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

#import <Cocoa/Cocoa.h>
#import <transmission.h>

@interface Torrent : NSObject
{
    tr_handle_t  * fLib;
    tr_torrent_t * fHandle;
    tr_info_t    * fInfo;
    tr_stat_t    * fStat;
    BOOL         fResumeOnWake;
    NSDate       * fDate;

    NSUserDefaults  * fDefaults;

    NSImage         * fIcon;
    NSImage         * fIconNonFlipped;
    NSMutableString * fStatusString;
    NSMutableString * fInfoString;
    NSMutableString * fDownloadString;
    NSMutableString * fUploadString;
    
    int     fStopRatioSetting;
    float   fRatioLimit;
}

- (id)          initWithPath: (NSString *) path lib: (tr_handle_t *) lib;
- (id)          initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib;
- (NSDictionary *) history;
                    
- (void)       setFolder: (NSString *) path;
- (NSString *) getFolder;
- (void)       getAvailability: (int8_t *) tab size: (int) size;

- (void)       update;
- (void)       start;
- (void)       stop;
- (void)       sleep;
- (void)       wakeUp;

- (float)      ratio;
- (int)        stopRatioSetting;
- (void)       setStopRatioSetting: (int) setting;
- (float)      ratioLimit;
- (void)       setRatioLimit: (float) limit;

- (NSNumber *) stateSortKey;

- (void)       reveal;
- (void)       trashTorrent;
- (void)       trashData;

- (NSImage *)  icon;
- (NSImage *)  iconNonFlipped;
- (NSString *) path;
- (NSString *) name;
- (uint64_t)   size;
- (NSString *) tracker;
- (NSString *) announce;
- (int)        pieceSize;
- (int)        pieceCount;
- (NSString *) hash;

- (float)      progress;
- (BOOL)       isActive;
- (BOOL)       isSeeding;
- (BOOL)       isPaused;
- (BOOL)       justFinished;
- (NSString *) statusString;
- (NSString *) infoString;
- (NSString *) downloadString;
- (NSString *) uploadString;
- (int)        seeders;
- (int)        leechers;
- (uint64_t)   downloaded;
- (uint64_t)   uploaded;
- (NSDate *)   date;

@end
