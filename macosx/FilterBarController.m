/******************************************************************************
 * Copyright (c) 2011-2012 Transmission authors and contributors
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

#import "FilterBarController.h"
#import "FilterButton.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"

#define FILTER_TYPE_TAG_NAME    401
#define FILTER_TYPE_TAG_TRACKER 402

#define SEARCH_MIN_WIDTH 48.0
#define SEARCH_MAX_WIDTH 95.0

@interface FilterBarController (Private)

- (void) resizeBar;
- (void) updateGroupsButton;
- (void) updateGroups: (NSNotification *) notification;

@end

@implementation FilterBarController

- (id) init
{
    return (self = [super initWithNibName: @"FilterBar" bundle: nil]);
}

- (void) awakeFromNib
{
    //localizations
    [fNoFilterButton setTitle: NSLocalizedString(@"All", "Filter Bar -> filter button")];
    [fActiveFilterButton setTitle: NSLocalizedString(@"Active", "Filter Bar -> filter button")];
    [fDownloadFilterButton setTitle: NSLocalizedString(@"Downloading", "Filter Bar -> filter button")];
    [fSeedFilterButton setTitle: NSLocalizedString(@"Seeding", "Filter Bar -> filter button")];
    [fPauseFilterButton setTitle: NSLocalizedString(@"Paused", "Filter Bar -> filter button")];

    [[fNoFilterButton cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fActiveFilterButton cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fDownloadFilterButton cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fSeedFilterButton cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fPauseFilterButton cell] setBackgroundStyle: NSBackgroundStyleRaised];

    [[[[fSearchField cell] searchMenuTemplate] itemWithTag: FILTER_TYPE_TAG_NAME] setTitle:
        NSLocalizedString(@"Name", "Filter Bar -> filter menu")];
    [[[[fSearchField cell] searchMenuTemplate] itemWithTag: FILTER_TYPE_TAG_TRACKER] setTitle:
        NSLocalizedString(@"Tracker", "Filter Bar -> filter menu")];

    [[[fGroupsButton menu] itemWithTag: GROUP_FILTER_ALL_TAG] setTitle:
        NSLocalizedString(@"All Groups", "Filter Bar -> group filter menu")];

    [self resizeBar];

    //set current filter
    NSString * filterType = [[NSUserDefaults standardUserDefaults] stringForKey: @"Filter"];

    NSButton * currentFilterButton;
    if ([filterType isEqualToString: FILTER_ACTIVE])
        currentFilterButton = fActiveFilterButton;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        currentFilterButton = fPauseFilterButton;
    else if ([filterType isEqualToString: FILTER_SEED])
        currentFilterButton = fSeedFilterButton;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        currentFilterButton = fDownloadFilterButton;
    else
    {
        //safety
        if (![filterType isEqualToString: FILTER_NONE])
            [[NSUserDefaults standardUserDefaults] setObject: FILTER_NONE forKey: @"Filter"];
        currentFilterButton = fNoFilterButton;
    }
    [currentFilterButton setState: NSOnState];

    //set filter search type
    NSString * filterSearchType = [[NSUserDefaults standardUserDefaults] stringForKey: @"FilterSearchType"];

    NSMenu * filterSearchMenu = [[fSearchField cell] searchMenuTemplate];
    NSString * filterSearchTypeTitle;
    if ([filterSearchType isEqualToString: FILTER_TYPE_TRACKER])
        filterSearchTypeTitle = [[filterSearchMenu itemWithTag: FILTER_TYPE_TAG_TRACKER] title];
    else
    {
        //safety
        if (![filterType isEqualToString: FILTER_TYPE_NAME])
            [[NSUserDefaults standardUserDefaults] setObject: FILTER_TYPE_NAME forKey: @"FilterSearchType"];
        filterSearchTypeTitle = [[filterSearchMenu itemWithTag: FILTER_TYPE_TAG_NAME] title];
    }
    [[fSearchField cell] setPlaceholderString: filterSearchTypeTitle];

    NSString * searchString;
    if ((searchString = [[NSUserDefaults standardUserDefaults] stringForKey: @"FilterSearchString"]))
        [fSearchField setStringValue: searchString];

    [self updateGroupsButton];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(resizeBar)
        name: NSWindowDidResizeNotification object: [[self view] window]];

    //update when groups change
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateGroups:)
        name: @"UpdateGroups" object: nil];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
}

- (void) setFilter: (id) sender
{
    NSString * oldFilterType = [[NSUserDefaults standardUserDefaults] stringForKey: @"Filter"];

    NSButton * prevFilterButton;
    if ([oldFilterType isEqualToString: FILTER_PAUSE])
        prevFilterButton = fPauseFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_ACTIVE])
        prevFilterButton = fActiveFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_SEED])
        prevFilterButton = fSeedFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_DOWNLOAD])
        prevFilterButton = fDownloadFilterButton;
    else
        prevFilterButton = fNoFilterButton;

    if (sender != prevFilterButton)
    {
        [prevFilterButton setState: NSOffState];
        [sender setState: NSOnState];

        NSString * filterType;
        if (sender == fActiveFilterButton)
            filterType = FILTER_ACTIVE;
        else if (sender == fDownloadFilterButton)
            filterType = FILTER_DOWNLOAD;
        else if (sender == fPauseFilterButton)
            filterType = FILTER_PAUSE;
        else if (sender == fSeedFilterButton)
            filterType = FILTER_SEED;
        else
            filterType = FILTER_NONE;

        [[NSUserDefaults standardUserDefaults] setObject: filterType forKey: @"Filter"];
    }
    else
        [sender setState: NSOnState];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"ApplyFilter" object: nil];
}

- (void) switchFilter: (BOOL) right
{
    NSString * filterType = [[NSUserDefaults standardUserDefaults] stringForKey: @"Filter"];

    NSButton * button;
    if ([filterType isEqualToString: FILTER_NONE])
        button = right ? fActiveFilterButton : fPauseFilterButton;
    else if ([filterType isEqualToString: FILTER_ACTIVE])
        button = right ? fDownloadFilterButton : fNoFilterButton;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        button = right ? fSeedFilterButton : fActiveFilterButton;
    else if ([filterType isEqualToString: FILTER_SEED])
        button = right ? fPauseFilterButton : fDownloadFilterButton;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        button = right ? fNoFilterButton : fSeedFilterButton;
    else
        button = fNoFilterButton;

    [self setFilter: button];
}

- (void) setSearchText: (id) sender
{
    [[NSUserDefaults standardUserDefaults] setObject: [fSearchField stringValue] forKey: @"FilterSearchString"];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"ApplyFilter" object: nil];
}

- (void) focusSearchField
{
    [[[self view] window] makeFirstResponder: fSearchField];
}

- (void) setSearchType: (id) sender
{
    NSString * oldFilterType = [[NSUserDefaults standardUserDefaults] stringForKey: @"FilterSearchType"];

    NSInteger prevTag, currentTag = [sender tag];
    if ([oldFilterType isEqualToString: FILTER_TYPE_TRACKER])
        prevTag = FILTER_TYPE_TAG_TRACKER;
    else
        prevTag = FILTER_TYPE_TAG_NAME;

    if (currentTag != prevTag)
    {
        NSString * filterType;
        if (currentTag == FILTER_TYPE_TAG_TRACKER)
            filterType = FILTER_TYPE_TRACKER;
        else
            filterType = FILTER_TYPE_NAME;

        [[NSUserDefaults standardUserDefaults] setObject: filterType forKey: @"FilterSearchType"];

        [[fSearchField cell] setPlaceholderString: [sender title]];
    }

    [[NSNotificationCenter defaultCenter] postNotificationName: @"ApplyFilter" object: nil];
}

- (void) setGroupFilter: (id) sender
{
    [[NSUserDefaults standardUserDefaults] setInteger: [sender tag] forKey: @"FilterGroup"];
    [self updateGroupsButton];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"ApplyFilter" object: nil];
}

- (void) reset: (BOOL) updateUI
{
    [[NSUserDefaults standardUserDefaults] setInteger: GROUP_FILTER_ALL_TAG forKey: @"FilterGroup"];

    if (updateUI)
    {
        [self updateGroupsButton];

        [self setFilter: fNoFilterButton];

        [fSearchField setStringValue: @""];
        [self setSearchText: fSearchField];
    }
    else
    {
        [[NSUserDefaults standardUserDefaults] setObject: FILTER_NONE forKey: @"Filter"];
        [[NSUserDefaults standardUserDefaults] removeObjectForKey: @"FilterSearchString"];
    }
}

- (NSArray *) searchStrings
{
    return [[fSearchField stringValue] betterComponentsSeparatedByCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

- (void) setCountAll: (NSUInteger) all active: (NSUInteger) active downloading: (NSUInteger) downloading
        seeding: (NSUInteger) seeding paused: (NSUInteger) paused
{
    [fNoFilterButton setCount: all];
    [fActiveFilterButton setCount: active];
    [fDownloadFilterButton setCount: downloading];
    [fSeedFilterButton setCount: seeding];
    [fPauseFilterButton setCount: paused];
}

- (void) menuNeedsUpdate: (NSMenu *) menu
{
    if (menu == [fGroupsButton menu])
    {
        for (NSInteger i = [menu numberOfItems]-1; i >= 3; i--)
            [menu removeItemAtIndex: i];

        NSMenu * groupMenu = [[GroupsController groups] groupMenuWithTarget: self action: @selector(setGroupFilter:) isSmall: YES];

        const NSInteger groupMenuCount = [groupMenu numberOfItems];
        for (NSInteger i = 0; i < groupMenuCount; i++)
        {
            NSMenuItem * item = [groupMenu itemAtIndex: 0];
            [groupMenu removeItemAtIndex: 0];
            [menu addItem: item];
        }
    }
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    const SEL action = [menuItem action];

    //check proper filter search item
    if (action == @selector(setSearchType:))
    {
        NSString * filterType = [[NSUserDefaults standardUserDefaults] stringForKey: @"FilterSearchType"];

        BOOL state;
        if ([menuItem tag] == FILTER_TYPE_TAG_TRACKER)
            state = [filterType isEqualToString: FILTER_TYPE_TRACKER];
        else
            state = [filterType isEqualToString: FILTER_TYPE_NAME];

        [menuItem setState: state ? NSOnState : NSOffState];
        return YES;
    }

    if (action == @selector(setGroupFilter:))
    {
        [menuItem setState: [menuItem tag] == [[NSUserDefaults standardUserDefaults] integerForKey: @"FilterGroup"]
                                                ? NSOnState : NSOffState];
        return YES;
    }

    return YES;
}

@end

@implementation FilterBarController (Private)

- (void) resizeBar
{
    //replace all buttons
    [fNoFilterButton sizeToFit];
    [fActiveFilterButton sizeToFit];
    [fDownloadFilterButton sizeToFit];
    [fSeedFilterButton sizeToFit];
    [fPauseFilterButton sizeToFit];

    NSRect allRect = [fNoFilterButton frame];
    NSRect activeRect = [fActiveFilterButton frame];
    NSRect downloadRect = [fDownloadFilterButton frame];
    NSRect seedRect = [fSeedFilterButton frame];
    NSRect pauseRect = [fPauseFilterButton frame];

    //size search filter to not overlap buttons
    NSRect searchFrame = [fSearchField frame];
    searchFrame.origin.x = NSMaxX(pauseRect) + 5.0;
    searchFrame.size.width = NSWidth([[self view] frame]) - searchFrame.origin.x - 5.0;

    //make sure it is not too long
    if (NSWidth(searchFrame) > SEARCH_MAX_WIDTH)
    {
        searchFrame.origin.x += NSWidth(searchFrame) - SEARCH_MAX_WIDTH;
        searchFrame.size.width = SEARCH_MAX_WIDTH;
    }
    else if (NSWidth(searchFrame) < SEARCH_MIN_WIDTH)
    {
        searchFrame.origin.x += NSWidth(searchFrame) - SEARCH_MIN_WIDTH;
        searchFrame.size.width = SEARCH_MIN_WIDTH;

        //calculate width the buttons can take up
        const CGFloat allowedWidth = (searchFrame.origin.x - 5.0) - allRect.origin.x;
        const CGFloat currentWidth = NSWidth(allRect) + NSWidth(activeRect) + NSWidth(downloadRect) + NSWidth(seedRect)
                                        + NSWidth(pauseRect) + 4.0; //add 4 for space between buttons
        const CGFloat ratio = allowedWidth / currentWidth;

        //decrease button widths proportionally
        allRect.size.width  = NSWidth(allRect) * ratio;
        activeRect.size.width = NSWidth(activeRect) * ratio;
        downloadRect.size.width = NSWidth(downloadRect) * ratio;
        seedRect.size.width = NSWidth(seedRect) * ratio;
        pauseRect.size.width = NSWidth(pauseRect) * ratio;
    }
    else;

    activeRect.origin.x = NSMaxX(allRect) + 1.0;
    downloadRect.origin.x = NSMaxX(activeRect) + 1.0;
    seedRect.origin.x = NSMaxX(downloadRect) + 1.0;
    pauseRect.origin.x = NSMaxX(seedRect) + 1.0;

    [fNoFilterButton setFrame: allRect];
    [fActiveFilterButton setFrame: activeRect];
    [fDownloadFilterButton setFrame: downloadRect];
    [fSeedFilterButton setFrame: seedRect];
    [fPauseFilterButton setFrame: pauseRect];

    [fSearchField setFrame: searchFrame];
}

- (void) updateGroupsButton
{
    const NSInteger groupIndex = [[NSUserDefaults standardUserDefaults] integerForKey: @"FilterGroup"];

    NSImage * icon;
    NSString * toolTip;
    if (groupIndex == GROUP_FILTER_ALL_TAG)
    {
        icon = [NSImage imageNamed: @"PinTemplate"];
        toolTip = NSLocalizedString(@"All Groups", "Groups -> Button");
    }
    else
    {
        icon = [[GroupsController groups] imageForIndex: groupIndex];
        NSString * groupName = groupIndex != -1 ? [[GroupsController groups] nameForIndex: groupIndex]
                                                : NSLocalizedString(@"None", "Groups -> Button");
        toolTip = [NSLocalizedString(@"Group", "Groups -> Button") stringByAppendingFormat: @": %@", groupName];
    }

    [[[fGroupsButton menu] itemAtIndex: 0] setImage: icon];
    [fGroupsButton setToolTip: toolTip];
}

- (void) updateGroups: (NSNotification *) notification
{
    [self updateGroupsButton];
}

@end
