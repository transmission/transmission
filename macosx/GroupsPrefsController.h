// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@interface GroupsPrefsController : NSObject

- (IBAction)toggleUseAutoAssignRules:(id)sender;
- (IBAction)orderFrontRulesSheet:(id)sender;
- (IBAction)cancelRules:(id)sender;
- (IBAction)saveRules:(id)sender;

@end
