// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterBarController.h"
#import "FilterButton.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"

FilterType const FilterTypeNone = @"None";
FilterType const FilterTypeActive = @"Active";
FilterType const FilterTypeDownload = @"Download";
FilterType const FilterTypeSeed = @"Seed";
FilterType const FilterTypePause = @"Pause";
FilterType const FilterTypeError = @"Error";

FilterSearchType const FilterSearchTypeName = @"Name";
FilterSearchType const FilterSearchTypeTracker = @"Tracker";

NSInteger const kGroupFilterAllTag = -2;

typedef NS_ENUM(NSInteger, FilterTypeTag) {
    FilterTypeTagName = 401,
    FilterTypeTagTracker = 402,
};

@interface FilterBarController ()<NSSearchFieldDelegate>

@property(nonatomic) IBOutlet FilterButton* fNoFilterButton;
@property(nonatomic) IBOutlet FilterButton* fActiveFilterButton;
@property(nonatomic) IBOutlet FilterButton* fDownloadFilterButton;
@property(nonatomic) IBOutlet FilterButton* fSeedFilterButton;
@property(nonatomic) IBOutlet FilterButton* fPauseFilterButton;
@property(nonatomic) IBOutlet FilterButton* fErrorFilterButton;

@property(nonatomic) IBOutlet NSSearchField* fSearchField;
@property(nonatomic) IBOutlet NSLayoutConstraint* fSearchFieldMinWidthConstraint;

@property(nonatomic) IBOutlet NSPopUpButton* fGroupsButton;

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

    [self.fSearchField.searchMenuTemplate itemWithTag:FilterTypeTagName].title = NSLocalizedString(@"Name", "Filter Bar -> filter menu");
    [self.fSearchField.searchMenuTemplate itemWithTag:FilterTypeTagTracker].title = NSLocalizedString(@"Tracker", "Filter Bar -> filter menu");

    [self.fGroupsButton.menu itemWithTag:kGroupFilterAllTag].title = NSLocalizedString(@"All Groups", "Filter Bar -> group filter menu");

    //set current filter
    NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* currentFilterButton;
    if ([filterType isEqualToString:FilterTypeActive])
    {
        currentFilterButton = self.fActiveFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypePause])
    {
        currentFilterButton = self.fPauseFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeSeed])
    {
        currentFilterButton = self.fSeedFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeDownload])
    {
        currentFilterButton = self.fDownloadFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeError])
    {
        currentFilterButton = self.fErrorFilterButton;
    }
    else
    {
        //safety
        if (![filterType isEqualToString:FilterTypeNone])
        {
            [NSUserDefaults.standardUserDefaults setObject:FilterTypeNone forKey:@"Filter"];
        }
        currentFilterButton = self.fNoFilterButton;
    }
    currentFilterButton.state = NSControlStateValueOn;

    //set filter search type
    NSString* filterSearchType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

    NSMenu* filterSearchMenu = self.fSearchField.searchMenuTemplate;
    NSString* filterSearchTypeTitle;
    if ([filterSearchType isEqualToString:FilterSearchTypeTracker])
    {
        filterSearchTypeTitle = [filterSearchMenu itemWithTag:FilterTypeTagTracker].title;
    }
    else
    {
        //safety
        if (![filterType isEqualToString:FilterSearchTypeName])
        {
            [NSUserDefaults.standardUserDefaults setObject:FilterSearchTypeName forKey:@"FilterSearchType"];
        }
        filterSearchTypeTitle = [filterSearchMenu itemWithTag:FilterTypeTagName].title;
    }
    self.fSearchField.placeholderString = filterSearchTypeTitle;

    NSString* searchString;
    if ((searchString = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchString"]))
    {
        self.fSearchField.stringValue = searchString;
    }

    [self updateGroupsButton];

    // update when groups change
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateGroups:) name:@"UpdateGroups" object:nil];

    // update when filter change
    self.fSearchField.delegate = self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)setFilter:(id)sender
{
    NSString* oldFilterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* prevFilterButton;
    if ([oldFilterType isEqualToString:FilterTypePause])
    {
        prevFilterButton = self.fPauseFilterButton;
    }
    else if ([oldFilterType isEqualToString:FilterTypeActive])
    {
        prevFilterButton = self.fActiveFilterButton;
    }
    else if ([oldFilterType isEqualToString:FilterTypeSeed])
    {
        prevFilterButton = self.fSeedFilterButton;
    }
    else if ([oldFilterType isEqualToString:FilterTypeDownload])
    {
        prevFilterButton = self.fDownloadFilterButton;
    }
    else if ([oldFilterType isEqualToString:FilterTypeError])
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

        FilterType filterType;
        if (sender == self.fActiveFilterButton)
        {
            filterType = FilterTypeActive;
        }
        else if (sender == self.fDownloadFilterButton)
        {
            filterType = FilterTypeDownload;
        }
        else if (sender == self.fPauseFilterButton)
        {
            filterType = FilterTypePause;
        }
        else if (sender == self.fSeedFilterButton)
        {
            filterType = FilterTypeSeed;
        }
        else if (sender == self.fErrorFilterButton)
        {
            filterType = FilterTypeError;
        }
        else
        {
            filterType = FilterTypeNone;
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
    if ([filterType isEqualToString:FilterTypeNone])
    {
        button = right ? self.fActiveFilterButton : self.fErrorFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeActive])
    {
        button = right ? self.fDownloadFilterButton : self.fNoFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeDownload])
    {
        button = right ? self.fSeedFilterButton : self.fActiveFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeSeed])
    {
        button = right ? self.fPauseFilterButton : self.fDownloadFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypePause])
    {
        button = right ? self.fErrorFilterButton : self.fSeedFilterButton;
    }
    else if ([filterType isEqualToString:FilterTypeError])
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

