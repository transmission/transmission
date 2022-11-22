//    VDKQueue.mm
//  Copyright © 2017-2022 Transmission authors and contributors.
//
//  Based on VDKQueue (https://github.com/bdkjones/VDKQueue) which was created and copyrighted by Bryan D K Jones on 28 March 2012.
//  Based on UKKQueue (https://github.com/uliwitness/UKFileWatcher) which was created and copyrighted by Uli Kusterer on 21 Dec 2003.
//
//    This software is provided 'as-is', without any express or implied
//    warranty. In no event will the authors be held liable for any damages
//    arising from the use of this software.
//    Permission is granted to anyone to use this software for any purpose,
//    including commercial applications, and to alter it and redistribute it
//    freely, subject to the following restrictions:
//       1. The origin of this software must not be misrepresented; you must not
//       claim that you wrote the original software. If you use this software
//       in a product, an acknowledgment in the product documentation would be
//       appreciated but is not required.
//       2. Altered source versions must be plainly marked as such, and must not be
//       misrepresented as being the original software.
//       3. This notice may not be removed or altered from any source
//       distribution.

#import "VDKQueue.h"

#import <unistd.h>
#import <fcntl.h>
#include <sys/stat.h>

NSString const* VDKQueueRenameNotification = @"VDKQueueFileRenamedNotification";
NSString const* VDKQueueWriteNotification = @"VDKQueueFileWrittenToNotification";
NSString const* VDKQueueDeleteNotification = @"VDKQueueFileDeletedNotification";
NSString const* VDKQueueAttributeChangeNotification = @"VDKQueueFileAttributesChangedNotification";
NSString const* VDKQueueSizeIncreaseNotification = @"VDKQueueFileSizeIncreasedNotification";
NSString const* VDKQueueLinkCountChangeNotification = @"VDKQueueLinkCountChangedNotification";
NSString const* VDKQueueAccessRevocationNotification = @"VDKQueueAccessWasRevokedNotification";

#pragma mark -
#pragma mark VDKQueuePathEntry

/// This is a simple model class used to hold info about each path we watch.
@interface VDKQueuePathEntry : NSObject

@property(atomic, copy) NSString* path;
@property(atomic, assign) int watchedFD;
@property(atomic, assign) u_int subscriptionFlags;

@end

@implementation VDKQueuePathEntry

- (nullable instancetype)initWithPath:(NSString*)inPath andSubscriptionFlags:(u_int)flags
{
    self = [super init];
    if (self)
    {
        _path = [inPath copy];
        _watchedFD = open(_path.fileSystemRepresentation, O_EVTONLY, 0);
        if (_watchedFD < 0)
        {
            return nil;
        }
        _subscriptionFlags = flags;
    }
    return self;
}

- (void)dealloc
{
    if (_watchedFD >= 0)
    {
        close(_watchedFD);
    }
    _watchedFD = -1;
}

@end

#pragma mark -
#pragma mark VDKQueue

@interface VDKQueue ()
{
  @private
    /// The actual kqueue ID (Unix file descriptor).
    int _coreQueueFD;
    /// List of VDKQueuePathEntries. Keys are NSStrings of the path that each VDKQueuePathEntry is for.
    NSMutableDictionary* _watchedPathEntries;
    /// Set to NO to cancel the thread that watches `_coreQueueFD` for kQueue events
    BOOL _keepWatcherThreadRunning;
}
@end

@implementation VDKQueue

#pragma mark -
#pragma mark INIT/DEALLOC

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _coreQueueFD = kqueue();
        if (_coreQueueFD == -1)
        {
            return nil;
        }
        _watchedPathEntries = [[NSMutableDictionary alloc] init];
    }
    return self;
}

- (void)dealloc
{
    // Shut down the thread that's scanning for kQueue events
    _keepWatcherThreadRunning = NO;

    // Do this to close all the open file descriptors for files we're watching
    [self removeAllPaths];

    _watchedPathEntries = nil;
}

#pragma mark -
#pragma mark PRIVATE METHODS

- (VDKQueuePathEntry*)addPathToQueue:(NSString*)path notifyingAbout:(u_int)flags
{
    @synchronized(self)
    {
        // Are we already watching this path?
        VDKQueuePathEntry* pathEntry = _watchedPathEntries[path];

        if (pathEntry)
        {
            // All flags already set?
            if ((pathEntry.subscriptionFlags & flags) == flags)
            {
                return pathEntry;
            }
            flags |= pathEntry.subscriptionFlags;
        }

        if (!pathEntry)
        {
            pathEntry = [[VDKQueuePathEntry alloc] initWithPath:path andSubscriptionFlags:flags];
        }

        if (pathEntry)
        {
            struct timespec nullts = { 0, 0 };
            struct kevent ev;

            EV_SET(&ev, pathEntry.watchedFD, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR, flags, 0, (__bridge void*)pathEntry);

            pathEntry.subscriptionFlags = flags;

            _watchedPathEntries[path] = pathEntry;
            kevent(_coreQueueFD, &ev, 1, NULL, 0, &nullts);

            // Start the thread that fetches and processes our events if it's not already running.
            if (!_keepWatcherThreadRunning)
            {
                _keepWatcherThreadRunning = YES;
                [NSThread detachNewThreadSelector:@selector(watcherThread:) toTarget:self withObject:nil];
            }
        }

        return pathEntry;
    }
}

