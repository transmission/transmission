//	VDKQueue.h
//	Created by Bryan D K Jones on 28 March 2012
//	Copyright 2013 Bryan D K Jones
//
//  Based heavily on UKKQueue, which was created and copyrighted by Uli Kusterer on 21 Dec 2003.
//
//	This software is provided 'as-is', without any express or implied
//	warranty. In no event will the authors be held liable for any damages
//	arising from the use of this software.
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//	   1. The origin of this software must not be misrepresented; you must not
//	   claim that you wrote the original software. If you use this software
//	   in a product, an acknowledgment in the product documentation would be
//	   appreciated but is not required.
//	   2. Altered source versions must be plainly marked as such, and must not be
//	   misrepresented as being the original software.
//	   3. This notice may not be removed or altered from any source
//	   distribution.

//
//  BASED ON UKKQUEUE:
//
//      This is an updated, modernized and streamlined version of the excellent UKKQueue class, which was authored by Uli Kusterer.
//      UKKQueue was written back in 2003 and there have been many, many improvements to Objective-C since then. VDKQueue uses the 
//      core of Uli's original class, but makes it faster and more efficient. Method calls are reduced. Grand Central Dispatch is used in place
//      of Uli's "threadProxy" objects. The memory footprint is roughly halved, as I don't create the overhead that UKKQueue does.
//
//      VDKQueue is also simplified. The option to use it as a singleton is removed. You simply alloc/init an instance and add paths you want to
//      watch. Your objects can be alerted to changes either by notifications or by a delegate method (or both). See below. 
//
//      It also fixes several bugs. For one, it won't crash if it can't create a file descriptor to a file you ask it to watch. (By default, an OS X process can only
//      have about 3,000 file descriptors open at once. If you hit that limit, UKKQueue will crash. VDKQueue will not.)
//

//
//  DEPENDENCIES: 
//      
//      VDKQueue requires OS 10.6+ because it relies on Grand Central Dispatch.
//

//
//  IMPORTANT NOTE ABOUT ATOMIC OPERATIONS
//
//      There are two ways of saving a file on OS X: Atomic and Non-Atomic. In a non-atomic operation, a file is saved by directly overwriting it with new data.
//      In an Atomic save, a temporary file is first written to a different location on disk. When that completes successfully, the original file is deleted and the
//      temporary one is renamed and moved into place where the original file existed.
//
//      This matters a great deal. If you tell VDKQueue to watch file X, then you save file X ATOMICALLY, you'll receive a notification about that event. HOWEVER, you will
//      NOT receive any additional notifications for file X from then on. This is because the atomic operation has essentially created a new file that replaced the one you
//      told VDKQueue to watch. (This is not an issue for non-atomic operations.)
//
//      To handle this, any time you receive a change notification from VDKQueue, you should call -removePath: followed by -addPath: on the file's path, even if the path
//      has not changed. This will ensure that if the event that triggered the notification was an atomic operation, VDKQueue will start watching the "new" file that took
//      the place of the old one.
//
//      Other frameworks out there try to work around this issue by immediately attempting to re-open the file descriptor to the path. This is not bulletproof and may fail;
//      it all depends on the timing of disk I/O. Bottom line: you could not rely on it and might miss future changes to the file path you're supposedly watching. That's why
//      VDKQueue does not take this approach, but favors the "manual" method of "stop-watching-then-rewatch". 
//



#import <Foundation/Foundation.h>
#include <sys/types.h>
#include <sys/event.h>


//
//  Logical OR these values into the u_int that you pass in the -addPath:notifyingAbout: method
//  to specify the types of notifications you're interested in. Pass the default value to receive all of them.
//
#define VDKQueueNotifyAboutRename					NOTE_RENAME		// Item was renamed.
#define VDKQueueNotifyAboutWrite					NOTE_WRITE		// Item contents changed (also folder contents changed).
#define VDKQueueNotifyAboutDelete					NOTE_DELETE		// item was removed.
#define VDKQueueNotifyAboutAttributeChange			NOTE_ATTRIB		// Item attributes changed.
#define VDKQueueNotifyAboutSizeIncrease				NOTE_EXTEND		// Item size increased.
#define VDKQueueNotifyAboutLinkCountChanged			NOTE_LINK		// Item's link count changed.
#define VDKQueueNotifyAboutAccessRevocation			NOTE_REVOKE		// Access to item was revoked.

#define VDKQueueNotifyDefault						(VDKQueueNotifyAboutRename | VDKQueueNotifyAboutWrite \
                                                    | VDKQueueNotifyAboutDelete | VDKQueueNotifyAboutAttributeChange \
                                                    | VDKQueueNotifyAboutSizeIncrease | VDKQueueNotifyAboutLinkCountChanged \
                                                    | VDKQueueNotifyAboutAccessRevocation)

//
//  Notifications that this class sends to the NSWORKSPACE notification center.
//      Object          =   the instance of VDKQueue that was watching for changes
//      userInfo.path   =   the file path where the change was observed
//
extern NSString * VDKQueueRenameNotification;
extern NSString * VDKQueueWriteNotification;
extern NSString * VDKQueueDeleteNotification;
extern NSString * VDKQueueAttributeChangeNotification;
extern NSString * VDKQueueSizeIncreaseNotification;
extern NSString * VDKQueueLinkCountChangeNotification;
extern NSString * VDKQueueAccessRevocationNotification;



//
//  Or, instead of subscribing to notifications, you can specify a delegate and implement this method to respond to kQueue events.
//  Note the required statement! For speed, this class does not check to make sure the delegate implements this method. (When I say "required" I mean it!)
//
@class VDKQueue;
@protocol VDKQueueDelegate <NSObject>
@required

-(void) VDKQueue:(VDKQueue *)queue receivedNotification:(NSString*)noteName forPath:(NSString*)fpath;

@end





@interface VDKQueue : NSObject
{
    id<VDKQueueDelegate>    _delegate;
    BOOL                    _alwaysPostNotifications;               // By default, notifications are posted only if there is no delegate set. Set this value to YES to have notes posted even when there is a delegate.
    
@private
    int						_coreQueueFD;                           // The actual kqueue ID (Unix file descriptor).
	NSMutableDictionary    *_watchedPathEntries;                    // List of VDKQueuePathEntries. Keys are NSStrings of the path that each VDKQueuePathEntry is for.
    BOOL                    _keepWatcherThreadRunning;              // Set to NO to cancel the thread that watches _coreQueueFD for kQueue events
}


//
//  Note: there is no need to ask whether a path is already being watched. Just add it or remove it and this class
//        will take action only if appropriate. (Add only if we're not already watching it, remove only if we are.)
//  
//  Warning: You must pass full, root-relative paths. Do not pass tilde-abbreviated paths or file URLs. 
//
- (void) addPath:(NSString *)aPath;
- (void) addPath:(NSString *)aPath notifyingAbout:(u_int)flags;     // See note above for values to pass in "flags"

- (void) removePath:(NSString *)aPath;
- (void) removeAllPaths;


- (NSUInteger) numberOfWatchedPaths;                                //  Returns the number of paths that this VDKQueue instance is actively watching.



@property (assign) id<VDKQueueDelegate> delegate;
@property (assign) BOOL alwaysPostNotifications;

@end