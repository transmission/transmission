// This file Copyright © 2011-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#define FILTER_NONE @"None"
#define FILTER_ACTIVE @"Active"
#define FILTER_DOWNLOAD @"Download"
#define FILTER_SEED @"Seed"
#define FILTER_PAUSE @"Pause"
#define FILTER_ERROR @"Error"

#define FILTER_TYPE_NAME @"Name"
#define FILTER_TYPE_TRACKER @"Tracker"

#define GROUP_FILTER_ALL_TAG -2

@interface FilterBarController : NSViewController

@property(nonatomic, readonly) NSArray<NSString*>* searchStrings;

- (instancetype)init;

- (void)setFilter:(id)sender;
- (void)switchFilter:(BOOL)right;
- (void)setSearchText:(id)sender;
- (void)setSearchType:(id)sender;
- (void)setGroupFilter:(id)sender;
- (void)reset:(BOOL)updateUI;
- (void)focusSearchField;

- (void)setCountAll:(NSUInteger)all
             active:(NSUInteger)active
        downloading:(NSUInteger)downloading
            seeding:(NSUInteger)seeding
             paused:(NSUInteger)paused
              error:(NSUInteger)error;

@end
