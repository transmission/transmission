// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MenuBarItem.h"

#import "Controller.h"
#import "NSStringAdditions.h"

@implementation MenuBarItem
{
    NSStatusItem* fStatusItem;
    NSMenu* fMenu;
}

- (BOOL)isVisible
{
    return fStatusItem != nil;
}

- (void)showWithController:(Controller*)controller
{
    if (fStatusItem)
    {
        return;
    }

    fStatusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];

    NSButton* button = fStatusItem.button;
    button.image = [NSImage imageNamed:@"YingYangTemplate"];

    fMenu = [[NSMenu alloc] initWithTitle:@"Transmission"];

    NSMenuItem* showItem = [fMenu addItemWithTitle:NSLocalizedString(@"Show Transmission", "Menu bar item")
                                            action:@selector(showMainWindow:)
                                     keyEquivalent:@""];
    showItem.target = controller;

    [fMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* speedLimitItem = [fMenu addItemWithTitle:NSLocalizedString(@"Speed Limit", "Menu bar item")
                                                  action:@selector(toggleSpeedLimit:)
                                           keyEquivalent:@""];
    speedLimitItem.target = controller;

    [fMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* stopItem = [fMenu addItemWithTitle:NSLocalizedString(@"Pause All", "Menu bar item")
                                            action:@selector(stopAllTorrents:)
                                     keyEquivalent:@""];
    stopItem.target = controller;

    NSMenuItem* resumeItem = [fMenu addItemWithTitle:NSLocalizedString(@"Resume All", "Menu bar item")
                                              action:@selector(resumeAllTorrents:)
                                       keyEquivalent:@""];
    resumeItem.target = controller;

    [fMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* quitItem = [fMenu addItemWithTitle:NSLocalizedString(@"Quit Transmission", "Menu bar item")
                                            action:@selector(terminate:)
                                     keyEquivalent:@""];
    quitItem.target = NSApp;

    fStatusItem.menu = fMenu;
}

- (void)hide
{
    if (!fStatusItem)
    {
        return;
    }

    [NSStatusBar.systemStatusBar removeStatusItem:fStatusItem];
    fStatusItem = nil;
    fMenu = nil;
}

- (void)updateWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate
{
    if (!fStatusItem)
    {
        return;
    }

    NSString* text = [NSString localizedStringWithFormat:@"↓%@  ↑%@",
                                                         [NSString stringForSpeedAbbrev:downloadRate],
                                                         [NSString stringForSpeedAbbrev:uploadRate]];

    fStatusItem.button.attributedTitle = [[NSAttributedString alloc] initWithString:text
                                                                         attributes:@{ NSFontAttributeName :
                                                                                           [NSFont monospacedDigitSystemFontOfSize:
                                                                                                        NSFont.systemFontSize
                                                                                                               weight:NSFontWeightRegular] }];
}

@end
