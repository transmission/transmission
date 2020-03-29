/******************************************************************************
 * Copyright (c) 2010-2012 Transmission authors and contributors
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

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "InfoPeersViewController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "PeerProgressIndicatorCell.h"
#import "Torrent.h"
#import "WebSeedTableView.h"

#define ANIMATION_ID_KEY @"animationId"
#define WEB_SEED_ANIMATION_ID @"webSeed"

@interface InfoPeersViewController (Private)

- (void) setupInfo;

- (void) setWebSeedTableHidden: (BOOL) hide animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

@end

@implementation InfoPeersViewController

- (id) init
{
    if ((self = [super initWithNibName: @"InfoPeersView" bundle: nil]))
    {
        [self setTitle: NSLocalizedString(@"Peers", "Inspector view -> title")];
    }

    return self;
}

- (void) awakeFromNib
{
    const CGFloat height = [[NSUserDefaults standardUserDefaults] floatForKey: @"InspectorContentHeightPeers"];
    if (height != 0.0)
    {
        NSRect viewRect = [[self view] frame];
        viewRect.size.height = height;
        [[self view] setFrame: viewRect];
    }

    //set table header text
    [[[fPeerTable tableColumnWithIdentifier: @"IP"] headerCell] setStringValue: NSLocalizedString(@"IP Address",
                                                                        "inspector -> peer table -> header")];
    [[[fPeerTable tableColumnWithIdentifier: @"Client"] headerCell] setStringValue: NSLocalizedString(@"Client",
                                                                        "inspector -> peer table -> header")];
    [[[fPeerTable tableColumnWithIdentifier: @"DL From"] headerCell] setStringValue: NSLocalizedString(@"DL",
                                                                        "inspector -> peer table -> header")];
    [[[fPeerTable tableColumnWithIdentifier: @"UL To"] headerCell] setStringValue: NSLocalizedString(@"UL",
                                                                        "inspector -> peer table -> header")];

    [[[fWebSeedTable tableColumnWithIdentifier: @"Address"] headerCell] setStringValue: NSLocalizedString(@"Web Seeds",
                                                                        "inspector -> web seed table -> header")];
    [[[fWebSeedTable tableColumnWithIdentifier: @"DL From"] headerCell] setStringValue: NSLocalizedString(@"DL",
                                                                        "inspector -> web seed table -> header")];

    //set table header tool tips
    [[fPeerTable tableColumnWithIdentifier: @"Encryption"] setHeaderToolTip: NSLocalizedString(@"Encrypted Connection",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"Progress"] setHeaderToolTip: NSLocalizedString(@"Available",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Peer",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"UL To"] setHeaderToolTip: NSLocalizedString(@"Uploading To Peer",
                                                                        "inspector -> peer table -> header tool tip")];

    [[fWebSeedTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Web Seed",
                                                                        "inspector -> web seed table -> header tool tip")];

    //prepare for animating peer table and web seed table
    fViewTopMargin = fWebSeedTableTopConstraint.constant;

    CABasicAnimation * webSeedTableAnimation = [CABasicAnimation animation];
    [webSeedTableAnimation setTimingFunction: [CAMediaTimingFunction functionWithName: kCAMediaTimingFunctionLinear]];
    [webSeedTableAnimation setDuration: 0.125];
    [webSeedTableAnimation setDelegate: self];
    [webSeedTableAnimation setValue: WEB_SEED_ANIMATION_ID forKey: ANIMATION_ID_KEY];
    [fWebSeedTableTopConstraint setAnimations: @{ @"constant": webSeedTableAnimation }];

    [self setWebSeedTableHidden: YES animate: NO];
}


#warning subclass?
- (void) setInfoForTorrents: (NSArray *) torrents
{
    //don't check if it's the same in case the metadata changed
    fTorrents = torrents;

    fSet = NO;
}

- (void) updateInfo
{
    if (!fSet)
        [self setupInfo];

    if ([fTorrents count] == 0)
        return;

    if (!fPeers)
        fPeers = [[NSMutableArray alloc] init];
    else
        [fPeers removeAllObjects];

    if (!fWebSeeds)
        fWebSeeds = [[NSMutableArray alloc] init];
    else
        [fWebSeeds removeAllObjects];

    NSUInteger connected = 0, tracker = 0, incoming = 0, cache = 0, lpd = 0, pex = 0, dht = 0, ltep = 0,
                toUs = 0, fromUs = 0;
    BOOL anyActive = false;
    for (Torrent * torrent in fTorrents)
    {
        if ([torrent webSeedCount] > 0)
            [fWebSeeds addObjectsFromArray: [torrent webSeeds]];

        if ([torrent isActive])
        {
            anyActive = YES;
            [fPeers addObjectsFromArray: [torrent peers]];

            const NSUInteger connectedThis = [torrent totalPeersConnected];
            if (connectedThis > 0)
            {
                connected += [torrent totalPeersConnected];
                tracker += [torrent totalPeersTracker];
                incoming += [torrent totalPeersIncoming];
                cache += [torrent totalPeersCache];
                lpd += [torrent totalPeersLocal];
                pex += [torrent totalPeersPex];
                dht += [torrent totalPeersDHT];
                ltep += [torrent totalPeersLTEP];

                toUs += [torrent peersSendingToUs];
                fromUs += [torrent peersGettingFromUs];
            }
        }
    }

    [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
    [fPeerTable reloadData];

    [fWebSeeds sortUsingDescriptors: [fWebSeedTable sortDescriptors]];
    [fWebSeedTable reloadData];
    [fWebSeedTable setWebSeeds: fWebSeeds];

    if (anyActive)
    {
        NSString * connectedText = [NSString stringWithFormat: NSLocalizedString(@"%d Connected", "Inspector -> Peers tab -> peers"),
                                    connected];

        if (connected > 0)
        {
            NSMutableArray * upDownComponents = [NSMutableArray arrayWithCapacity: 2];
            if (toUs > 0)
                [upDownComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"DL from %d", "Inspector -> Peers tab -> peers"), toUs]];
            if (fromUs > 0)
                [upDownComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"UL to %d", "Inspector -> Peers tab -> peers"), fromUs]];
            if ([upDownComponents count] > 0)
                connectedText = [connectedText stringByAppendingFormat: @": %@", [upDownComponents componentsJoinedByString: @", "]];

            NSMutableArray * fromComponents = [NSMutableArray arrayWithCapacity: 7];
            if (tracker > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d tracker", "Inspector -> Peers tab -> peers"), tracker]];
            if (incoming > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d incoming", "Inspector -> Peers tab -> peers"), incoming]];
            if (cache > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d cache", "Inspector -> Peers tab -> peers"), cache]];
            if (lpd > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d local discovery", "Inspector -> Peers tab -> peers"), lpd]];
            if (pex > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d PEX", "Inspector -> Peers tab -> peers"), pex]];
            if (dht > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d DHT", "Inspector -> Peers tab -> peers"), dht]];
            if (ltep > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d LTEP", "Inspector -> Peers tab -> peers"), ltep]];

            connectedText = [connectedText stringByAppendingFormat: @"\n%@", [fromComponents componentsJoinedByString: @", "]];
        }

        [fConnectedPeersField setStringValue: connectedText];
    }
    else
    {
        NSString * notActiveString;
        if ([fTorrents count] == 1)
            notActiveString = NSLocalizedString(@"Transfer Not Active", "Inspector -> Peers tab -> peers");
        else
            notActiveString = NSLocalizedString(@"Transfers Not Active", "Inspector -> Peers tab -> peers");

        [fConnectedPeersField setStringValue: notActiveString];
    }
}

- (void) saveViewSize
{
    [[NSUserDefaults standardUserDefaults] setFloat: NSHeight([[self view] frame]) forKey: @"InspectorContentHeightPeers"];
}

- (void) clearView
{
    fPeers = nil;
    fWebSeeds = nil;
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    if (tableView == fWebSeedTable)
        return fWebSeeds ? [fWebSeeds count] : 0;
    else
        return fPeers ? [fPeers count] : 0;
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (NSInteger) row
{
    if (tableView == fWebSeedTable)
    {
        NSString * ident = [column identifier];
        NSDictionary * webSeed = fWebSeeds[row];

        if ([ident isEqualToString: @"DL From"])
        {
            NSNumber * rate;
            return (rate = webSeed[@"DL From Rate"]) ? [NSString stringForSpeedAbbrev: [rate doubleValue]] : @"";
        }
        else
            return webSeed[@"Address"];
    }
    else
    {
        NSString * ident = [column identifier];
        NSDictionary * peer = fPeers[row];

        if ([ident isEqualToString: @"Encryption"])
            return [peer[@"Encryption"] boolValue] ? [NSImage imageNamed: @"Lock"] : nil;
        else if ([ident isEqualToString: @"Client"])
            return peer[@"Client"];
        else if  ([ident isEqualToString: @"Progress"])
            return peer[@"Progress"];
        else if ([ident isEqualToString: @"UL To"])
        {
            NSNumber * rate;
            return (rate = peer[@"UL To Rate"]) ? [NSString stringForSpeedAbbrev: [rate doubleValue]] : @"";
        }
        else if ([ident isEqualToString: @"DL From"])
        {
            NSNumber * rate;
            return (rate = peer[@"DL From Rate"]) ? [NSString stringForSpeedAbbrev: [rate doubleValue]] : @"";
        }
        else
            return peer[@"IP"];
    }
}

- (void) tableView: (NSTableView *) tableView willDisplayCell: (id) cell forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    if (tableView == fPeerTable)
    {
        NSString * ident = [tableColumn identifier];

        if  ([ident isEqualToString: @"Progress"])
        {
            NSDictionary * peer = fPeers[row];
            [(PeerProgressIndicatorCell *)cell setSeed: [peer[@"Seed"] boolValue]];
        }
    }
}

- (void) tableView: (NSTableView *) tableView didClickTableColumn: (NSTableColumn *) tableColumn
{
    if (tableView == fWebSeedTable)
    {
        if (fWebSeeds)
        {
            [fWebSeeds sortUsingDescriptors: [fWebSeedTable sortDescriptors]];
            [tableView reloadData];
        }
    }
    else
    {
        if (fPeers)
        {
            [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
            [tableView reloadData];
        }
    }
}

- (BOOL) tableView: (NSTableView *) tableView shouldSelectRow: (NSInteger) row
{
    return tableView != fPeerTable;
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (NSInteger) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fPeerTable)
    {
        const BOOL multiple = [fTorrents count] > 1;

        NSDictionary * peer = fPeers[row];
        NSMutableArray * components = [NSMutableArray arrayWithCapacity: multiple ? 6 : 5];

        if (multiple)
            [components addObject: peer[@"Name"]];

        const CGFloat progress = [peer[@"Progress"] floatValue];
        NSString * progressString = [NSString stringWithFormat: NSLocalizedString(@"Progress: %@",
                                        "Inspector -> Peers tab -> table row tooltip"),
                                        [NSString percentString: progress longDecimals: NO]];
        if (progress < 1.0 && [peer[@"Seed"] boolValue])
            progressString = [progressString stringByAppendingFormat: @" (%@)", NSLocalizedString(@"Partial Seed",
                                "Inspector -> Peers tab -> table row tooltip")];
        [components addObject: progressString];

        NSString * protocolString = [peer[@"uTP"] boolValue] ? @"\u00b5TP" : @"TCP";
        if ([peer[@"Encryption"] boolValue])
            protocolString = [protocolString stringByAppendingFormat: @" (%@)",
                                NSLocalizedString(@"encrypted", "Inspector -> Peers tab -> table row tooltip")];
        [components addObject: [NSString stringWithFormat:
                                NSLocalizedString(@"Protocol: %@", "Inspector -> Peers tab -> table row tooltip"),
                                protocolString]];

        NSString * portString;
        NSInteger port;
        if ((port = [peer[@"Port"] intValue]) > 0)
            portString = [NSString stringWithFormat: @"%ld", port];
        else
            portString = NSLocalizedString(@"N/A", "Inspector -> Peers tab -> table row tooltip");
        [components addObject: [NSString stringWithFormat: @"%@: %@", NSLocalizedString(@"Port",
            "Inspector -> Peers tab -> table row tooltip"), portString]];

        const NSInteger peerFrom = [peer[@"From"] integerValue];
        switch (peerFrom)
        {
            case TR_PEER_FROM_TRACKER:
                [components addObject: NSLocalizedString(@"From: tracker", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_INCOMING:
                [components addObject: NSLocalizedString(@"From: incoming connection", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_RESUME:
                [components addObject: NSLocalizedString(@"From: cache", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_LPD:
                [components addObject: NSLocalizedString(@"From: local peer discovery", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_PEX:
                [components addObject: NSLocalizedString(@"From: peer exchange", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_DHT:
                [components addObject: NSLocalizedString(@"From: distributed hash table", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_LTEP:
                [components addObject: NSLocalizedString(@"From: libtorrent extension protocol handshake",
                                        "Inspector -> Peers tab -> table row tooltip")];
                break;
            default:
                NSAssert1(NO, @"Peer from unknown source: %ld", peerFrom);
        }

        //determing status strings from flags
        NSMutableArray * statusArray = [NSMutableArray arrayWithCapacity: 6];
        NSString * flags = peer[@"Flags"];

        if ([flags rangeOfString: @"D"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently downloading (interested and not choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"d"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You want to download, but peer does not want to send (interested and choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"U"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently uploading (interested and not choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"u"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer wants you to upload, but you do not want to (interested and choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"K"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer is unchoking you, but you are not interested",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"?"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You unchoked the peer, but the peer is not interested",
                "Inspector -> peer -> status")];

        if ([statusArray count] > 0)
        {
            NSString * statusStrings = [statusArray componentsJoinedByString: @"\n\n"];
            [components addObject: [@"\n" stringByAppendingString: statusStrings]];
        }

        return [components componentsJoinedByString: @"\n"];
    }
    else
    {
        if ([fTorrents count] > 1)
            return fWebSeeds[row][@"Name"];
    }

    return nil;
}

- (void) animationDidStart: (CAAnimation *) animation
{
    if (![[animation valueForKey: ANIMATION_ID_KEY] isEqualToString: WEB_SEED_ANIMATION_ID])
        return;

    [[fWebSeedTable enclosingScrollView] setHidden: NO];
}

- (void) animationDidStop: (CAAnimation *) animation finished: (BOOL) finished
{
    if (![[animation valueForKey: ANIMATION_ID_KEY] isEqualToString: WEB_SEED_ANIMATION_ID])
        return;

    [[fWebSeedTable enclosingScrollView] setHidden: finished && fWebSeedTableTopConstraint.constant < 0];
}

@end

@implementation InfoPeersViewController (Private)

- (void) setupInfo
{
    __block BOOL hasWebSeeds = NO;

    if ([fTorrents count] == 0)
    {
        fPeers = nil;
        [fPeerTable reloadData];

        [fConnectedPeersField setStringValue: @""];
    }
    else
    {
        [fTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(Torrent * torrent, NSUInteger idx, BOOL *stop) {
            if ([torrent webSeedCount] > 0)
            {
                hasWebSeeds = YES;
                *stop = YES;
            }
        }];
    }

    if (!hasWebSeeds)
    {
        fWebSeeds = nil;
        [fWebSeedTable reloadData];
    }
    else
        [fWebSeedTable deselectAll: self];
    [self setWebSeedTableHidden: !hasWebSeeds animate: YES];

    fSet = YES;
}

- (void) setWebSeedTableHidden: (BOOL) hide animate: (BOOL) animate
{
    if (animate && (![[self view] window] || ![[[self view] window] isVisible]))
        animate = NO;

    const CGFloat webSeedTableTopMargin = hide ? -NSHeight([[fWebSeedTable enclosingScrollView] frame]) : fViewTopMargin;

    [(animate ? [fWebSeedTableTopConstraint animator] : fWebSeedTableTopConstraint) setConstant: webSeedTableTopMargin];
}

- (NSArray *) peerSortDescriptors
{
    NSMutableArray * descriptors = [NSMutableArray arrayWithCapacity: 2];

    NSArray * oldDescriptors = [fPeerTable sortDescriptors];
    BOOL useSecond = YES, asc = YES;
    if ([oldDescriptors count] > 0)
    {
        NSSortDescriptor * descriptor = oldDescriptors[0];
        [descriptors addObject: descriptor];

        if ((useSecond = ![[descriptor key] isEqualToString: @"IP"]))
            asc = [descriptor ascending];
    }

    //sort by IP after primary sort
    if (useSecond)
    {
        NSSortDescriptor * secondDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"IP" ascending: asc selector: @selector(compareNumeric:)];
        [descriptors addObject: secondDescriptor];
    }

    return descriptors;
}

@end
