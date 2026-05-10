// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class NSPopUpButton;

extern NSString* const TRBindInterfaceModeLiteral;
extern NSString* const TRBindInterfaceModeActiveVPN;
extern NSString* const TRBindInterfaceActiveVPNMenuValue;
extern NSString* const TRNoActiveVPNBindInterfaceName;
extern NSString* const TRActiveVPNBindInterfaceDidChangeNotification;

extern NSString* const TRActiveVPNResolutionInterfaceKey;
extern NSString* const TRActiveVPNResolutionDisplayNameKey;
extern NSString* const TRActiveVPNResolutionServiceNameKey;
extern NSString* const TRActiveVPNResolutionProviderIdentifierKey;
extern NSString* const TRActiveVPNResolutionRouteInterfaceKey;
extern NSString* const TRActiveVPNResolutionCandidatesKey;
extern NSString* const TRActiveVPNResolutionActiveKey;

// 0.1 precision
bool isSpeedEqual(CGFloat old_speed, CGFloat new_speed);
// 0.01 precision
bool isRatioEqual(CGFloat old_ratio, CGFloat new_ratio);

NSArray<NSString*>* TRActiveNetworkInterfaceNames(void);
NSDictionary<NSString*, id>* TRResolveActiveVPNInterface(void);
void TRPopulateBindInterfacePopUp(NSPopUpButton* popUp, NSString* selectedInterface, BOOL includeInherit, NSString* defaultRouteValue);
void TRPopulateAppBindInterfacePopUp(NSPopUpButton* popUp, NSString* selectedInterface, NSString* selectedMode);
NSString* TRBindInterfacePopUpValue(NSPopUpButton* popUp);
NSString* TRBindInterfacePopUpModeValue(NSPopUpButton* popUp);
