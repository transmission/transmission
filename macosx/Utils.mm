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

NSString* const TRBindInterfaceModeLiteral = @"literal";
NSString* const TRBindInterfaceModeActiveVPN = @"active-vpn";
NSString* const TRBindInterfaceActiveVPNMenuValue = @"__transmission_active_vpn__";
NSString* const TRNoActiveVPNBindInterfaceName = @"tr-vpn-none";
NSString* const TRActiveVPNBindInterfaceDidChangeNotification = @"TRActiveVPNBindInterfaceDidChangeNotification";
NSString* const TRBindInterfaceServiceNameDefaultsKey = @"BindInterfaceServiceName";
NSString* const TRBindInterfaceProviderIdentifierDefaultsKey = @"BindInterfaceProviderIdentifier";

NSString* const TRActiveVPNResolutionInterfaceKey = @"interface";
NSString* const TRActiveVPNResolutionDisplayNameKey = @"displayName";
NSString* const TRActiveVPNResolutionServiceNameKey = @"serviceName";
NSString* const TRActiveVPNResolutionProviderIdentifierKey = @"providerIdentifier";
NSString* const TRActiveVPNResolutionRouteInterfaceKey = @"routeInterface";
NSString* const TRActiveVPNResolutionCandidatesKey = @"candidates";
NSString* const TRActiveVPNResolutionActiveKey = @"active";

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
BOOL TRInterfaceNameLooksLikeVPNTunnel(NSString* name)
{
    return [name hasPrefix:@"utun"] || [name hasPrefix:@"tun"] || [name hasPrefix:@"tap"] || [name hasPrefix:@"ppp"];
}

BOOL TRAddressIsUsableVPNAddress(NSString* address)
{
    return address.length > 0 && ![address hasPrefix:@"169.254."] && ![address hasPrefix:@"fe80:"] && ![address isEqualToString:@"::1"];
}

NSString* TRPrimaryRouteInterface(SCDynamicStoreRef store, CFStringRef key)
{
    NSDictionary* route = CFBridgingRelease(SCDynamicStoreCopyValue(store, key));
    NSString* interface = route[@"PrimaryInterface"];
    return interface.length > 0 ? interface : nil;
}

NSString* TRRouteInterfaceDescription(NSString* ipv4Interface, NSString* ipv6Interface)
{
    NSMutableArray<NSString*>* parts = [NSMutableArray arrayWithCapacity:2];
    if (ipv4Interface.length > 0)
    {
        [parts addObject:[NSString stringWithFormat:@"IPv4: %@", ipv4Interface]];
    }
    if (ipv6Interface.length > 0)
    {
        [parts addObject:[NSString stringWithFormat:@"IPv6: %@", ipv6Interface]];
    }
    return [parts componentsJoinedByString:@", "];
}

NSString* TRTunnelInterfaceFromConnectionStatus(NSDictionary* status)
{
    for (NSString* protocolKey in @[ @"IPv4", @"IPv6" ])
    {
        NSDictionary* protocolStatus = status[protocolKey];
        NSString* interfaceName = protocolStatus[@"InterfaceName"];
        if (TRInterfaceNameLooksLikeVPNTunnel(interfaceName))
        {
            return interfaceName;
        }
    }

    return nil;
}

NSDictionary<NSString*, NSString*>* TRUserDefinedNamesByInterfaceName()
{
    NSMutableDictionary<NSString*, NSString*>* names = [NSMutableDictionary dictionary];

    SCDynamicStoreRef store = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("Transmission"), nullptr, nullptr);
    if (store == nullptr)
    {
        return names;
    }

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
    return names;
}

NSString* TRVPNProviderIdentifierForService(SCNetworkServiceRef service)
{
    for (SCNetworkInterfaceRef interface = SCNetworkServiceGetInterface(service); interface != nullptr;
         interface = SCNetworkInterfaceGetInterface(interface))
    {
        NSString* type = CFBridgingRelease(
            SCNetworkInterfaceGetInterfaceType(interface) != nullptr ? CFRetain(SCNetworkInterfaceGetInterfaceType(interface)) : nullptr);
        if (type.length == 0 || [type isEqualToString:@"VPN"] || [type isEqualToString:@"IPv4"] || [type isEqualToString:@"IPv6"])
        {
            continue;
        }

        return type;
    }

    return nil;
}