- (void)watcherThread:(id)sender
{
    int n;
    struct kevent ev;
    // 1 second timeout. Should be longer, but we need this thread to exit when a kqueue is dealloced, so 1 second timeout is quite a while to wait.
    struct timespec timeout = { 1, 0 };
    // So we don't have to risk accessing iVars when the thread is terminated.
    int theFD = _coreQueueFD;

    NSMutableArray* notesToPost = [[NSMutableArray alloc] initWithCapacity:5];

#if DEBUG_LOG_THREAD_LIFETIME
    NSLog(@"watcherThread started.");
#endif

    while (_keepWatcherThreadRunning)
    {
        n = kevent(theFD, NULL, 0, &ev, 1, &timeout);
        if (n <= 0 || ev.filter != EVFILT_VNODE || !ev.fflags)
        {
            continue;
        }
        // The KEVENT can be sent back to us with a udata value that is NOT what we assigned to the queue.
        // A KEVENT that does not have a VDKQueuePathEntry object attached as the udata parameter is not an event we registered for.
        id pe = (__bridge id)(ev.udata);
        if (![pe isKindOfClass:VDKQueuePathEntry.class])
        {
            continue;
        }
        NSString* fpath = ((VDKQueuePathEntry*)pe).path;
        if (!fpath)
        {
            continue;
        }

        // Clear any old notifications
        [notesToPost removeAllObjects];

        // Figure out which notifications we need to issue
        if ((ev.fflags & NOTE_RENAME) == NOTE_RENAME)
        {
            [notesToPost addObject:VDKQueueRenameNotification];
        }
        if ((ev.fflags & NOTE_WRITE) == NOTE_WRITE)
        {
            [notesToPost addObject:VDKQueueWriteNotification];
        }
        if ((ev.fflags & NOTE_DELETE) == NOTE_DELETE)
        {
            [notesToPost addObject:VDKQueueDeleteNotification];
        }
        if ((ev.fflags & NOTE_ATTRIB) == NOTE_ATTRIB)
        {
            [notesToPost addObject:VDKQueueAttributeChangeNotification];
        }
        if ((ev.fflags & NOTE_EXTEND) == NOTE_EXTEND)
        {
            [notesToPost addObject:VDKQueueSizeIncreaseNotification];
        }
        if ((ev.fflags & NOTE_LINK) == NOTE_LINK)
        {
            [notesToPost addObject:VDKQueueLinkCountChangeNotification];
        }
        if ((ev.fflags & NOTE_REVOKE) == NOTE_REVOKE)
        {
            [notesToPost addObject:VDKQueueAccessRevocationNotification];
        }

        // notesToPost will be changed in the next loop iteration, which will likely occur before the block below runs.
        NSArray* notes = [[NSArray alloc] initWithArray:notesToPost];

        // Post the notifications (or call the delegate method) on the main thread.
        dispatch_async(dispatch_get_main_queue(), ^{
            for (NSString* note in notes)
            {
                [self->_delegate VDKQueue:self receivedNotification:note forPath:fpath];

                if (!self->_delegate || self->_alwaysPostNotifications)
                {
                    [NSNotificationCenter.defaultCenter postNotificationName:note object:self userInfo:@{ @"path" : fpath }];
                }
            }
        });
    }

    // Close our kqueue's file descriptor
    if (close(theFD) == -1)
    {
        NSLog(@"VDKQueue watcherThread: Couldn't close main kqueue (%d)", errno);
    }

#if DEBUG_LOG_THREAD_LIFETIME
    NSLog(@"watcherThread finished.");
#endif
}

#pragma mark -
#pragma mark PUBLIC METHODS

- (void)addPath:(NSString*)aPath
{
    [self addPath:aPath notifyingAbout:VDKQueueNotifyDefault];
}

- (void)addPath:(NSString*)aPath notifyingAbout:(u_int)flags
{
    if (!aPath)
    {
        return;
    }

    @synchronized(self)
    {
        if (_watchedPathEntries[aPath])
        {
            // Only add this path if we don't already have it.
            return;
        }
        VDKQueuePathEntry* entry = [self addPathToQueue:aPath notifyingAbout:flags];
        if (!entry)
        {
            // By default, a darwin process can only have 256 file descriptors open at once.
            // https://wilsonmar.github.io/maximum-limits/
            NSLog(
                @"VDKQueue tried to add the path %@ to watchedPathEntries, but the VDKQueuePathEntry was nil. \nIt's possible that the host process has hit its max open file descriptors limit.",
                aPath);
        }
    }
}

- (void)removePath:(NSString*)aPath
{
    if (!aPath)
    {
        return;
    }

    @synchronized(self)
    {
        VDKQueuePathEntry* entry = _watchedPathEntries[aPath];

        // Remove it only if we're watching it.
        if (entry)
        {
            [_watchedPathEntries removeObjectForKey:aPath];
        }
    }
}

- (void)removeAllPaths
{
    @synchronized(self)
    {
        [_watchedPathEntries removeAllObjects];
    }
}

@end
