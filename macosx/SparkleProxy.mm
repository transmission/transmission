// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#if __has_feature(modules)
@import ObjectiveC;
@import AppKit;
#else
#import <objc/runtime.h>
#import <AppKit/AppKit.h>
#endif
#import "NSStringAdditions.h"

// Development-only proxy when app is not signed for running Sparkle
void SPUStandardUpdaterController_checkForUpdates(id /*self*/, SEL /*_cmd*/, ...)
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

// No-op so the nib's updaterDelegate outlet connection doesn't throw an unknown-key exception.
void SPUStandardUpdaterController_setUpdaterDelegate(id /*self*/, SEL /*_cmd*/, id /*delegate*/)
{
}

/// Proxy SPUStandardUpdaterController if isn't registered at program startup due to codesigning.
__attribute__((constructor)) static void registerSPUStandardUpdaterController()
{
    if (!objc_getClass("SPUStandardUpdaterController"))
    {
        NSLog(@"App is not signed for running Sparkle");
        Class updaterControllerClass = objc_allocateClassPair(objc_getClass("NSObject"), "SPUStandardUpdaterController", 0);
        class_addMethod(updaterControllerClass, sel_getUid("checkForUpdates:"), (IMP)SPUStandardUpdaterController_checkForUpdates, "v@:@");
        class_addMethod(updaterControllerClass, sel_getUid("setUpdaterDelegate:"), (IMP)SPUStandardUpdaterController_setUpdaterDelegate, "v@:@");
        objc_registerClassPair(updaterControllerClass);
    }
}