NSDictionary<NSString*, NSDictionary<NSString*, NSString*>*>* TRVPNServiceInfoByInterfaceName()
{
    NSMutableDictionary<NSString*, NSDictionary<NSString*, NSString*>*>* serviceInfo = [NSMutableDictionary dictionary];
    NSDictionary<NSString*, NSString*>* userDefinedNames = TRUserDefinedNamesByInterfaceName();

    SCPreferencesRef prefs = SCPreferencesCreate(kCFAllocatorDefault, CFSTR("Transmission"), nullptr);
    if (prefs == nullptr)
    {
        return serviceInfo;
    }

    NSArray* services = CFBridgingRelease(SCNetworkServiceCopyAll(prefs));
    for (id object in services)
    {
        SCNetworkServiceRef service = (__bridge SCNetworkServiceRef)object;
        if (!SCNetworkServiceGetEnabled(service))
        {
            continue;
        }

        NSString* serviceID = CFBridgingRelease(
            SCNetworkServiceGetServiceID(service) != nullptr ? CFRetain(SCNetworkServiceGetServiceID(service)) : nullptr);
        if (serviceID.length == 0)
        {
            continue;
        }

        SCNetworkInterfaceRef interface = SCNetworkServiceGetInterface(service);
        NSString* interfaceType = interface != nullptr ?
            CFBridgingRelease(
                SCNetworkInterfaceGetInterfaceType(interface) != nullptr ? CFRetain(SCNetworkInterfaceGetInterfaceType(interface)) : nullptr) :
            nil;
        if (![interfaceType isEqualToString:@"VPN"])
        {
            continue;
        }

        SCNetworkConnectionRef connection = SCNetworkConnectionCreateWithServiceID(kCFAllocatorDefault, (__bridge CFStringRef)serviceID, nullptr, nullptr);
        if (connection == nullptr)
        {
            continue;
        }

        SCNetworkConnectionStatus const connectionStatus = SCNetworkConnectionGetStatus(connection);
        NSDictionary* extendedStatus = CFBridgingRelease(SCNetworkConnectionCopyExtendedStatus(connection));
        CFRelease(connection);

        if (connectionStatus != kSCNetworkConnectionConnected)
        {
            continue;
        }

        NSString* tunnelInterface = TRTunnelInterfaceFromConnectionStatus(extendedStatus);
        if (tunnelInterface.length == 0)
        {
            continue;
        }

        NSString* serviceName = CFBridgingRelease(
            SCNetworkServiceGetName(service) != nullptr ? CFRetain(SCNetworkServiceGetName(service)) : nullptr);
        NSString* providerIdentifier = TRVPNProviderIdentifierForService(service);
        NSString* userDefinedName = userDefinedNames[tunnelInterface];

        NSMutableDictionary<NSString*, NSString*>* info = [NSMutableDictionary dictionary];
        if (userDefinedName.length > 0)
        {
            info[TRActiveVPNResolutionServiceNameKey] = userDefinedName;
        }
        else if (serviceName.length > 0)
        {
            info[TRActiveVPNResolutionServiceNameKey] = serviceName;
        }
        if (providerIdentifier.length > 0)
        {
            info[TRActiveVPNResolutionProviderIdentifierKey] = providerIdentifier;
        }
        if (info.count > 0)
        {
            serviceInfo[tunnelInterface] = info;
        }
    }

    CFRelease(prefs);
    return serviceInfo;
}