- (BOOL)isFocused
{
    NSTextView* textView = (NSTextView*)self.fSearchField.window.firstResponder;
    return [self.fSearchField.window.firstResponder isKindOfClass:NSTextView.class] &&
        [self.fSearchField.window fieldEditor:NO forObject:nil] != nil && [self.fSearchField isEqualTo:textView.delegate];
}

- (void)searchFieldDidStartSearching:(NSSearchField*)sender
{
    [self.fSearchFieldMinWidthConstraint animator].constant = 95;
}

- (void)searchFieldDidEndSearching:(NSSearchField*)sender
{
    [self.fSearchFieldMinWidthConstraint animator].constant = 48;
}

- (void)setSearchType:(id)sender
{
    NSString* oldFilterType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

    NSInteger prevTag, currentTag = [sender tag];
    if ([oldFilterType isEqualToString:FilterSearchTypeTracker])
    {
        prevTag = FilterTypeTagTracker;
    }
    else
    {
        prevTag = FilterTypeTagName;
    }

    if (currentTag != prevTag)
    {
        FilterSearchType filterType;
        if (currentTag == FilterTypeTagTracker)
        {
            filterType = FilterSearchTypeTracker;
        }
        else
        {
            filterType = FilterSearchTypeName;
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
    [NSUserDefaults.standardUserDefaults setInteger:kGroupFilterAllTag forKey:@"FilterGroup"];

    if (updateUI)
    {
        [self updateGroupsButton];

        [self setFilter:self.fNoFilterButton];

        self.fSearchField.stringValue = @"";
        [self setSearchText:self.fSearchField];
    }
    else
    {
        [NSUserDefaults.standardUserDefaults setObject:FilterTypeNone forKey:@"Filter"];
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
        //remove all items except first three
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
        if (menuItem.tag == FilterTypeTagTracker)
        {
            state = [filterType isEqualToString:FilterSearchTypeTracker];
        }
        else
        {
            state = [filterType isEqualToString:FilterSearchTypeName];
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

- (void)updateGroupsButton
{
    NSInteger const groupIndex = [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"];

    NSImage* icon;
    NSString* toolTip;
    if (groupIndex == kGroupFilterAllTag)
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
