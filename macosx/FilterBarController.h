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

#import <Cocoa/Cocoa.h>

@class FilterButton;

#define FILTER_NONE     @"None"
#define FILTER_ACTIVE   @"Active"
#define FILTER_DOWNLOAD @"Download"
#define FILTER_SEED     @"Seed"
#define FILTER_PAUSE    @"Pause"

#define FILTER_TYPE_NAME    @"Name"
#define FILTER_TYPE_TRACKER @"Tracker"

#define GROUP_FILTER_ALL_TAG -2

@interface FilterBarController : NSViewController
{
    IBOutlet FilterButton * fNoFilterButton, * fActiveFilterButton, * fDownloadFilterButton,
                            * fSeedFilterButton, * fPauseFilterButton;

    IBOutlet NSSearchField * fSearchField;

    IBOutlet NSPopUpButton * fGroupsButton;
}

- (id) init;

- (void) setFilter: (id) sender;
- (void) switchFilter: (BOOL) right;
- (void) setSearchText: (id) sender;
- (void) setSearchType: (id) sender;
- (void) setGroupFilter: (id) sender;
- (void) reset: (BOOL) updateUI;

- (NSArray *) searchStrings;
- (void) focusSearchField;

- (void) setCountAll: (NSUInteger) all active: (NSUInteger) active downloading: (NSUInteger) downloading
        seeding: (NSUInteger) seeding paused: (NSUInteger) paused;


@end
