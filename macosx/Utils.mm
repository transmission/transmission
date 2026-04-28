// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cmath>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#import <AppKit/AppKit.h>
#import <SystemConfiguration/SystemConfiguration.h>

#import "Utils.h"

bool isSpeedEqual(CGFloat old_speed, CGFloat new_speed)
{
    static CGFloat constexpr kSpeedCompareEps = 0.1 / 2;
    return std::abs(new_speed - old_speed) < kSpeedCompareEps;
}

bool isRatioEqual(CGFloat old_ratio, CGFloat new_ratio)
{
    static CGFloat constexpr kRatioCompareEps = 0.01 / 2;
    return std::abs(new_ratio - old_ratio) < kRatioCompareEps;
}

namespace
{
NSDictionary<NSString*, NSString*>* TRNetworkServiceNamesByInterfaceName()
{
    NSMutableDictionary<NSString*, NSString*>* names = [NSMutableDictionary dictionary];

    SCPreferencesRef prefs = SCPreferencesCreate(kCFAllocatorDefault, CFSTR("Transmission"), nullptr);
    if (prefs != nullptr)
    {
        NSArray* services = CFBridgingRelease(SCNetworkServiceCopyAll(prefs));
        for (id object in services)
        {
            SCNetworkServiceRef service = (__bridge SCNetworkServiceRef)object;
            if (!SCNetworkServiceGetEnabled(service))
            {
                continue;
            }

            NSString* serviceName = CFBridgingRelease(
                SCNetworkServiceGetName(service) != nullptr ? CFRetain(SCNetworkServiceGetName(service)) : nullptr);
            if (serviceName.length == 0)
            {
                continue;
            }

            for (SCNetworkInterfaceRef interface = SCNetworkServiceGetInterface(service); interface != nullptr;
                 interface = SCNetworkInterfaceGetInterface(interface))
            {
                NSString* bsdName = CFBridgingRelease(
                    SCNetworkInterfaceGetBSDName(interface) != nullptr ? CFRetain(SCNetworkInterfaceGetBSDName(interface)) : nullptr);
                if (bsdName.length > 0)
                {
                    names[bsdName] = serviceName;
                }
            }
        }

        CFRelease(prefs);
    }

    SCDynamicStoreRef store = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("Transmission"), nullptr, nullptr);
    if (store != nullptr)
    {
        NSArray* keys = CFBridgingRelease(SCDynamicStoreCopyKeyList(store, CFSTR("State:/Network/Service/.*/Interface")));
        for (NSString* key in keys)
        {
            NSDictionary* interfaceInfo = CFBridgingRelease(SCDynamicStoreCopyValue(store, (__bridge CFStringRef)key));
            NSString* bsdName = interfaceInfo[@"DeviceName"] ?: interfaceInfo[@"InterfaceName"];
            NSString* userDefinedName = interfaceInfo[@"UserDefinedName"];

            if (bsdName.length > 0 && userDefinedName.length > 0)
            {
                names[bsdName] = userDefinedName;
            }
        }

        CFRelease(store);
    }

    return names;
}

NSString* TRNetworkInterfaceKind(NSString* name)
{
    if ([name hasPrefix:@"utun"] || [name hasPrefix:@"tun"] || [name hasPrefix:@"tap"] || [name hasPrefix:@"ppp"])
    {
        return NSLocalizedString(@"VPN Tunnel", "Network interface type");
    }

    if ([name hasPrefix:@"en"])
    {
        return NSLocalizedString(@"Network Adapter", "Network interface type");
    }

    if ([name hasPrefix:@"bridge"])
    {
        return NSLocalizedString(@"Bridge", "Network interface type");
    }

    if ([name hasPrefix:@"awdl"] || [name hasPrefix:@"llw"])
    {
        return NSLocalizedString(@"Apple Wireless Direct Link", "Network interface type");
    }

    return NSLocalizedString(@"Network Interface", "Network interface type");
}