BOOL TRVPNServiceInfoMatchesIdentity(
    NSDictionary<NSString*, NSString*>* serviceInfo,
    NSString* expectedServiceName,
    NSString* expectedProviderIdentifier)
{
    if (expectedServiceName.length > 0)
    {
        NSString* serviceName = serviceInfo[TRActiveVPNResolutionServiceNameKey];
        if (![serviceName isEqualToString:expectedServiceName])
        {
            return NO;
        }
    }

    if (expectedProviderIdentifier.length > 0)
    {
        NSString* providerIdentifier = serviceInfo[TRActiveVPNResolutionProviderIdentifierKey];
        if (![providerIdentifier isEqualToString:expectedProviderIdentifier])
        {
            return NO;
        }
    }

    return expectedServiceName.length > 0 || expectedProviderIdentifier.length > 0;
}

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

    [names addEntriesFromDictionary:TRUserDefinedNamesByInterfaceName()];

    NSDictionary<NSString*, NSDictionary<NSString*, NSString*>*>* vpnServices = TRVPNServiceInfoByInterfaceName();
    for (NSString* interfaceName in vpnServices)
    {
        NSString* serviceName = vpnServices[interfaceName][TRActiveVPNResolutionServiceNameKey];
        if (serviceName.length > 0)
        {
            names[interfaceName] = serviceName;
        }
    }

    return names;
}

NSString* TRNetworkInterfaceKind(NSString* name)
{
    if (TRInterfaceNameLooksLikeVPNTunnel(name))
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

NSDictionary<NSString*, id>* TRResolveActiveVPNInterfaceMatchingIdentity(NSString* expectedServiceName, NSString* expectedProviderIdentifier)
{
    expectedServiceName = expectedServiceName ?: @"";
    expectedProviderIdentifier = expectedProviderIdentifier ?: @"";

    SCDynamicStoreRef store = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("Transmission"), nullptr, nullptr);
    NSString* ipv4RouteInterface = nil;
    NSString* ipv6RouteInterface = nil;
    if (store != nullptr)
    {
        ipv4RouteInterface = TRPrimaryRouteInterface(store, CFSTR("State:/Network/Global/IPv4"));
        ipv6RouteInterface = TRPrimaryRouteInterface(store, CFSTR("State:/Network/Global/IPv6"));
        CFRelease(store);
    }

    NSMutableOrderedSet<NSString*>* candidates = [NSMutableOrderedSet orderedSet];
    NSMutableSet<NSString*>* runningTunnels = [NSMutableSet set];

    struct ifaddrs* ifaddrs = nullptr;
    if (getifaddrs(&ifaddrs) == 0)
    {
        for (struct ifaddrs const* ifa = ifaddrs; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_name == nullptr || ifa->ifa_addr == nullptr)
            {
                continue;
            }

            NSString* name = @(ifa->ifa_name);
            if (!TRInterfaceNameLooksLikeVPNTunnel(name))
            {
                continue;
            }

            unsigned int const flags = ifa->ifa_flags;
            if ((flags & IFF_UP) == 0 || (flags & IFF_RUNNING) == 0 || (flags & IFF_LOOPBACK) != 0)
            {
                continue;
            }

            [runningTunnels addObject:name];

            sa_family_t const family = ifa->ifa_addr->sa_family;
            if (family != AF_INET && family != AF_INET6)
            {
                continue;
            }

            char host[NI_MAXHOST] = {};
            if (getnameinfo(ifa->ifa_addr, family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), host, sizeof(host), nullptr, 0, NI_NUMERICHOST) != 0)
            {
                continue;
            }

            if (TRAddressIsUsableVPNAddress(@(host)))
            {
                [candidates addObject:name];
            }
        }

        freeifaddrs(ifaddrs);
    }

    for (NSString* routeInterface in @[ ipv4RouteInterface ?: @"", ipv6RouteInterface ?: @"" ])
    {
        if (TRInterfaceNameLooksLikeVPNTunnel(routeInterface) &&
            ([runningTunnels containsObject:routeInterface] || runningTunnels.count == 0))
        {
            [candidates addObject:routeInterface];
        }
    }

    NSArray<NSString*>* sortedCandidates = [[candidates array] sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
    NSDictionary<NSString*, NSDictionary<NSString*, NSString*>*>* vpnServices = TRVPNServiceInfoByInterfaceName();

    NSString* preferredInterface = nil;
    NSMutableArray<NSString*>* identityMatches = [NSMutableArray array];
    if (expectedServiceName.length > 0 || expectedProviderIdentifier.length > 0)
    {
        for (NSString* candidate in sortedCandidates)
        {
            if (TRVPNServiceInfoMatchesIdentity(vpnServices[candidate], expectedServiceName, expectedProviderIdentifier))
            {
                [identityMatches addObject:candidate];
            }
        }

        for (NSString* routeInterface in @[ ipv4RouteInterface ?: @"", ipv6RouteInterface ?: @"" ])
        {
            if ([identityMatches containsObject:routeInterface])
            {
                preferredInterface = routeInterface;
                break;
            }
        }
        if (preferredInterface == nil && identityMatches.count == 1)
        {
            preferredInterface = identityMatches.firstObject;
        }
    }
    else
    {
        for (NSString* routeInterface in @[ ipv4RouteInterface ?: @"", ipv6RouteInterface ?: @"" ])
        {
            if ([sortedCandidates containsObject:routeInterface])
            {
                preferredInterface = routeInterface;
                break;
            }
        }
        if (preferredInterface == nil && sortedCandidates.count == 1)
        {
            preferredInterface = sortedCandidates.firstObject;
        }
    }

    NSDictionary<NSString*, NSString*>* preferredVPNService = preferredInterface.length > 0 ? vpnServices[preferredInterface] : nil;
    NSString* serviceName = preferredVPNService[TRActiveVPNResolutionServiceNameKey];
    NSString* providerIdentifier = preferredVPNService[TRActiveVPNResolutionProviderIdentifierKey];
    NSString* displayName = serviceName.length > 0 ? [NSString stringWithFormat:@"%@ (%@)", serviceName, preferredInterface] : preferredInterface;

    NSString* routeDescription = TRRouteInterfaceDescription(ipv4RouteInterface, ipv6RouteInterface);
    return @{
        TRActiveVPNResolutionInterfaceKey : preferredInterface ?: @"",
        TRActiveVPNResolutionDisplayNameKey : displayName ?: @"",
        TRActiveVPNResolutionServiceNameKey : serviceName ?: @"",
        TRActiveVPNResolutionProviderIdentifierKey : providerIdentifier ?: @"",
        TRActiveVPNResolutionRouteInterfaceKey : routeDescription ?: @"",
        TRActiveVPNResolutionCandidatesKey : sortedCandidates ?: @[],
        TRActiveVPNResolutionActiveKey : @(preferredInterface.length > 0),
    };
}

