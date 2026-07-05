// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#if __has_feature(modules)
@import AppKit;
@import UserNotifications;
#else
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>
#endif

#include <libtransmission/torrent-metainfo.h>

#import "AutoImportController.h"

@interface AutoImportController ()

@property(nonatomic, readonly) NSUserDefaults* fDefaults;
@property(nonatomic, weak) id<AutoImportControllerDelegate> delegate;
@property(nonatomic) NSMutableArray<NSString*>* fAutoImportedNames;
@property(nonatomic) NSTimer* fAutoImportTimer;

@end

@implementation AutoImportController

- (instancetype)initWithDefaults:(NSUserDefaults*)defaults delegate:(id<AutoImportControllerDelegate>)delegate
{
    if ((self = [super init]))
    {
        _fDefaults = defaults;
        _delegate = delegate;

        _fileWatcherQueue = [[VDKQueue alloc] init];
        _fileWatcherQueue.delegate = self;

        [self updateWatchedAutoImportDirectory];

        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(changeAutoImport)
                                                   name:@"AutoImportSettingChange"
                                                 object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(beginCreateFile:) name:@"BeginCreateTorrentFile"
                                                 object:nil];
    }

    return self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self.fAutoImportTimer invalidate];
    [self.fileWatcherQueue removeAllPaths];
}

- (void)VDKQueue:(VDKQueue*)queue receivedNotification:(NSString*)notification forPath:(NSString*)fpath
{
    // Don't assume that just because we're watching for write notification, we'll only receive write notifications.
    if (![self.fDefaults boolForKey:@"AutoImport"] || ![self.fDefaults stringForKey:@"AutoImportDirectory"])
    {
        return;
    }

    if (self.fAutoImportTimer.valid)
    {
        [self.fAutoImportTimer invalidate];
    }

    // Check again in 10 seconds in case torrent file wasn't complete.
    __weak __auto_type weakSelf = self;
    self.fAutoImportTimer = [NSTimer scheduledTimerWithTimeInterval:10.0 repeats:NO block:^(NSTimer* _Nonnull __unused timer) {
        [weakSelf checkAutoImportDirectory];
    }];

    [self checkAutoImportDirectory];
}

- (void)changeAutoImport
{
    if (self.fAutoImportTimer.valid)
    {
        [self.fAutoImportTimer invalidate];
    }
    self.fAutoImportTimer = nil;

    self.fAutoImportedNames = nil;

    [self updateWatchedAutoImportDirectory];
    [self checkAutoImportDirectory];
}

- (void)updateWatchedAutoImportDirectory
{
    [self.fileWatcherQueue removeAllPaths];

    NSString* path = [self.fDefaults stringForKey:@"AutoImportDirectory"];
    if ([self.fDefaults boolForKey:@"AutoImport"] && path)
    {
        [self.fileWatcherQueue addPath:path.stringByExpandingTildeInPath notifyingAbout:VDKQueueNotifyAboutWrite];
    }
}

- (void)checkAutoImportDirectory
{
    NSString* path;
    if (![self.fDefaults boolForKey:@"AutoImport"] || !(path = [self.fDefaults stringForKey:@"AutoImportDirectory"]))
    {
        return;
    }

    path = path.stringByExpandingTildeInPath;

    NSArray<NSString*>* importedNames;
    if (!(importedNames = [NSFileManager.defaultManager contentsOfDirectoryAtPath:path error:NULL]))
    {
        return;
    }

    // Only check files that have not been checked yet.
    NSMutableArray* newNames = [importedNames mutableCopy];

    if (self.fAutoImportedNames)
    {
        [newNames removeObjectsInArray:self.fAutoImportedNames];
    }
    else
    {
        self.fAutoImportedNames = [[NSMutableArray alloc] init];
    }
    [self.fAutoImportedNames setArray:importedNames];

    for (NSString* file in newNames)
    {
        if ([file hasPrefix:@"."])
        {
            continue;
        }

        NSString* fullFile = [path stringByAppendingPathComponent:file];

        if (!([[NSWorkspace.sharedWorkspace typeOfFile:fullFile error:NULL] isEqualToString:@"org.bittorrent.torrent"] ||
              [fullFile.pathExtension caseInsensitiveCompare:@"torrent"] == NSOrderedSame))
        {
            continue;
        }

        NSDictionary<NSFileAttributeKey, id>* fileAttributes = [NSFileManager.defaultManager attributesOfItemAtPath:fullFile
                                                                                                              error:nil];
        if (fileAttributes.fileSize == 0)
        {
            // Workaround for Firefox downloads happening in two steps: first time being an empty file.
            [self.fAutoImportedNames removeObject:file];
            continue;
        }

        auto metainfo = tr_torrent_metainfo{};
        if (!metainfo.parse_torrent_file(fullFile.UTF8String))
        {
            continue;
        }

        [self.delegate autoImportController:self openTorrentFile:fullFile];

        NSString* notificationTitle = NSLocalizedString(@"Torrent File Auto Added", "notification title");

        NSString* identifier = [@"Torrent File Auto Added " stringByAppendingString:file];
        UNMutableNotificationContent* content = [UNMutableNotificationContent new];
        content.title = notificationTitle;
        content.body = file;

        UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:identifier content:content trigger:nil];
        [UNUserNotificationCenter.currentNotificationCenter addNotificationRequest:request withCompletionHandler:nil];
    }
}

- (void)beginCreateFile:(NSNotification*)notification
{
    if (![self.fDefaults boolForKey:@"AutoImport"])
    {
        return;
    }

    NSString* location = ((NSURL*)notification.object).path;
    NSString* path = [self.fDefaults stringForKey:@"AutoImportDirectory"];

    NSString* parentPath = location.stringByDeletingLastPathComponent.stringByExpandingTildeInPath;
    if (location && path && [parentPath isEqualToString:path.stringByExpandingTildeInPath])
    {
        [self.fAutoImportedNames addObject:location.lastPathComponent];
    }
}

@end
