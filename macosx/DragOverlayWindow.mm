// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "DragOverlayWindow.h"
#import "DragOverlayView.h"
#import "NSStringAdditions.h"

#include <libtransmission/torrent-metainfo.h>

@interface DragOverlayWindow ()

@property(nonatomic, readonly) NSViewAnimation* fFadeInAnimation;
@property(nonatomic, readonly) NSViewAnimation* fFadeOutAnimation;

@end

@implementation DragOverlayWindow

- (instancetype)initForWindow:(NSWindow*)window
{
    if ((self = ([super initWithContentRect:window.frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered
                                      defer:NO])))
    {
        self.backgroundColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.5];
        self.alphaValue = 0.0;
        self.opaque = NO;
        self.hasShadow = NO;

        DragOverlayView* view = [[DragOverlayView alloc] initWithFrame:self.frame];
        self.contentView = view;

        self.releasedWhenClosed = NO;
        self.ignoresMouseEvents = YES;

        _fFadeInAnimation = [[NSViewAnimation alloc] initWithViewAnimations:@[
            @{ NSViewAnimationTargetKey : self, NSViewAnimationEffectKey : NSViewAnimationFadeInEffect }
        ]];
        _fFadeInAnimation.duration = 0.15;
        _fFadeInAnimation.animationBlockingMode = NSAnimationNonblockingThreaded;

        _fFadeOutAnimation = [[NSViewAnimation alloc] initWithViewAnimations:@[
            @{ NSViewAnimationTargetKey : self, NSViewAnimationEffectKey : NSViewAnimationFadeOutEffect }
        ]];
        _fFadeOutAnimation.duration = 0.5;
        _fFadeOutAnimation.animationBlockingMode = NSAnimationNonblockingThreaded;

        [window addChildWindow:self ordered:NSWindowAbove];
    }
    return self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)setTorrents:(NSArray<NSString*>*)files
{
    uint64_t size = 0;
    NSUInteger count = 0;

    NSString* name;
    NSUInteger fileCount = 0;

    for (NSString* file in files)
    {
        if ([[NSWorkspace.sharedWorkspace typeOfFile:file error:NULL] isEqualToString:@"org.bittorrent.torrent"] ||
            [file.pathExtension caseInsensitiveCompare:@"torrent"] == NSOrderedSame)
        {
            auto metainfo = tr_torrent_metainfo{};
            if (metainfo.parseTorrentFile(file.UTF8String))
            {
                ++count;

                size += metainfo.totalSize();

                auto const n_files = metainfo.fileCount();
                fileCount += n_files;
                // only useful when one torrent
                if (count == 1)
                {
                    if (n_files == 1)
                    {
                        name = [NSString convertedStringFromCString:metainfo.fileSubpath(0).c_str()];
                    }
                    else
                    {
                        name = @(metainfo.name().c_str());
                    }
                }
            }
        }
    }

    if (count <= 0)
    {
        return;
    }

    //set strings and icon
    NSString* secondString = [NSString stringForFileSize:size];
    if (count > 1)
    {
        NSString* fileString;
        if (fileCount == 1)
        {
            fileString = NSLocalizedString(@"1 file", "Drag overlay -> torrents");
        }
        else
        {
            fileString = [NSString localizedStringWithFormat:NSLocalizedString(@"%lu files", "Drag overlay -> torrents"), fileCount];
        }
        secondString = [NSString stringWithFormat:@"%@, %@", fileString, secondString];
    }

    NSImage* icon;
    if (count == 1)
    {
        icon = [NSWorkspace.sharedWorkspace
            iconForFileType:fileCount <= 1 ? name.pathExtension : NSFileTypeForHFSTypeCode(kGenericFolderIcon)];
    }
    else
    {
        name = [NSString localizedStringWithFormat:NSLocalizedString(@"%lu Torrent Files", "Drag overlay -> torrents"), count];
        secondString = [secondString stringByAppendingString:@" total"];
        icon = [NSImage imageNamed:@"TransmissionDocument.icns"];
    }

    [self.contentView setOverlay:icon mainLine:name subLine:secondString];
    [self fadeIn];
}

- (void)setFile:(NSString*)file
{
    [self.contentView setOverlay:[NSImage imageNamed:@"CreateLarge"]
                        mainLine:NSLocalizedString(@"Create a Torrent File", "Drag overlay -> file")
                         subLine:file];
    [self fadeIn];
}

- (void)setURL:(NSString*)url
{
    [self.contentView setOverlay:[NSImage imageNamed:@"Globe"] mainLine:NSLocalizedString(@"Web Address", "Drag overlay -> url")
                         subLine:url];
    [self fadeIn];
}

- (void)fadeIn
{
    //stop other animation and set to same progress
    if (self.fFadeOutAnimation.animating)
    {
        [self.fFadeOutAnimation stopAnimation];
        self.fFadeInAnimation.currentProgress = 1.0 - self.fFadeOutAnimation.currentProgress;
    }
    [self.fFadeInAnimation startAnimation];
}

- (void)fadeOut
{
    //stop other animation and set to same progress
    if (self.fFadeInAnimation.animating)
    {
        [self.fFadeInAnimation stopAnimation];
        self.fFadeOutAnimation.currentProgress = 1.0 - self.fFadeInAnimation.currentProgress;
    }
    if (self.alphaValue > 0.0)
    {
        [self.fFadeOutAnimation startAnimation];
    }
}

@end
