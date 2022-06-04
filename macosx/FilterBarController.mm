// This file Copyright © 2011-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterBarController.h"
#import "FilterButton.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"

#define FILTER_TYPE_TAG_NAME 401
#define FILTER_TYPE_TAG_TRACKER 402

#define SEARCH_MIN_WIDTH 48.0
#define SEARCH_MAX_WIDTH 95.0

@interface FilterBarController ()

@property(nonatomic) IBOutlet FilterButton* fNoFilterButton;
@property(nonatomic) IBOutlet FilterButton* fActiveFilterButton;
@property(nonatomic) IBOutlet FilterButton* fDownloadFilterButton;
@property(nonatomic) IBOutlet FilterButton* fSeedFilterButton;
@property(nonatomic) IBOutlet FilterButton* fPauseFilterButton;
@property(nonatomic) IBOutlet FilterButton* fErrorFilterButton;

@property(nonatomic) IBOutlet NSSearchField* fSearchField;

@property(nonatomic) IBOutlet NSPopUpButton* fGroupsButton;

- (void)resizeBar;
- (void)updateGroupsButton;
- (void)updateGroups:(NSNotification*)notification;

@end

@implementation FilterBarController

- (instancetype)init
{
    self = [super initWithNibName:@"FilterBar" bundle:nil];
    return self;
}

- (void)awakeFromNib
{
    //localizations
    self.fNoFilterButton.title = NSLocalizedString(@"All", "Filter Bar -> filter button");
    self.fActiveFilterButton.title = NSLocalizedString(@"Active", "Filter Bar -> filter button");
    self.fDownloadFilterButton.title = NSLocalizedString(@"Downloading", "Filter Bar -> filter button");
    self.fSeedFilterButton.title = NSLocalizedString(@"Seeding", "Filter Bar -> filter button");
    self.fPauseFilterButton.title = NSLocalizedString(@"Paused", "Filter Bar -> filter button");
    self.fErrorFilterButton.title = NSLocalizedString(@"Error", "Filter Bar -> filter button");

    self.fNoFilterButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fActiveFilterButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fDownloadFilterButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fSeedFilterButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fPauseFilterButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fErrorFilterButton.cell.backgroundStyle = NSBackgroundStyleRaised;

    [self.fSearchField.searchMenuTemplate itemWithTag:FILTER_TYPE_TAG_NAME].title = NSLocalizedString(@"Name", "Filter Bar -> filter menu");
    [self.fSearchField.searchMenuTemplate itemWithTag:FILTER_TYPE_TAG_TRACKER].title = NSLocalizedString(@"Tracker", "Filter Bar -> filter menu");

    [self.fGroupsButton.menu itemWithTag:GROUP_FILTER_ALL_TAG].title = NSLocalizedString(@"All Groups", "Filter Bar -> group filter menu");

    [self resizeBar];

    //set current filter
    NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* currentFilterButton;
    if ([filterType isEqualToString:FILTER_ACTIVE])
    {
        currentFilterButton = self.fActiveFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_PAUSE])
    {
        currentFilterButton = self.fPauseFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_SEED])
    {
        currentFilterButton = self.fSeedFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_DOWNLOAD])
    {
        currentFilterButton = self.fDownloadFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_ERROR])
    {
        currentFilterButton = self.fErrorFilterButton;
    }
    else
    {
        //safety
        if (![filterType isEqualToString:FILTER_NONE])
        {
            [NSUserDefaults.standardUserDefaults setObject:FILTER_NONE forKey:@"Filter"];
        }
        currentFilterButton = self.fNoFilterButton;
    }
    currentFilterButton.state = NSControlStateValueOn;

    //set filter search type
    NSString* filterSearchType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

    NSMenu* filterSearchMenu = self.fSearchField.searchMenuTemplate;
    NSString* filterSearchTypeTitle;
    if ([filterSearchType isEqualToString:FILTER_TYPE_TRACKER])
    {
        filterSearchTypeTitle = [filterSearchMenu itemWithTag:FILTER_TYPE_TAG_TRACKER].title;
    }
    else
    {
        //safety
        if (![filterType isEqualToString:FILTER_TYPE_NAME])
        {
            [NSUserDefaults.standardUserDefaults setObject:FILTER_TYPE_NAME forKey:@"FilterSearchType"];
        }
        filterSearchTypeTitle = [filterSearchMenu itemWithTag:FILTER_TYPE_TAG_NAME].title;
    }
    self.fSearchField.placeholderString = filterSearchTypeTitle;

    NSString* searchString;
    if ((searchString = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchString"]))
    {
        self.fSearchField.stringValue = searchString;
    }

    [self updateGroupsButton];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(resizeBar) name:NSWindowDidResizeNotification
                                             object:self.view.window];

    //update when groups change
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateGroups:) name:@"UpdateGroups" object:nil];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)setFilter:(id)sender
{
    NSString* oldFilterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* prevFilterButton;
    if ([oldFilterType isEqualToString:FILTER_PAUSE])
    {
        prevFilterButton = self.fPauseFilterButton;
    }
    else if ([oldFilterType isEqualToString:FILTER_ACTIVE])
    {
        prevFilterButton = self.fActiveFilterButton;
    }
    else if ([oldFilterType isEqualToString:FILTER_SEED])
    {
        prevFilterButton = self.fSeedFilterButton;
    }
    else if ([oldFilterType isEqualToString:FILTER_DOWNLOAD])
    {
        prevFilterButton = self.fDownloadFilterButton;
    }
    else if ([oldFilterType isEqualToString:FILTER_ERROR])
    {
        prevFilterButton = self.fErrorFilterButton;
    }
    else
    {
        prevFilterButton = self.fNoFilterButton;
    }

    if (sender != prevFilterButton)
    {
        prevFilterButton.state = NSControlStateValueOff;
        [sender setState:NSControlStateValueOn];

        NSString* filterType;
        if (sender == self.fActiveFilterButton)
        {
            filterType = FILTER_ACTIVE;
        }
        else if (sender == self.fDownloadFilterButton)
        {
            filterType = FILTER_DOWNLOAD;
        }
        else if (sender == self.fPauseFilterButton)
        {
            filterType = FILTER_PAUSE;
        }
        else if (sender == self.fSeedFilterButton)
        {
            filterType = FILTER_SEED;
        }
        else if (sender == self.fErrorFilterButton)
        {
            filterType = FILTER_ERROR;
        }
        else
        {
            filterType = FILTER_NONE;
        }

        [NSUserDefaults.standardUserDefaults setObject:filterType forKey:@"Filter"];
    }
    else
    {
        [sender setState:NSControlStateValueOn];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)switchFilter:(BOOL)right
{
    NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* button;
    if ([filterType isEqualToString:FILTER_NONE])
    {
        button = right ? self.fActiveFilterButton : self.fErrorFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_ACTIVE])
    {
        button = right ? self.fDownloadFilterButton : self.fNoFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_DOWNLOAD])
    {
        button = right ? self.fSeedFilterButton : self.fActiveFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_SEED])
    {
        button = right ? self.fPauseFilterButton : self.fDownloadFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_PAUSE])
    {
        button = right ? self.fErrorFilterButton : self.fSeedFilterButton;
    }
    else if ([filterType isEqualToString:FILTER_ERROR])
    {
        button = right ? self.fNoFilterButton : self.fPauseFilterButton;
    }
    else
    {
        button = self.fNoFilterButton;
    }

    [self setFilter:button];
}

