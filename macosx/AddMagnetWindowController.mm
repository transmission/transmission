/******************************************************************************
 * Copyright (c) 2010-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "AddMagnetWindowController.h"
#import "Controller.h"
#import "ExpandedPathToIconTransformer.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

#define POPUP_PRIORITY_HIGH 0
#define POPUP_PRIORITY_NORMAL 1
#define POPUP_PRIORITY_LOW 2

@interface AddMagnetWindowController (Private)

- (void)confirmAdd;

- (void)setDestinationPath:(NSString*)destination determinationType:(TorrentDeterminationType)determinationType;

- (void)setGroupsMenu;
- (void)changeGroupValue:(id)sender;

@end

@implementation AddMagnetWindowController

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller
{
    if ((self = [super initWithWindowNibName:@"AddMagnetWindow"]))
    {
        fTorrent = torrent;
        fDestination = path.stringByExpandingTildeInPath;

        fController = controller;

        fGroupValue = torrent.groupValue;
        fGroupDeterminationType = TorrentDeterminationAutomatic;
    }
    return self;
}

- (void)awakeFromNib
{
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateGroupMenu:) name:@"UpdateGroups" object:nil];

    NSString* name = fTorrent.name;
    self.window.title = name;
    fNameField.stringValue = name;
    fNameField.toolTip = name;

    [self setGroupsMenu];
    [fGroupPopUp selectItemWithTag:fGroupValue];

    NSInteger priorityIndex;
    switch (fTorrent.priority)
    {
    case TR_PRI_HIGH:
        priorityIndex = POPUP_PRIORITY_HIGH;
        break;
    case TR_PRI_NORMAL:
        priorityIndex = POPUP_PRIORITY_NORMAL;
        break;
    case TR_PRI_LOW:
        priorityIndex = POPUP_PRIORITY_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown priority for adding torrent: %d", fTorrent.priority);
        priorityIndex = POPUP_PRIORITY_NORMAL;
    }
    [fPriorityPopUp selectItemAtIndex:priorityIndex];

    fStartCheck.state = [NSUserDefaults.standardUserDefaults boolForKey:@"AutoStartDownload"] ? NSOnState : NSOffState;

    if (fDestination)
    {
        [self setDestinationPath:fDestination determinationType:TorrentDeterminationAutomatic];
    }
    else
    {
        fLocationField.stringValue = @"";
        fLocationImageView.image = nil;
    }

#warning when 10.7-only, switch to auto layout
    [fMagnetLinkLabel sizeToFit];

    CGFloat const downloadToLabelOldWidth = fDownloadToLabel.frame.size.width;
    [fDownloadToLabel sizeToFit];
    CGFloat const changeDestOldWidth = fChangeDestinationButton.frame.size.width;
    [fChangeDestinationButton sizeToFit];
    NSRect changeDestFrame = fChangeDestinationButton.frame;
    changeDestFrame.origin.x -= changeDestFrame.size.width - changeDestOldWidth;
    fChangeDestinationButton.frame = changeDestFrame;

    NSRect downloadToBoxFrame = fDownloadToBox.frame;
    CGFloat const downloadToBoxSizeDiff = (fDownloadToLabel.frame.size.width - downloadToLabelOldWidth) +
        (changeDestFrame.size.width - changeDestOldWidth);
    downloadToBoxFrame.size.width -= downloadToBoxSizeDiff;
    downloadToBoxFrame.origin.x -= downloadToLabelOldWidth - fDownloadToLabel.frame.size.width;
    fDownloadToBox.frame = downloadToBoxFrame;

    NSRect groupPopUpFrame = fGroupPopUp.frame;
    NSRect priorityPopUpFrame = fPriorityPopUp.frame;
    CGFloat const popUpOffset = groupPopUpFrame.origin.x - NSMaxX(fGroupLabel.frame);
    [fGroupLabel sizeToFit];
    [fPriorityLabel sizeToFit];
    NSRect groupLabelFrame = fGroupLabel.frame;
    NSRect priorityLabelFrame = fPriorityLabel.frame;
    //first bring them both to the left edge
    groupLabelFrame.origin.x = MIN(groupLabelFrame.origin.x, priorityLabelFrame.origin.x);
    priorityLabelFrame.origin.x = MIN(groupLabelFrame.origin.x, priorityLabelFrame.origin.x);
    //then align on the right
    CGFloat const labelWidth = MAX(groupLabelFrame.size.width, priorityLabelFrame.size.width);
    groupLabelFrame.origin.x += labelWidth - groupLabelFrame.size.width;
    priorityLabelFrame.origin.x += labelWidth - priorityLabelFrame.size.width;
    groupPopUpFrame.origin.x = NSMaxX(groupLabelFrame) + popUpOffset;
    priorityPopUpFrame.origin.x = NSMaxX(priorityLabelFrame) + popUpOffset;
    fGroupLabel.frame = groupLabelFrame;
    fGroupPopUp.frame = groupPopUpFrame;
    fPriorityLabel.frame = priorityLabelFrame;
    fPriorityPopUp.frame = priorityPopUpFrame;

    CGFloat const minButtonWidth = 82.0;
    CGFloat const oldAddButtonWidth = fAddButton.bounds.size.width;
    CGFloat const oldCancelButtonWidth = fCancelButton.bounds.size.width;
    [fAddButton sizeToFit];
    [fCancelButton sizeToFit];
    NSRect addButtonFrame = fAddButton.frame;
    NSRect cancelButtonFrame = fCancelButton.frame;
    CGFloat buttonWidth = MAX(addButtonFrame.size.width, cancelButtonFrame.size.width);
    buttonWidth = MAX(buttonWidth, minButtonWidth);
    addButtonFrame.size.width = buttonWidth;
    cancelButtonFrame.size.width = buttonWidth;
    CGFloat const addButtonWidthIncrease = buttonWidth - oldAddButtonWidth;
    addButtonFrame.origin.x -= addButtonWidthIncrease;
    cancelButtonFrame.origin.x -= addButtonWidthIncrease + (buttonWidth - oldCancelButtonWidth);
    fAddButton.frame = addButtonFrame;
    fCancelButton.frame = cancelButtonFrame;

    [fStartCheck sizeToFit];
}

- (void)windowDidLoad
{
    //if there is no destination, prompt for one right away
    if (!fDestination)
    {
        [self setDestination:nil];
    }
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (Torrent*)torrent
{
    return fTorrent;
}

- (void)setDestination:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.prompt = NSLocalizedString(@"Select", "Open torrent -> prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;

    panel.message = [NSString stringWithFormat:NSLocalizedString(@"Select the download folder for \"%@\"", "Add -> select destination folder"),
                                               fTorrent.name];

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            [self setDestinationPath:panel.URLs[0].path determinationType:TorrentDeterminationUserSpecified];
        }
        else
        {
            if (!fDestination)
            {
                [self performSelectorOnMainThread:@selector(cancelAdd:) withObject:nil waitUntilDone:NO];
            }
        }
    }];
}

- (void)add:(id)sender
{
    if ([fDestination.lastPathComponent isEqualToString:fTorrent.name] &&
        [NSUserDefaults.standardUserDefaults boolForKey:@"WarningFolderDataSameName"])
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"The destination directory and root data directory have the same name.", "Add torrent -> same name -> title");
        alert.informativeText = NSLocalizedString(
            @"If you are attempting to use already existing data,"
             " the root data directory should be inside the destination directory.",
            "Add torrent -> same name -> message");
        alert.alertStyle = NSWarningAlertStyle;
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Add torrent -> same name -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Add", "Add torrent -> same name -> button")];
        alert.showsSuppressionButton = YES;

        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
            if (alert.suppressionButton.state == NSOnState)
            {
                [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningFolderDataSameName"];
            }

            if (returnCode == NSAlertSecondButtonReturn)
            {
                [self performSelectorOnMainThread:@selector(confirmAdd) withObject:nil waitUntilDone:NO];
            }
        }];
    }
    else
    {
        [self confirmAdd];
    }
}

- (void)cancelAdd:(id)sender
{
    [self.window performClose:sender];
}

//only called on cancel
- (BOOL)windowShouldClose:(id)window
{
    [fController askOpenMagnetConfirmed:self add:NO];
    return YES;
}

- (void)changePriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
    case POPUP_PRIORITY_HIGH:
        priority = TR_PRI_HIGH;
        break;
    case POPUP_PRIORITY_NORMAL:
        priority = TR_PRI_NORMAL;
        break;
    case POPUP_PRIORITY_LOW:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown priority tag for adding torrent: %ld", [sender tag]);
        priority = TR_PRI_NORMAL;
    }
    fTorrent.priority = priority;
}

- (void)updateGroupMenu:(NSNotification*)notification
{
    [self setGroupsMenu];
    if (![fGroupPopUp selectItemWithTag:fGroupValue])
    {
        fGroupValue = -1;
        fGroupDeterminationType = TorrentDeterminationAutomatic;
        [fGroupPopUp selectItemWithTag:fGroupValue];
    }
}

@end

@implementation AddMagnetWindowController (Private)

- (void)confirmAdd
{
    [fTorrent setGroupValue:fGroupValue determinationType:fGroupDeterminationType];

    if (fStartCheck.state == NSOnState)
    {
        [fTorrent startTransfer];
    }

    [self close];
    [fController askOpenMagnetConfirmed:self add:YES];
}

- (void)setDestinationPath:(NSString*)destination determinationType:(TorrentDeterminationType)determinationType
{
    destination = destination.stringByExpandingTildeInPath;
    if (!fDestination || ![fDestination isEqualToString:destination])
    {
        fDestination = destination;

        [fTorrent changeDownloadFolderBeforeUsing:fDestination determinationType:determinationType];
    }

    fLocationField.stringValue = fDestination.stringByAbbreviatingWithTildeInPath;
    fLocationField.toolTip = fDestination;

    ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
    fLocationImageView.image = [iconTransformer transformedValue:fDestination];
}

- (void)setGroupsMenu
{
    NSMenu* groupMenu = [GroupsController.groups groupMenuWithTarget:self action:@selector(changeGroupValue:) isSmall:NO];
    fGroupPopUp.menu = groupMenu;
}

- (void)changeGroupValue:(id)sender
{
    NSInteger previousGroup = fGroupValue;
    fGroupValue = [sender tag];
    fGroupDeterminationType = TorrentDeterminationUserSpecified;

    if ([GroupsController.groups usesCustomDownloadLocationForIndex:fGroupValue])
    {
        [self setDestinationPath:[GroupsController.groups customDownloadLocationForIndex:fGroupValue]
               determinationType:TorrentDeterminationAutomatic];
    }
    else if ([fDestination isEqualToString:[GroupsController.groups customDownloadLocationForIndex:previousGroup]])
    {
        [self setDestinationPath:[NSUserDefaults.standardUserDefaults stringForKey:@"DownloadFolder"]
               determinationType:TorrentDeterminationAutomatic];
    }
}

@end
