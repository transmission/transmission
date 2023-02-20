// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

@import ObjectiveC;
@import AppKit;
#import "NSStringAdditions.h"

// Development-only proxy when app is not signed for running Sparkle
void SUUpdater_checkForUpdates(id /*self*/, SEL /*_cmd*/, ...)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = LocalizationNotNeeded(@"Sparkle not configured");
        alert.informativeText = [NSString
            stringWithFormat:@"App needs to be codesigned for Development to support Sparkle with Hardened Runtime. Alternatively, re-codesign without the Hardened Runtime option: `sudo codesign -s - %@`",
                             NSBundle.mainBundle.bundleURL.lastPathComponent];
        [alert runModal];
    });
}

/// Proxy SUUpdater if isn't registered at program startup due to codesigning.
__attribute__((constructor)) static void registerSUUpdater()
{
    if (!objc_getClass("SUUpdater"))
    {
        NSLog(@"App is not signed for running Sparkle");
        Class SUUpdaterClass = objc_allocateClassPair(objc_getClass("NSObject"), "SUUpdater", 0);
        class_addMethod(SUUpdaterClass, sel_getUid("checkForUpdates:"), (IMP)SUUpdater_checkForUpdates, "v@:@");
        objc_registerClassPair(SUUpdaterClass);
    }
}