- (void)setSearchText:(id)sender
{
    [NSUserDefaults.standardUserDefaults setObject:self.fSearchField.stringValue forKey:@"FilterSearchString"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)focusSearchField
{
    [self.view.window makeFirstResponder:self.fSearchField];
}

- (void)setSearchType:(id)sender
{
    NSString* oldFilterType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

    NSInteger prevTag, currentTag = [sender tag];
    if ([oldFilterType isEqualToString:FILTER_TYPE_TRACKER])
    {
        prevTag = FILTER_TYPE_TAG_TRACKER;
    }
    else
    {
        prevTag = FILTER_TYPE_TAG_NAME;
    }

    if (currentTag != prevTag)
    {
        NSString* filterType;
        if (currentTag == FILTER_TYPE_TAG_TRACKER)
        {
            filterType = FILTER_TYPE_TRACKER;
        }
        else
        {
            filterType = FILTER_TYPE_NAME;
        }

        [NSUserDefaults.standardUserDefaults setObject:filterType forKey:@"FilterSearchType"];

        self.fSearchField.placeholderString = [sender title];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)setGroupFilter:(id)sender
{
    [NSUserDefaults.standardUserDefaults setInteger:[sender tag] forKey:@"FilterGroup"];
    [self updateGroupsButton];

    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)reset:(BOOL)updateUI
{
    [NSUserDefaults.standardUserDefaults setInteger:GROUP_FILTER_ALL_TAG forKey:@"FilterGroup"];

    if (updateUI)
    {
        [self updateGroupsButton];

        [self setFilter:self.fNoFilterButton];

        self.fSearchField.stringValue = @"";
        [self setSearchText:self.fSearchField];
    }
    else
    {
        [NSUserDefaults.standardUserDefaults setObject:FILTER_NONE forKey:@"Filter"];
        [NSUserDefaults.standardUserDefaults removeObjectForKey:@"FilterSearchString"];
    }
}

- (NSArray<NSString*>*)searchStrings
{
    return [self.fSearchField.stringValue nonEmptyComponentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

- (void)setCountAll:(NSUInteger)all
             active:(NSUInteger)active
        downloading:(NSUInteger)downloading
            seeding:(NSUInteger)seeding
             paused:(NSUInteger)paused
              error:(NSUInteger)error
{
    self.fNoFilterButton.count = all;
    self.fActiveFilterButton.count = active;
    self.fDownloadFilterButton.count = downloading;
    self.fSeedFilterButton.count = seeding;
    self.fPauseFilterButton.count = paused;
    self.fErrorFilterButton.count = error;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if (menu == self.fGroupsButton.menu)
    {
        for (NSInteger i = menu.numberOfItems - 1; i >= 3; i--)
        {
            [menu removeItemAtIndex:i];
        }

        NSMenu* groupMenu = [GroupsController.groups groupMenuWithTarget:self action:@selector(setGroupFilter:) isSmall:YES];

        NSInteger const groupMenuCount = groupMenu.numberOfItems;
        for (NSInteger i = 0; i < groupMenuCount; i++)
        {
            NSMenuItem* item = [groupMenu itemAtIndex:0];
            [groupMenu removeItemAtIndex:0];
            [menu addItem:item];
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    //check proper filter search item
    if (action == @selector(setSearchType:))
    {
        NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

        BOOL state;
        if (menuItem.tag == FILTER_TYPE_TAG_TRACKER)
        {
            state = [filterType isEqualToString:FILTER_TYPE_TRACKER];
        }
        else
        {
            state = [filterType isEqualToString:FILTER_TYPE_NAME];
        }

        menuItem.state = state ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }

    if (action == @selector(setGroupFilter:))
    {
        menuItem.state = menuItem.tag == [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"] ? NSControlStateValueOn :
                                                                                                              NSControlStateValueOff;
        return YES;
    }

    return YES;
}

#pragma mark - Private

- (void)resizeBar
{
    //replace all buttons
    [self.fNoFilterButton sizeToFit];
    [self.fActiveFilterButton sizeToFit];
    [self.fDownloadFilterButton sizeToFit];
    [self.fSeedFilterButton sizeToFit];
    [self.fPauseFilterButton sizeToFit];
    [self.fErrorFilterButton sizeToFit];

    NSRect allRect = self.fNoFilterButton.frame;
    NSRect activeRect = self.fActiveFilterButton.frame;
    NSRect downloadRect = self.fDownloadFilterButton.frame;
    NSRect seedRect = self.fSeedFilterButton.frame;
    NSRect pauseRect = self.fPauseFilterButton.frame;
    NSRect errorRect = self.fErrorFilterButton.frame;

    //size search filter to not overlap buttons
    NSRect searchFrame = self.fSearchField.frame;
    searchFrame.origin.x = NSMaxX(errorRect) + 5.0;
    searchFrame.size.width = NSWidth(self.view.frame) - searchFrame.origin.x - 5.0;

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
        CGFloat const allowedWidth = (searchFrame.origin.x - 5.0) - allRect.origin.x;
        CGFloat const currentWidth = NSWidth(allRect) + NSWidth(activeRect) + NSWidth(downloadRect) + NSWidth(seedRect) +
            NSWidth(pauseRect) + NSWidth(errorRect) + 4.0; //add 4 for space between buttons
        CGFloat const ratio = allowedWidth / currentWidth;

        //decrease button widths proportionally
        allRect.size.width = NSWidth(allRect) * ratio;
        activeRect.size.width = NSWidth(activeRect) * ratio;
        downloadRect.size.width = NSWidth(downloadRect) * ratio;
        seedRect.size.width = NSWidth(seedRect) * ratio;
        pauseRect.size.width = NSWidth(pauseRect) * ratio;
        errorRect.size.width = NSWidth(errorRect) * ratio;
    }

    activeRect.origin.x = NSMaxX(allRect) + 1.0;
    downloadRect.origin.x = NSMaxX(activeRect) + 1.0;
    seedRect.origin.x = NSMaxX(downloadRect) + 1.0;
    pauseRect.origin.x = NSMaxX(seedRect) + 1.0;
    errorRect.origin.x = NSMaxX(pauseRect) + 1.0;

    self.fNoFilterButton.frame = allRect;
    self.fActiveFilterButton.frame = activeRect;
    self.fDownloadFilterButton.frame = downloadRect;
    self.fSeedFilterButton.frame = seedRect;
    self.fPauseFilterButton.frame = pauseRect;
    self.fErrorFilterButton.frame = errorRect;

    self.fSearchField.frame = searchFrame;
}

- (void)updateGroupsButton
{
    NSInteger const groupIndex = [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"];

    NSImage* icon;
    NSString* toolTip;
    if (groupIndex == GROUP_FILTER_ALL_TAG)
    {
        icon = [NSImage imageNamed:@"PinTemplate"];
        toolTip = NSLocalizedString(@"All Groups", "Groups -> Button");
    }
    else
    {
        icon = [GroupsController.groups imageForIndex:groupIndex];
        NSString* groupName = groupIndex != -1 ? [GroupsController.groups nameForIndex:groupIndex] :
                                                 NSLocalizedString(@"None", "Groups -> Button");
        toolTip = [NSLocalizedString(@"Group", "Groups -> Button") stringByAppendingFormat:@": %@", groupName];
    }

    [self.fGroupsButton.menu itemAtIndex:0].image = icon;
    self.fGroupsButton.toolTip = toolTip;
}

- (void)updateGroups:(NSNotification*)notification
{
    [self updateGroupsButton];
}

@end
