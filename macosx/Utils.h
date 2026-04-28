// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class NSPopUpButton;

// 0.1 precision
bool isSpeedEqual(CGFloat old_speed, CGFloat new_speed);
// 0.01 precision
bool isRatioEqual(CGFloat old_ratio, CGFloat new_ratio);

NSArray<NSString*>* TRActiveNetworkInterfaceNames(void);
void TRPopulateBindInterfacePopUp(NSPopUpButton* popUp, NSString* selectedInterface, BOOL includeInherit, NSString* defaultRouteValue);
NSString* TRBindInterfacePopUpValue(NSPopUpButton* popUp);
