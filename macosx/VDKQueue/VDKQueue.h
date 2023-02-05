//    VDKQueue.h
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

//  DESCRIPTION
//
//      VDKQueue is an Objective-C wrapper around kernel queues (kQueues). It allows you to watch a file or folder for changes and be notified when they occur.

//  USAGE
//
//      You simply alloc/init an instance and add paths you want to watch. Your objects can be alerted to changes either by notifications or by a delegate method (or both).

//  IMPORTANT NOTE ABOUT ATOMIC OPERATIONS
//
//      There are two ways of saving a file on macOS: Atomic and Non-Atomic. In a non-atomic operation, a file is saved by directly overwriting it with new data.
//      In an Atomic save, a temporary file is first written to a different location on disk. When that completes successfully, the original file is deleted and the temporary one is renamed and moved into place where the original file existed.
//
//      This matters a great deal. If you tell VDKQueue to watch file X, then you save file X ATOMICALLY, you'll receive a notification about that event. HOWEVER, you will NOT receive any additional notifications for file X from then on. This is because the atomic operation has essentially created a new file that replaced the one you told VDKQueue to watch. (This is not an issue for non-atomic operations.)
//
//      To handle this, any time you receive a change notification from VDKQueue, you should call -removePath: followed by -addPath: on the file's path, even if the path has not changed.
//      This will ensure that if the event that triggered the notification was an atomic operation, VDKQueue will start watching the "new" file that took the place of the old one.
//
//      Some try to work around this issue by immediately attempting to re-open the file descriptor to the path. This is not bulletproof and may fail; it all depends on the timing of disk I/O.
//      Bottom line: you could not rely on it and might miss future changes to the file path you're supposedly watching. That's why VDKQueue does not take this approach, but favors the "manual" method of "stop-watching-then-rewatch".

// LIMITATIONS of VDKQueue
//
// - You have to manually call -removePath: followed by -addPath: each time you receive a change notification.
// - Callbacks are only on the main thread.
// - Unmaintained as a standalone project.

#warning Adopt an alternative to VDKQueue (UKFSEventsWatcher, EonilFSEvents, FileWatcher, DTFolderMonitor or SFSMonitor)
// ALTERNATIVES (from archaic to modern)
//
//  - FreeBSD 4.1: Kernel Queue API (kevent and kqueue)
//  (https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html)
//
//  Example: SKQueue (https://github.com/daniel-pedersen/SKQueue) but claimed to crash and be superseded by SFSMonitor (https://stackoverflow.com/a/62167224)
//
//  - macOS 10.1–10.8: FNSubscribe and FNNotify API
//  (https://developer.apple.com/documentation/coreservices/1566843-fnsubscribebypath)
//  "the FNNotify API has been supplanted by the FSEvents API"
//  (https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.7.sdk/System/Library/Frameworks/AppKit.framework/Versions/C/Headers/NSWorkspace.h)
//
//  - macOS 10.5+: File System Events API (FSEventStreamCreate)
//  (https://developer.apple.com/documentation/coreservices/file_system_events)
//  "File system events are intended to provide notification of changes with directory-level granularity. For most purposes, this is sufficient. In some cases, however, you may need to receive notifications with finer granularity. For example, you might need to monitor only changes made to a single file. For that purpose, the kernel queue (kqueue) notification system is more appropriate.
//  If you are monitoring a large hierarchy of content, you should use file system events instead, however, because kernel queues are somewhat more complex than kernel events, and can be more resource intensive because of the additional user-kernel communication involved."
// (https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/KernelQueues/KernelQueues.html)
//
//  Example: UKFSEventsWatcher (https://github.com/uliwitness/UKFileWatcher)
//  Example: EonilFSEvents (https://github.com/eonil/FSEvents)
//  Example: FileWatcher (https://github.com/eonist/FileWatcher)
//
//  - macOS 10.6+: Grand Central Dispatch API to monitor virtual filesystem nodes (DISPATCH_SOURCE_TYPE_VNODE)
//  (https://developer.apple.com/documentation/dispatch/dispatch_source_type_vnode)
//  "GCD uses kqueue under the hood and the same capabilities are made available."
//  (https://www.reddit.com/r/programming/comments/l6j3g/using_kqueue_in_cocoa/c2q74yy)
//
//  Example: RSTDirectoryMonitor (https://github.com/varuzhnikov/HelloWorld) but unmaintained as a standalone project (abandoned 2013)
//  Example: DirectoryMonitor (https://github.com/robovm/apple-ios-samples/blob/master/ListerforwatchOSiOSandOSX/Swift/ListerKit/DirectoryMonitor.swift) but unmaintained (abandoned 2016)
//  Example: TABFileMonitor (https://github.com/tblank555/iMonitorMyFiles/tree/master/iMonitorMyFiles/Classes) but unmaintained (abandoned 2016)
//  Example: DTFolderMonitor (https://github.com/Cocoanetics/DTFoundation/tree/develop/Core/Source)
//
//  - macOS 10.7+: NSFilePresenter API
//  (https://developer.apple.com/documentation/foundation/nsfilepresenter?language=objc)
//  "They're buggy, broken, and Apple is haven't willing to fix them for last 4 years."
//  (https://stackoverflow.com/a/26878163)
//
//  - macOS 10.10+: DispatchSource API (makeFileSystemObjectSource)
//  (https://developer.apple.com/documentation/dispatch/dispatchsource/2300040-makefilesystemobjectsource)
//
//  Example: SFSMonitor (https://github.com/ClassicalDude/SFSMonitor)

