// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PowerManager.h"

#include <os/log.h>

@interface PowerManager ()

@property(nonatomic, readonly) os_log_t log;
@property(getter=isListening) BOOL listening;

@property(nonatomic) id<NSObject> noNapActivity;
@property(nonatomic) id<NSObject> noSleepActivity;

- (void)systemWillSleep:(NSNotification*)notification;
- (void)systemDidWakeUp:(NSNotification*)notification;

- (void)powerStateDidChange:(NSNotification*)notification NS_AVAILABLE_MAC(12_0);

@end

@implementation PowerManager

+ (instancetype)shared
{
    static PowerManager* sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[PowerManager alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init
{
    if ((self = [super init]))
    {
        _log = os_log_create("org.transmission", "power");
        _listening = NO;
    }

    return self;
}

- (void)dealloc
{
    [self stop];
}

- (void)start
{
    os_log_info(self.log, "Starting power manager");
    if (!self.isListening)
    {
        os_log_debug(self.log, "Registering sleep/wake/low power mode notifications");
        [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(systemWillSleep:)
                                                               name:NSWorkspaceWillSleepNotification
                                                             object:nil];
        [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(systemDidWakeUp:)
                                                               name:NSWorkspaceDidWakeNotification
                                                             object:nil];
        if (@available(macOS 12.0, *))
        {
            [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(powerStateDidChange:)
                                                       name:NSProcessInfoPowerStateDidChangeNotification
                                                     object:nil];
        }
        self.listening = YES;
    }

    if (self.noNapActivity == nil)
    {
        os_log_debug(self.log, "Starting no-nap activity");
        self.noNapActivity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                                                          reason:@"Transmission: Application is active"];
    }
}

- (void)stop
{
    os_log_info(self.log, "Stopping power manager");
    if (self.isListening)
    {
        os_log_debug(self.log, "Unregistering sleep/wake/low power mode notifications");
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceWillSleepNotification object:nil];
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceDidWakeNotification object:nil];
        if (@available(macOS 12.0, *))
        {
            [NSNotificationCenter.defaultCenter removeObserver:self name:NSProcessInfoPowerStateDidChangeNotification object:nil];
        }
        self.listening = NO;
    }

    if (self.noNapActivity != nil)
    {
        os_log_debug(self.log, "Ending no-nap activity");
        [NSProcessInfo.processInfo endActivity:self.noNapActivity];
        self.noNapActivity = nil;
    }

    if (self.noSleepActivity != nil)
    {
        os_log_debug(self.log, "Ending no-sleep activity");
        [NSProcessInfo.processInfo endActivity:self.noSleepActivity];
        self.noSleepActivity = nil;
    }
}

- (void)systemWillSleep:(NSNotification*)notification
{
    os_log_info(self.log, "System will sleep notification received");
    [self.delegate systemWillSleep];
}

- (void)systemDidWakeUp:(NSNotification*)notification
{
    os_log_info(self.log, "System did wake up notification received");
    [self.delegate systemDidWakeUp];
}

- (void)powerStateDidChange:(NSNotification*)notification
{
    os_log_info(self.log, "Power state did change notification received");
    if (NSProcessInfo.processInfo.lowPowerModeEnabled)
    {
        os_log_info(self.log, "Low power mode enabled, disabling sleep prevention");
        self.shouldPreventSleep = NO;
    }
}

- (void)setShouldPreventSleep:(BOOL)shouldPreventSleep
{
    if (@available(macOS 12.0, *))
    {
        if (shouldPreventSleep && NSProcessInfo.processInfo.lowPowerModeEnabled)
        {
            return;
        }
    }

    if (shouldPreventSleep)
    {
        if (self.noSleepActivity != nil)
        {
            return;
        }

        os_log_info(self.log, "Starting no-sleep activity");
        self.noSleepActivity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityIdleSystemSleepDisabled
                                                                            reason:@"Transmission: Active Torrents"];
    }
    else
    {
        if (self.noSleepActivity == nil)
        {
            return;
        }

        os_log_info(self.log, "Ending no-sleep activity");
        [NSProcessInfo.processInfo endActivity:self.noSleepActivity];
        self.noSleepActivity = nil;
    }
}

- (BOOL)shouldPreventSleep
{
    return self.noSleepActivity != nil;
}

@end
