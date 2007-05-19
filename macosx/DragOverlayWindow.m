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

#import "DragOverlayWindow.h"
#import "DragOverlayView.h"
#import "StringAdditions.h"

@interface DragOverlayWindow (Private)

- (void) beginFadeIn;
- (void) fadeIn;
- (void) fadeOut;

@end

@implementation DragOverlayWindow

- (id) initWithLib: (tr_handle_t *) lib
{
    if (self = ([super initWithContentRect: NSMakeRect(0, 0, 1.0, 1.0) styleMask: NSBorderlessWindowMask
                    backing: NSBackingStoreBuffered defer: NO]))
    {
        fLib = lib;
        
        [self setBackgroundColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.5]];
        [self setOpaque: NO];
        [self setHasShadow: NO];
        
        DragOverlayView * view = [[DragOverlayView alloc] initWithFrame: [self frame]];
        [self setContentView: view];
        [view release];
        
        [self setReleasedWhenClosed: NO];
        [self setIgnoresMouseEvents: YES];
    }
    return self;
}

- (void) dealloc
{
    if (fFadeInTimer)
        [fFadeOutTimer invalidate];
    if (fFadeOutTimer)
        [fFadeOutTimer invalidate];
    
    [super dealloc];
}

- (void) setFiles: (NSArray *) files
{
    uint64_t size = 0;
    int count = 0;
    
    NSString * name;
    BOOL folder;
    
    NSString * file;
    NSEnumerator * enumerator = [files objectEnumerator];
    tr_torrent_t * tempTor;
    tr_info_t * info;
    while ((file = [enumerator nextObject]))
    {
        int error;
        if ((tempTor = tr_torrentInit(fLib, [file UTF8String], NULL, 0, &error)))
        {
            info = tr_torrentInfo(tempTor);
            
            count++;
            size += info->totalSize;
            
            //only useful when one torrent
            if (count == 1)
            {
                name = [NSString stringWithUTF8String: info->name];
                folder = info->multifile;
            }
            
            tr_torrentClose(fLib, tempTor);
        }
    }
    
    NSImage * icon = nil;
    NSString * sizeString = [NSString stringForFileSize: size];
    if (count == 1)
        icon = [[NSWorkspace sharedWorkspace] iconForFileType: folder ? NSFileTypeForHFSTypeCode('fldr') : [name pathExtension]];
    else
    {
        name = [NSString stringWithFormat: NSLocalizedString(@"%d Torrent Files", "Drag overlay -> multiple drag files"), count];
        sizeString = [sizeString stringByAppendingString: @" Total"];
    }
    
    [[self contentView] setOverlay: icon mainLine: name subLine: sizeString];
    
    [self beginFadeIn];
}

- (void) setURL: (NSString *) url
{
    [[self contentView] setOverlay: [NSImage imageNamed: @"Globe.tiff"]
        mainLine: NSLocalizedString(@"Web Address", "Drag overlay -> url") subLine: url];
    
    [self beginFadeIn];
}

- (void) closeFadeOut
{
    if (fFadeInTimer)
    {
        [fFadeInTimer invalidate];
        fFadeInTimer = nil;
    }
    if (fFadeOutTimer)
    {
        [fFadeOutTimer invalidate];
        fFadeOutTimer = nil;
    }
    
    fFadeOutTimer = [NSTimer scheduledTimerWithTimeInterval: 0.015 target: self 
            selector: @selector(fadeOut) userInfo: nil repeats: YES];
}

@end

@implementation DragOverlayWindow (Private)

- (void) beginFadeIn
{
    [self setAlphaValue: 0.0];
    if (fFadeInTimer)
    {
        [fFadeInTimer invalidate];
        fFadeInTimer = nil;
    }
    if (fFadeOutTimer)
    {
        [fFadeOutTimer invalidate];
        fFadeOutTimer = nil;
    }
    
    fFadeInTimer = [NSTimer scheduledTimerWithTimeInterval: 0.0075 target: self 
            selector: @selector(fadeIn) userInfo: nil repeats: YES];
}

- (void) fadeIn
{
    [self setAlphaValue: [self alphaValue] + 0.1];
    if ([self alphaValue] >= 1.0)
    {
        [fFadeInTimer invalidate];
        fFadeInTimer = nil;
    }
}

- (void) fadeOut
{
    [self setAlphaValue: [self alphaValue] - 0.1];
    if ([self alphaValue] <= 0.0)
    {
        [fFadeOutTimer invalidate];
        fFadeOutTimer = nil;
        
        [self close];
    }
}

@end