#import <Foundation/Foundation.h>

#include <sys/types.h>
#include <sys/event.h>

@class VDKQueue;

//  Logical OR these values into the u_int that you pass in the -addPath:notifyingAbout: method
//  to specify the types of notifications you're interested in. Pass the default value to receive all of them.
typedef NS_OPTIONS(u_int, VDKQueueNotify) {
    VDKQueueNotifyAboutRename = NOTE_RENAME, ///< Item was renamed.
    VDKQueueNotifyAboutWrite = NOTE_WRITE, ///< Item contents changed (also folder contents changed).
    VDKQueueNotifyAboutDelete = NOTE_DELETE, ///< Item was removed.
    VDKQueueNotifyAboutAttributeChange = NOTE_ATTRIB, ///< Item attributes changed.
    VDKQueueNotifyAboutSizeIncrease = NOTE_EXTEND, ///< Item size increased.
    VDKQueueNotifyAboutLinkCountChanged = NOTE_LINK, ///< Item's link count changed.
    VDKQueueNotifyAboutAccessRevocation = NOTE_REVOKE, ///< Access to item was revoked.
    VDKQueueNotifyDefault = VDKQueueNotifyAboutRename | VDKQueueNotifyAboutWrite | VDKQueueNotifyAboutDelete |
        VDKQueueNotifyAboutAttributeChange | VDKQueueNotifyAboutSizeIncrease | VDKQueueNotifyAboutLinkCountChanged | VDKQueueNotifyAboutAccessRevocation
};

//  Notifications that this class sends to the default notification center.
//      Object          =   the instance of VDKQueue that was watching for changes
//      userInfo.path   =   the file path where the change was observed

extern NSString const* VDKQueueRenameNotification;
extern NSString const* VDKQueueWriteNotification;
extern NSString const* VDKQueueDeleteNotification;
extern NSString const* VDKQueueAttributeChangeNotification;
extern NSString const* VDKQueueSizeIncreaseNotification;
extern NSString const* VDKQueueLinkCountChangeNotification;
extern NSString const* VDKQueueAccessRevocationNotification;

//  You can specify a delegate and implement this protocol's method to respond to kQueue events, instead of subscribing to notifications.
@protocol VDKQueueDelegate<NSObject>

@required
- (void)VDKQueue:(VDKQueue*)queue receivedNotification:(NSString*)noteName forPath:(NSString*)fpath;

@end

@interface VDKQueue : NSObject

//  Note: there is no need to ask whether a path is already being watched.
//        Just add it or remove it and this class will take action only if appropriate.
//        (Add only if we're not already watching it, remove only if we are.)
//
//  Warning: You must pass full, root-relative paths. Do not pass tilde-abbreviated paths or file URLs.
- (void)addPath:(NSString*)aPath;
- (void)addPath:(NSString*)aPath notifyingAbout:(u_int)flags; // See note above for values to pass in "flags"

- (void)removePath:(NSString*)aPath;
- (void)removeAllPaths;

@property(nonatomic, weak) id<VDKQueueDelegate> delegate;

/// By default, notifications are posted only if there is no delegate set. Set this value to YES to have notes posted even when there is a delegate.
@property(nonatomic, assign) BOOL alwaysPostNotifications;

@end