NSDictionary<NSString*, id>* TRResolveActiveVPNInterface(void)
{
    return TRResolveActiveVPNInterfaceMatchingIdentity(@"", @"");
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

void TRPopulateAppBindInterfacePopUp(NSPopUpButton* popUp, NSString* selectedInterface, NSString* selectedMode)
{
    TRPopulateBindInterfacePopUp(popUp, selectedInterface, NO, @"");

    NSMenuItem* activeVPNItem = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Active VPN Tunnel", "Network interface binding menu item")
                                                           action:nil
                                                    keyEquivalent:@""];
    activeVPNItem.representedObject = TRBindInterfaceActiveVPNMenuValue;
    activeVPNItem.toolTip = NSLocalizedString(
        @"Follow the active macOS VPN tunnel. Traffic is blocked when no active VPN tunnel can be resolved.",
        "Network interface binding menu tooltip");
    [popUp.menu insertItem:activeVPNItem atIndex:MIN(1, popUp.numberOfItems)];

    if ([selectedMode isEqualToString:TRBindInterfaceModeActiveVPN])
    {
        [popUp selectItem:activeVPNItem];
    }
}

NSString* TRBindInterfacePopUpValue(NSPopUpButton* popUp)
{
    id value = popUp.selectedItem.representedObject;
    return [value isKindOfClass:NSString.class] ? value : @"";
}

NSString* TRBindInterfacePopUpModeValue(NSPopUpButton* popUp)
{
    return [TRBindInterfacePopUpValue(popUp) isEqualToString:TRBindInterfaceActiveVPNMenuValue] ? TRBindInterfaceModeActiveVPN :
                                                                                                  TRBindInterfaceModeLiteral;
}
