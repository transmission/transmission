// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PowerManager.h"

#include <os/log.h>

@interface PowerManager ()

@property(nonatomic) BOOL fIsListening;

@property(nonatomic) id<NSObject> fNoNapActivity;
@property(nonatomic) id<NSObject> fNoSleepActivity;

- (void)systemWillSleep:(NSNotification*)notification;
- (void)systemDidWakeUp:(NSNotification*)notification;

- (void)powerStateDidChange:(NSNotification*)notification NS_AVAILABLE_MAC(12_0);

@end

@implementation PowerManager
{
    os_log_t log;
}

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
        log = os_log_create("org.transmission", "power");
        _fIsListening = NO;
        _fNoNapActivity = nil;
        _fNoSleepActivity = nil;
    }

    return self;
}

- (void)dealloc
{
    [self stop];
}

- (void)start
{
    os_log_info(log, "Starting power manager");
    if (!self.fIsListening)
    {
        os_log_debug(log, "Registering sleep/wake/low power mode notifications");
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
        self.fIsListening = YES;
    }

    if (self.fNoNapActivity == nil)
    {
        os_log_debug(log, "Starting no-nap activity");
        self.fNoNapActivity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                                                           reason:@"Transmission: Application is active"];
    }
}

- (void)stop
{
    os_log_info(log, "Stopping power manager");
    if (self.fIsListening)
    {
        os_log_debug(log, "Unregistering sleep/wake/low power mode notifications");
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceWillSleepNotification object:nil];
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceDidWakeNotification object:nil];
        if (@available(macOS 12.0, *))
        {
            [NSNotificationCenter.defaultCenter removeObserver:self name:NSProcessInfoPowerStateDidChangeNotification object:nil];
        }
        self.fIsListening = NO;
    }

    if (self.fNoNapActivity != nil)
    {
        os_log_debug(log, "Ending no-nap activity");
        [NSProcessInfo.processInfo endActivity:self.fNoNapActivity];
        self.fNoNapActivity = nil;
    }

    if (self.fNoSleepActivity != nil)
    {
        os_log_debug(log, "Ending no-sleep activity");
        [NSProcessInfo.processInfo endActivity:self.fNoSleepActivity];
        self.fNoSleepActivity = nil;
    }
}

- (void)systemWillSleep:(NSNotification*)notification
{
    os_log_info(log, "System will sleep notification received");
    [self.delegate systemWillSleep];
}

- (void)systemDidWakeUp:(NSNotification*)notification
{
    os_log_info(log, "System did wake up notification received");
    [self.delegate systemDidWakeUp];
}

- (void)powerStateDidChange:(NSNotification*)notification
{
    os_log_info(log, "Power state did change notification received");
    if (NSProcessInfo.processInfo.lowPowerModeEnabled)
    {
        os_log_info(log, "Low power mode enabled, disabling sleep prevention");
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
        if (self.fNoSleepActivity != nil)
        {
            return;
        }

        os_log_info(log, "Starting no-sleep activity");
        self.fNoSleepActivity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityIdleSystemSleepDisabled
                                                                             reason:@"Transmission: Active Torrents"];
    }
    else
    {
        if (self.fNoSleepActivity == nil)
        {
            return;
        }

        os_log_info(log, "Ending no-sleep activity");
        [NSProcessInfo.processInfo endActivity:self.fNoSleepActivity];
        self.fNoSleepActivity = nil;
    }
}

- (BOOL)shouldPreventSleep
{
    return self.fNoSleepActivity != nil;
}

@end