NSArray<NSDictionary<NSString*, id>*>* TRActiveNetworkInterfaceInfo()
{
    struct ifaddrs* ifaddrs = nullptr;
    if (getifaddrs(&ifaddrs) != 0)
    {
        return @[];
    }

    NSMutableDictionary<NSString*, NSMutableSet<NSString*>*>* addresses = [NSMutableDictionary dictionary];
    NSMutableOrderedSet<NSString*>* names = [NSMutableOrderedSet orderedSet];
    for (struct ifaddrs const* ifa = ifaddrs; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_name == nullptr || ifa->ifa_addr == nullptr)
        {
            continue;
        }

        unsigned int const flags = ifa->ifa_flags;
        if ((flags & IFF_UP) == 0 || (flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }

        sa_family_t const family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6 && family != AF_LINK)
        {
            continue;
        }

        NSString* name = @(ifa->ifa_name);
        [names addObject:name];

        if (family == AF_INET || family == AF_INET6)
        {
            char host[NI_MAXHOST] = {};
            if (getnameinfo(ifa->ifa_addr, family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0)
            {
                NSString* address = @(host);
                if (![address hasPrefix:@"fe80:"])
                {
                    if (addresses[name] == nil)
                    {
                        addresses[name] = [NSMutableSet set];
                    }
                    [addresses[name] addObject:address];
                }
            }
        }
    }

    freeifaddrs(ifaddrs);

    NSDictionary<NSString*, NSString*>* serviceNames = TRNetworkServiceNamesByInterfaceName();
    NSMutableArray<NSDictionary<NSString*, id>*>* interfaces = [NSMutableArray array];

    for (NSString* name in names)
    {
        NSString* serviceName = serviceNames[name];
        NSString* kind = TRNetworkInterfaceKind(name);
        NSString* title = nil;

        if (serviceName.length > 0 && ![serviceName isEqualToString:name])
        {
            title = [NSString stringWithFormat:@"%@ — %@ (%@)", serviceName, kind, name];
        }
        else
        {
            title = [NSString stringWithFormat:@"%@ (%@)", kind, name];
        }

        NSArray<NSString*>* sortedAddresses = [[addresses[name] allObjects] sortedArrayUsingSelector:@selector(localizedStandardCompare:)] ?:
            @[];
        NSString* detail = sortedAddresses.count > 0 ?
            [NSString stringWithFormat:@"%@\n%@", name, [sortedAddresses componentsJoinedByString:@"\n"]] :
            name;

        NSUInteger rank = 20;
        if ([kind isEqualToString:NSLocalizedString(@"VPN Tunnel", "Network interface type")])
        {
            rank = 0;
        }
        else if (serviceName.length > 0)
        {
            rank = 10;
        }

        [interfaces addObject:@{
            @"name" : name,
            @"title" : title,
            @"detail" : detail,
            @"rank" : @(rank),
        }];
    }

    [interfaces sortUsingComparator:^NSComparisonResult(NSDictionary<NSString*, id>* lhs, NSDictionary<NSString*, id>* rhs) {
        NSInteger leftRank = [lhs[@"rank"] integerValue];
        NSInteger rightRank = [rhs[@"rank"] integerValue];
        if (leftRank < rightRank)
        {
            return NSOrderedAscending;
        }
        if (leftRank > rightRank)
        {
            return NSOrderedDescending;
        }
        return [lhs[@"title"] localizedStandardCompare:rhs[@"title"]];
    }];

    return interfaces;
}
} // namespace

NSArray<NSString*>* TRActiveNetworkInterfaceNames(void)
{
    NSMutableArray<NSString*>* names = [NSMutableArray array];
    for (NSDictionary<NSString*, id>* interfaceInfo in TRActiveNetworkInterfaceInfo())
    {
        [names addObject:interfaceInfo[@"name"]];
    }
    return names;
}

void TRPopulateBindInterfacePopUp(NSPopUpButton* popUp, NSString* selectedInterface, BOOL includeInherit, NSString* defaultRouteValue)
{
    selectedInterface = selectedInterface ?: @"";
    defaultRouteValue = defaultRouteValue ?: @"";

    [popUp removeAllItems];

    if (includeInherit)
    {
        [popUp addItemWithTitle:NSLocalizedString(@"Use App Default", "Network interface binding menu item")];
        popUp.lastItem.representedObject = @"";
        popUp.lastItem.toolTip = NSLocalizedString(@"Use the Network preference setting.", "Network interface binding menu tooltip");
    }

    [popUp addItemWithTitle:NSLocalizedString(@"Automatic (Default Connection)", "Network interface binding menu item")];
    popUp.lastItem.representedObject = defaultRouteValue;
    popUp.lastItem.toolTip = NSLocalizedString(@"Let macOS choose the connection for each transfer.", "Network interface binding menu tooltip");

    NSArray<NSDictionary<NSString*, id>*>* activeInterfaces = TRActiveNetworkInterfaceInfo();
    NSMutableArray<NSString*>* activeNames = [NSMutableArray array];
    for (NSDictionary<NSString*, id>* interfaceInfo in activeInterfaces)
    {
        NSString* name = interfaceInfo[@"name"];
        [activeNames addObject:name];

        [popUp addItemWithTitle:interfaceInfo[@"title"]];
        popUp.lastItem.representedObject = name;
        popUp.lastItem.toolTip = interfaceInfo[@"detail"];
    }

    BOOL selectedIsInherit = selectedInterface.length == 0 && includeInherit;
    BOOL selectedIsDefaultRoute = [selectedInterface isEqualToString:defaultRouteValue] || (selectedInterface.length == 0 && !includeInherit);
    BOOL selectedActiveInterface = selectedInterface.length > 0 && !selectedIsDefaultRoute && [activeNames containsObject:selectedInterface];

    if (selectedInterface.length > 0 && !selectedIsDefaultRoute && !selectedActiveInterface)
    {
        NSString* title = [NSString
            stringWithFormat:NSLocalizedString(@"Unavailable Interface (%@)", "Network interface binding menu item"), selectedInterface];
        [popUp addItemWithTitle:title];
        popUp.lastItem.representedObject = selectedInterface;
        popUp.lastItem.toolTip = NSLocalizedString(
            @"This saved connection is not currently available. Traffic will fail closed while it is selected.",
            "Network interface binding menu tooltip");
    }

    NSString* selectedValue = selectedIsInherit ? @"" : (selectedIsDefaultRoute ? defaultRouteValue : selectedInterface);
    for (NSMenuItem* item in popUp.itemArray)
    {
        if ([item.representedObject isKindOfClass:NSString.class] && [item.representedObject isEqualToString:selectedValue])
        {
            [popUp selectItem:item];
            return;
        }
    }

    [popUp selectItemAtIndex:0];
}

NSString* TRBindInterfacePopUpValue(NSPopUpButton* popUp)
{
    id value = popUp.selectedItem.representedObject;
    return [value isKindOfClass:NSString.class] ? value : @"";
}
