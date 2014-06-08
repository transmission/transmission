/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2012 Transmission authors and contributors
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
#import "NSStringAdditions.h"

@interface DragOverlayWindow (Private)

- (void) resizeWindow;

@end

@implementation DragOverlayWindow

- (id) initWithLib: (tr_session *) lib forWindow: (NSWindow *) window
{
    if ((self = ([super initWithContentRect: [window frame] styleMask: NSBorderlessWindowMask
                    backing: NSBackingStoreBuffered defer: NO])))
    {
        fLib = lib;
        
        [self setBackgroundColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.5]];
        [self setAlphaValue: 0.0];
        [self setOpaque: NO];
        [self setHasShadow: NO];
        
        DragOverlayView * view = [[DragOverlayView alloc] initWithFrame: [self frame]];
        [self setContentView: view];
        [view release];
        
        [self setReleasedWhenClosed: NO];
        [self setIgnoresMouseEvents: YES];
        
        fFadeInAnimation = [[NSViewAnimation alloc] initWithViewAnimations: [NSArray arrayWithObject:
                                [NSDictionary dictionaryWithObjectsAndKeys: self, NSViewAnimationTargetKey,
                                NSViewAnimationFadeInEffect, NSViewAnimationEffectKey, nil]]];
        [fFadeInAnimation setDuration: 0.15];
        [fFadeInAnimation setAnimationBlockingMode: NSAnimationNonblockingThreaded];
        
        fFadeOutAnimation = [[NSViewAnimation alloc] initWithViewAnimations: [NSArray arrayWithObject:
                                [NSDictionary dictionaryWithObjectsAndKeys: self, NSViewAnimationTargetKey,
                                NSViewAnimationFadeOutEffect, NSViewAnimationEffectKey, nil]]];
        [fFadeOutAnimation setDuration: 0.5];
        [fFadeOutAnimation setAnimationBlockingMode: NSAnimationNonblockingThreaded];
        
        [window addChildWindow: self ordered: NSWindowAbove];
        
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(resizeWindow)
            name: NSWindowDidResizeNotification object: window];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fFadeInAnimation release];
    [fFadeOutAnimation release];
    
    [super dealloc];
}

- (void) setTorrents: (NSArray *) files
{
    uint64_t size = 0;
    NSInteger count = 0;
    
    NSString * name;
    BOOL folder;
    NSInteger fileCount = 0;
    
    for (NSString * file in files)
    {
        if ([[[NSWorkspace sharedWorkspace] typeOfFile: file error: NULL] isEqualToString: @"org.bittorrent.torrent"]
            || [[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
        {
            tr_ctor * ctor = tr_ctorNew(fLib);
            tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
            tr_info info;
            if (tr_torrentParse(ctor, &info) == TR_PARSE_OK)
            {
                count++;
                size += info.totalSize;
                fileCount += info.fileCount;
                
                //only useful when one torrent
                if (count == 1)
                {
                    name = [NSString stringWithUTF8String: info.name];
                    folder = info.isFolder;
                }
            }
            tr_metainfoFree(&info);
            tr_ctorFree(ctor);
        }
    }
    
    if (count <= 0)
        return;
    
    //set strings and icon
    NSString * secondString = [NSString stringForFileSize: size];
    if (count > 1 || folder)
    {
        NSString * fileString;
        if (fileCount == 1)
            fileString = NSLocalizedString(@"1 file", "Drag overlay -> torrents");
        else
            fileString= [NSString stringWithFormat: NSLocalizedString(@"%@ files", "Drag overlay -> torrents"),
                            [NSString formattedUInteger: fileCount]];
        secondString = [NSString stringWithFormat: @"%@, %@", fileString, secondString];
    }
    
    NSImage * icon;
    if (count == 1)
        icon = [[NSWorkspace sharedWorkspace] iconForFileType: folder ? NSFileTypeForHFSTypeCode(kGenericFolderIcon) : [name pathExtension]];
    else
    {
        name = [NSString stringWithFormat: NSLocalizedString(@"%@ Torrent Files", "Drag overlay -> torrents"),
                [NSString formattedUInteger: count]];
        secondString = [secondString stringByAppendingString: @" total"];
        icon = [NSImage imageNamed: @"TransmissionDocument.icns"];
    }
    
    [[self contentView] setOverlay: icon mainLine: name subLine: secondString];
    [self fadeIn];
}

- (void) setFile: (NSString *) file
{
    [[self contentView] setOverlay: [NSImage imageNamed: @"CreateLarge"]
        mainLine: NSLocalizedString(@"Create a Torrent File", "Drag overlay -> file") subLine: file];
    [self fadeIn];
}

- (void) setURL: (NSString *) url
{
    [[self contentView] setOverlay: [NSImage imageNamed: @"Globe"]
        mainLine: NSLocalizedString(@"Web Address", "Drag overlay -> url") subLine: url];
    [self fadeIn];
}

- (void) fadeIn
{
    //stop other animation and set to same progress
    if ([fFadeOutAnimation isAnimating])
    {
        [fFadeOutAnimation stopAnimation];
        [fFadeInAnimation setCurrentProgress: 1.0 - [fFadeOutAnimation currentProgress]];
    }
    [fFadeInAnimation startAnimation];
}

- (void) fadeOut
{
    //stop other animation and set to same progress
    if ([fFadeInAnimation isAnimating])
    {
        [fFadeInAnimation stopAnimation];
        [fFadeOutAnimation setCurrentProgress: 1.0 - [fFadeInAnimation currentProgress]];
    }
    if ([self alphaValue] > 0.0)
        [fFadeOutAnimation startAnimation];
}

@end

@implementation DragOverlayWindow (Private)

- (void) resizeWindow
{
    [self setFrame: [[self parentWindow] frame] display: NO];
}

@end
